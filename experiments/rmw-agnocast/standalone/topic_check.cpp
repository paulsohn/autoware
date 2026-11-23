#include <rclcpp/rclcpp.hpp>

#include <std_msgs/msg/string.hpp>

#include <fstream>
#include <iostream>
#include <thread>
using namespace std::chrono_literals;

int main(int argc, char ** argv)
{
  if (argc != 3) return 2;
  rclcpp::init(argc, argv);
  rclcpp::NodeOptions options;
  options.enable_rosout(false)
    .start_parameter_event_publisher(false)
    .start_parameter_services(false)
    .use_clock_thread(false)
    .parameter_overrides({rclcpp::Parameter("start_type_description_service", false)});
  auto node = std::make_shared<rclcpp::Node>(argv[1], options);
  const std::string payload = std::string(8192, 'x') + argv[2];
  bool success = false;
  if (std::string(argv[1]) == "writer") {
    auto pub = node->create_publisher<std_msgs::msg::String>(argv[2], 4);
    auto deadline = std::chrono::steady_clock::now() + 5s;
    while (!pub->get_subscription_count() && std::chrono::steady_clock::now() < deadline)
      std::this_thread::sleep_for(20ms);
    if (pub->get_subscription_count() == 1) {
      if (std::getenv("CHECK_NATIVE_LOAN")) {
        if (!pub->can_loan_messages()) return 4;
        auto loan = pub->borrow_loaned_message();
        loan.get().data = payload;
        pub->publish(std::move(loan));
      } else {
        std_msgs::msg::String msg;
        msg.data = payload;
        pub->publish(msg);
      }
      std::this_thread::sleep_for(300ms);
      success = true;
    }
  } else {
    size_t callbacks = 0;
    auto sub = node->create_subscription<std_msgs::msg::String>(
      argv[2], 4, [&](const std_msgs::msg::String & msg) {
        ++callbacks;
        success = msg.data == payload;
      });
    rclcpp::executors::SingleThreadedExecutor exec;
    exec.add_node(node);
    if (std::getenv("CHECK_DDS_ISOLATION")) {
      const auto deadline = std::chrono::steady_clock::now() + 3s;
      size_t matches = 0;
      while (std::chrono::steady_clock::now() < deadline) {
        matches += sub->get_publisher_count();
        exec.spin_once(100ms);
      }
      std::cout << "native reader callbacks=" << callbacks << " observed_matches=" << matches
                << std::endl;
      success = !callbacks && !matches;
    } else {
      auto deadline = std::chrono::steady_clock::now() + 7s;
      while (!success && std::chrono::steady_clock::now() < deadline) exec.spin_once(1s);
    }
    exec.remove_node(node);
  }
  std::ifstream maps("/proc/self/maps");
  for (std::string line; std::getline(maps, line);)
    if (line.find("librmw_cyclonedds") != line.npos || line.find("libddsc.so") != line.npos)
      return 3;
  node.reset();
  rclcpp::shutdown();
  std::cout << argv[1] << " success=" << success << std::endl;
  return success ? 0 : 1;
}
