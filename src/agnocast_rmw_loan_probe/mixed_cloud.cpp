#include <rmw_agnocast/host_cloud_view.hpp>
#include <agnocast/node/agnocast_node.hpp>
#include <agnocast/node/agnocast_only_single_threaded_executor.hpp>
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/point_cloud2.hpp>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <thread>

using Cloud = sensor_msgs::msg::PointCloud2;
using namespace std::chrono_literals;
constexpr size_t bytes_per_frame = 256 * 1024;
constexpr char topic[] = "/ownership";
const auto qos = rclcpp::SensorDataQoS();
int64_t now_ns()
{
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
    std::chrono::system_clock::now().time_since_epoch()).count();
}
Cloud & get(agnocast::ipc_shared_ptr<Cloud> & loan) { return *loan; }
Cloud & get(rclcpp::LoanedMessage<Cloud> & loan) { return loan.get(); }

template<class Publisher>
void send(Publisher & pub, bool scope_allocation)
{
  while (!std::filesystem::exists("start")) std::this_thread::sleep_for(5ms);
  auto due = std::chrono::steady_clock::now();
  for (unsigned seq = 1; !std::filesystem::exists("stop"); ++seq) {
    auto loan = pub.borrow_loaned_message();
    auto & msg = get(loan);
    msg.header.frame_id = "lidar";
    msg.width = bytes_per_frame / 4;
    msg.height = 1;
    msg.point_step = 4;
    msg.row_step = bytes_per_frame;
    msg.fields.resize(1);
    msg.fields[0].name = "intensity";
    msg.fields[0].datatype = sensor_msgs::msg::PointField::UINT32;
    msg.fields[0].count = 1;
    if (scope_allocation) {
      struct SharedAllocation {
        SharedAllocation() { agnocast::increment_borrowed_publisher_num(); }
        ~SharedAllocation() { agnocast::decrement_borrowed_publisher_num(); }
      } scope;
      msg.data.resize(bytes_per_frame);
    } else {
      msg.data.resize(bytes_per_frame);
    }
    auto & bytes = static_cast<std::vector<uint8_t> &>(msg.data);
    for (size_t i = 0; i < bytes.size(); ++i) bytes[i] = uint8_t(seq + i);
    std::memcpy(bytes.data(), &seq, sizeof(seq));
    const auto address = reinterpret_cast<uintptr_t>(bytes.data());
    const auto stamp = now_ns();
    msg.header.stamp = rclcpp::Time(stamp);
    pub.publish(std::move(loan));
    std::printf("sent %u %ld %lu\n", seq, long(stamp), address);
    std::fflush(stdout);
    due += 50ms;
    std::this_thread::sleep_until(due);
  }
}

const std::vector<uint8_t> & data(const Cloud & msg, bool view)
{
  if (view) return dynamic_cast<const rmw_agnocast::HostCloudView &>(*msg.data.get_impl()).bytes();
  return static_cast<const std::vector<uint8_t> &>(msg.data);
}
unsigned verify(const Cloud & msg, const std::vector<uint8_t> & bytes)
{
  if (bytes.size() != bytes_per_frame || msg.header.frame_id != "lidar" ||
      msg.width != bytes_per_frame / 4 || msg.height != 1 || msg.point_step != 4 ||
      msg.row_step != bytes_per_frame || msg.fields.size() != 1 ||
      msg.fields[0].name != "intensity" || msg.fields[0].count != 1 ||
      msg.fields[0].datatype != sensor_msgs::msg::PointField::UINT32)
    throw std::runtime_error("cloud metadata");
  unsigned seq;
  std::memcpy(&seq, bytes.data(), sizeof(seq));
  for (size_t i = sizeof(seq); i < bytes.size(); ++i)
    if (bytes[i] != uint8_t(seq + i)) throw std::runtime_error("payload bytes");
  return seq;
}
void receive(const Cloud & msg, bool view, bool native)
{
  const auto entry = now_ns();
  if (!native && msg.data.get_backend_type() != (view ? "agnocast_host_view" : "cpu"))
    throw std::runtime_error("unexpected payload backend");
  const auto & bytes = data(msg, view);
  const auto seq = verify(msg, bytes);
  std::printf("got %u %ld %lu\n", seq, long(entry - rclcpp::Time(msg.header.stamp).nanoseconds()),
    reinterpret_cast<uintptr_t>(bytes.data()));
  std::fflush(stdout);
}
int main(int argc, char ** argv)
{
  if (argc != 3) return 2;
  const bool native = std::string(argv[1]) == "native";
  const std::string role = argv[2];
  if (native) {
    agnocast::init(argc, argv);
    auto node = std::make_shared<agnocast::Node>("ownership_" + role);
    if (role == "writer") {
      agnocast::Publisher<Cloud> pub(node.get(), topic, qos, {}, agnocast::PublisherRole::AgnocastOnly);
      send(pub, false);
    } else {
      agnocast::AgnocastOnlySingleThreadedExecutor exec;
      auto sub = std::make_shared<agnocast::Subscription<Cloud>>(node.get(), topic, qos,
        [](agnocast::ipc_shared_ptr<const Cloud> msg) { receive(*msg, false, true); },
        agnocast::SubscriptionOptions{}, agnocast::SubscriptionRole::AgnocastOnly);
      exec.add_node(node);
      std::ofstream("cpu-ready").close();
      exec.spin();
    }
    agnocast::shutdown();
  } else {
    rclcpp::init(argc, argv);
    auto node = std::make_shared<rclcpp::Node>("ownership_" + role);
    if (role == "writer") {
      auto pub = node->create_publisher<Cloud>(topic, qos);
      const bool shared = std::string(argv[1]) == "rmw";
      if (shared && !pub->can_loan_messages()) return 3;
      send(*pub, shared);
    } else {
      const bool view = role == "holder";
      rclcpp::SubscriptionOptions options;
      if (view) options.acceptable_buffer_backends = "agnocast_host_view";
      Cloud::ConstSharedPtr held;
      unsigned held_seq = 0;
      auto sub = node->create_subscription<Cloud>(role == "recorder" ? "/mirror" : topic, qos,
        [&](Cloud::ConstSharedPtr msg) {
          receive(*msg, view, false);
          if (!view) return;
          if (!held) {
            held = msg;
            held_seq = verify(*held, data(*held, true));
          } else if (verify(*held, data(*held, true)) != held_seq) {
            throw std::runtime_error("retained cloud changed");
          }
        }, options);
      std::ofstream(role + "-ready").close();
      rclcpp::spin(node);
    }
    rclcpp::shutdown();
  }
}
