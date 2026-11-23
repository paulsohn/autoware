#include <rcl_interfaces/msg/log.hpp>
#include <rclcpp/rclcpp.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

using namespace std::chrono_literals;
int main(int argc, char ** argv)
{
  if (argc != 3) return 2;
  bool writer = std::string(argv[1]) == "writer";
  std::filesystem::path dir(argv[2]);
  rclcpp::init(argc, argv);
  rclcpp::NodeOptions options;
  options.enable_rosout(writer)
    .start_parameter_event_publisher(false)
    .start_parameter_services(false)
    .use_clock_thread(false)
    .parameter_overrides({rclcpp::Parameter("start_type_description_service", false)});
  auto node =
    std::make_shared<rclcpp::Node>(writer ? "rosout_native_writer" : "rosout_late_reader", options);
  if (writer) {
    RCLCPP_WARN(node->get_logger(), "expired %s", argv[2]);
    std::this_thread::sleep_for(11s);
    RCLCPP_WARN(node->get_logger(), "fresh %s", argv[2]);
    std::ofstream(dir / "published").put('1');
    auto deadline = std::chrono::steady_clock::now() + 20s;
    while (!std::filesystem::exists(dir / "received") &&
           std::chrono::steady_clock::now() < deadline)
      std::this_thread::sleep_for(20ms);
    if (!std::filesystem::exists(dir / "received")) return 3;
  } else {
    bool fresh = false, expired = false;
    auto qos = rclcpp::QoS(
      rclcpp::QoSInitialization::from_rmw(rmw_qos_profile_rosout_default),
      rmw_qos_profile_rosout_default);
    auto sub = node->create_subscription<rcl_interfaces::msg::Log>(
      "/rosout", qos, [&](const rcl_interfaces::msg::Log & log) {
        if (log.name != "rosout_native_writer") return;
        expired |= log.msg == "expired " + dir.string();
        fresh |= log.msg == "fresh " + dir.string() && log.level == log.WARN && log.line &&
                 log.stamp.sec && log.function == "main" &&
                 log.file.find("rosout_check.cpp") != std::string::npos;
      });
    rclcpp::executors::SingleThreadedExecutor exec;
    exec.add_node(node);
    auto deadline = std::chrono::steady_clock::now() + 3s;
    while (std::chrono::steady_clock::now() < deadline) exec.spin_once(100ms);
    std::cout << "late rosout fresh=" << fresh << " expired=" << expired << std::endl;
    if (!fresh || expired) return 4;
    // Join while the retained sample is fresh, then let it expire before taking.
    auto delayed = node->create_subscription<rcl_interfaces::msg::Log>(
      "/rosout", qos, [](const rcl_interfaces::msg::Log &) {});
    if (!delayed->get_publisher_count()) return 5;
    std::this_thread::sleep_for(11s);
    rcl_interfaces::msg::Log sample;
    rclcpp::MessageInfo info;
    const bool queued = delayed->take(sample, info);
    std::cout << "queued rosout after expiry=" << queued << std::endl;
    if (queued || !delayed->get_publisher_count()) return 6;
    std::ofstream(dir / "received").put('1');
  }
  node.reset();
  rclcpp::shutdown();
  return 0;
}
