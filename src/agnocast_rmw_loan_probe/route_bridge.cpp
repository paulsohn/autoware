#include <nav_msgs/msg/path.hpp>
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/point_cloud2.hpp>

#include <array>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iomanip>
#include <sstream>
#include <string_view>

std::string gid(const rmw_gid_t & value)
{
  std::ostringstream out;
  out << (value.implementation_identifier ? value.implementation_identifier : "(null)") << '/';
  for (auto byte : value.data) out << std::hex << std::setw(2) << std::setfill('0') << unsigned(byte);
  return out.str();
}

template<class Message>
void relay(const char * input, const char * output, const rclcpp::QoS & qos, bool demand)
{
  auto node = std::make_shared<rclcpp::Node>("agnocast_route_bridge");
  typename rclcpp::Subscription<Message>::SharedPtr sub;
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
      RCLCPP_INFO(node->get_logger(), "matches=%d received=%zu forwarded=%zu",
        event.current_count, received, forwarded);
    };
  }
  auto pub = node->create_publisher<Message>(output, qos, options);
  attach = [&] {
    sub = node->create_subscription<Message>(input, qos,
      [&](const Message & msg, const rclcpp::MessageInfo & info) {
        auto * row = trace && recorded < timings.size() ? &timings[recorded] : nullptr;
        if (row) {
          (*row)[0] = rclcpp::Time(msg.header.stamp).nanoseconds();
          (*row)[1] = node->now().nanoseconds();
        }
        ++received;
        if (pub->get_subscription_count() == 0) return;
        if (!forwarded) RCLCPP_INFO(node->get_logger(), "ingress_gid=%s egress_gid=%s",
          gid(info.get_rmw_message_info().publisher_gid).c_str(), gid(pub->get_gid()).c_str());
        pub->publish(msg);
        if (row) {
          (*row)[2] = node->now().nanoseconds();
          ++recorded;
        }
        ++forwarded;
      });
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

int main(int argc, char ** argv)
{
  if (argc != 5) return 2;
  std::string direction(argv[1]), type(argv[2]);
  const char * rmw = std::getenv("RMW_IMPLEMENTATION");
  if ((direction != "a2r" && direction != "r2a") || (type != "path" && type != "cloud") ||
      argv[3][0] != '/' || argv[4][0] != '/' ||
      (!rmw || std::string_view(rmw) != "rmw_agnocast")) return 2;
  if (setenv("AGNOCAST_RMW_PUBLISH_TOPICS", direction == "r2a" ? argv[4] : "", 1) ||
      setenv("AGNOCAST_RMW_SUBSCRIBE_TOPICS", direction == "a2r" ? argv[3] : "", 1)) return 1;
  const bool demand = std::getenv("AGNOCAST_BRIDGE_DEMAND") != nullptr;
  if (demand && (direction != "a2r" || std::string_view(argv[3]) == argv[4])) return 2;
  rclcpp::init(argc, argv);
  if (type == "path") relay<nav_msgs::msg::Path>(argv[3], argv[4], rclcpp::QoS(4), demand);
  else relay<sensor_msgs::msg::PointCloud2>(argv[3], argv[4], rclcpp::SensorDataQoS(), demand);
  rclcpp::shutdown();
}
