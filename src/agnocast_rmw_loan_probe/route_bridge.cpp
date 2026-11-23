#include <nav_msgs/msg/path.hpp>
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/point_cloud2.hpp>

#include <array>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iomanip>
#include <set>
#include <sstream>
#include <string_view>
#include <thread>
#include <type_traits>

std::string gid(const rmw_gid_t & value)
{
  std::ostringstream out;
  out << (value.implementation_identifier ? value.implementation_identifier : "(null)") << '/';
  for (auto byte : value.data) out << std::hex << std::setw(2) << std::setfill('0') << unsigned(byte);
  return out.str();
}

template<class Message>
void relay(const char * input, const char * output, const rclcpp::QoS & qos, bool demand,
           const std::string & type = {})
{
  constexpr bool generic = std::is_same_v<Message, rclcpp::SerializedMessage>;
  auto node = std::make_shared<rclcpp::Node>("agnocast_route_bridge");
  rclcpp::SubscriptionBase::SharedPtr sub;
  std::function<void()> attach;
  rclcpp::PublisherOptions options;
  size_t received = 0, forwarded = 0;
  const char * trace = std::getenv("AGNOCAST_BRIDGE_PROFILE");
  std::array<std::array<int64_t, 3>, 60> timings{};
  size_t recorded = 0;
  if (demand) {
    options.event_callbacks.matched_callback = [&](rclcpp::MatchedInfo & event) {
      if (event.current_count > 0 && !sub) attach();
      if (event.current_count == 0) sub.reset();
      RCLCPP_INFO(node->get_logger(), "matches=%zu received=%zu forwarded=%zu",
        static_cast<size_t>(event.current_count), received, forwarded);
    };
  }
  auto pub = [&] {
    if constexpr (generic) return node->create_generic_publisher(output, type, qos, options);
    else return node->create_publisher<Message>(output, qos, options);
  }();
  attach = [&] {
    auto forward = [&](const Message & msg, const rclcpp::MessageInfo & info) {
      auto * row = trace && recorded < timings.size() ? &timings[recorded] : nullptr;
      if (row) {
        if constexpr (!generic) (*row)[0] = rclcpp::Time(msg.header.stamp).nanoseconds();
        (*row)[1] = node->now().nanoseconds();
      }
      ++received;
      // Cyclone's count getter resets the same status used by matched_callback.
      // In demand mode the callback already owns attachment and detachment.
      if (!demand && pub->get_subscription_count() == 0) return;
      if (!forwarded) RCLCPP_INFO(node->get_logger(), "ingress_gid=%s egress_gid=%s",
        gid(info.get_rmw_message_info().publisher_gid).c_str(), gid(pub->get_gid()).c_str());
      pub->publish(msg);
      if (row) {
        (*row)[2] = node->now().nanoseconds();
        ++recorded;
      }
      ++forwarded;
    };
    if constexpr (generic) {
      sub = node->create_generic_subscription(input, type, qos,
        [forward](std::shared_ptr<const Message> msg, const rclcpp::MessageInfo & info) {
          forward(*msg, info);
        });
    } else {
      sub = node->create_subscription<Message>(input, qos, forward);
    }
  };
  if (!demand) attach();
  if (auto ready = std::getenv("BRIDGE_READY_FILE")) std::ofstream(ready) << "ready";
  rclcpp::spin(node);
  if (trace) {
    std::ofstream out(trace);
    out.exceptions(std::ios::failbit | std::ios::badbit);
    for (size_t i = 0; i < recorded; ++i)
      out << timings[i][0] << ',' << timings[i][1] << ',' << timings[i][2] << '\n';
  }
  RCLCPP_INFO(node->get_logger(), "received=%zu forwarded=%zu", received, forwarded);
}

std::string discover_type(const char * input)
{
  auto node = std::make_shared<rclcpp::Node>("agnocast_route_discovery");
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  std::string candidate;
  auto since = deadline;
  do {
    std::set<std::string> types;
    for (const auto & writer : node->get_publishers_info_by_topic(input))
      types.insert(writer.topic_type());
    if (types.size() > 1) throw std::runtime_error("ambiguous publisher types");
    const std::string observed = types.empty() ? "" : *types.begin();
    if (observed != candidate) {
      candidate = observed;
      since = std::chrono::steady_clock::now();
    }
    if (!candidate.empty() && std::chrono::steady_clock::now() - since >= std::chrono::milliseconds(200))
      return candidate;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  } while (rclcpp::ok() && std::chrono::steady_clock::now() < deadline);
  throw std::runtime_error("no discoverable publisher type");
}

int main(int argc, char ** argv)
{
  if (argc != 5 && argc != 6) return 2;
  const std::string qos_mode = argc == 6 ? argv[5] : "default";
  if (qos_mode != "default" && qos_mode != "sensor" && qos_mode != "reliable") return 2;
  std::string direction(argv[1]), type(argv[2]);
  const char * rmw = std::getenv("RMW_IMPLEMENTATION");
  if ((direction != "a2r" && direction != "r2a") ||
      argv[3][0] != '/' || argv[4][0] != '/' ||
      (!rmw || std::string_view(rmw) != "rmw_agnocast")) return 2;
  if (setenv("AGNOCAST_RMW_PUBLISH_TOPICS", direction == "r2a" ? argv[4] : "", 1) ||
      setenv("AGNOCAST_RMW_SUBSCRIBE_TOPICS", direction == "a2r" ? argv[3] : "", 1)) return 1;
  const bool demand = std::getenv("AGNOCAST_BRIDGE_DEMAND") != nullptr;
  if (demand && (direction != "a2r" || std::string_view(argv[3]) == argv[4])) return 2;
  rclcpp::init(argc, argv);
  if (type == "path") type = "nav_msgs/msg/Path";
  if (type == "cloud") type = "sensor_msgs/msg/PointCloud2";
  try {
    const bool discovered = type == "auto";
    if (discovered) type = discover_type(argv[3]);
    const bool cloud = type == "sensor_msgs/msg/PointCloud2";
    const bool typed = !std::getenv("AGNOCAST_BRIDGE_FORCE_GENERIC") &&
      (cloud || (!discovered && type == "nav_msgs/msg/Path"));
    rclcpp::QoS qos(4);
    if (qos_mode == "sensor" || (qos_mode == "default" && cloud)) qos = rclcpp::SensorDataQoS();
    if (cloud && std::getenv("SYNC_RECORDER_DEEP_HISTORY")) qos.keep_last(32);
    std::cerr << "route_type=" << type << " adapter=" << (typed ? "typed" : "serialized") << std::endl;
    if (!typed) relay<rclcpp::SerializedMessage>(argv[3], argv[4], qos, demand, type);
    else if (cloud) relay<sensor_msgs::msg::PointCloud2>(argv[3], argv[4], qos, demand);
    else relay<nav_msgs::msg::Path>(argv[3], argv[4], qos, demand);
  } catch (const std::exception & e) {
    std::cerr << "bridge " << direction << " type " << type << ": " << e.what() << '\n';
    rclcpp::shutdown();
    return 1;
  }
  rclcpp::shutdown();
}
