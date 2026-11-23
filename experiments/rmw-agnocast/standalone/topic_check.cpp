#include <rclcpp/rclcpp.hpp>

#include <std_msgs/msg/string.hpp>

#include <fstream>
#include <iostream>
#include <thread>
using namespace std::chrono_literals;

bool check_qos()
{
  auto reliable = rmw_qos_profile_default;
  auto best_effort = reliable;
  best_effort.reliability = RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT;
  auto transient = reliable;
  transient.durability = RMW_QOS_POLICY_DURABILITY_TRANSIENT_LOCAL;
  rmw_qos_compatibility_type_t result{};
  char reason[128];
  return rmw_qos_profile_check_compatible(reliable, best_effort, &result, reason, sizeof(reason)) ==
           RMW_RET_OK &&
         result == RMW_QOS_COMPATIBILITY_OK && !reason[0] &&
         rmw_qos_profile_check_compatible(best_effort, reliable, &result, reason, sizeof(reason)) ==
           RMW_RET_OK &&
         result == RMW_QOS_COMPATIBILITY_ERROR && reason[0] &&
         rmw_qos_profile_check_compatible(reliable, transient, &result, reason, 1) == RMW_RET_OK &&
         result == RMW_QOS_COMPATIBILITY_ERROR && !reason[0];
}

bool check_batch(const rmw_subscription_t * sub, const std::string & payload)
{
  std_msgs::msg::String values[3];
  void * slots[]{&values[0], &values[1], &values[2]};
  rmw_message_info_t metadata[3]{};
  auto allocator = rcutils_get_default_allocator();
  rmw_message_sequence_t messages{slots, 0, 3, &allocator};
  rmw_message_info_sequence_t infos{metadata, 0, 3, &allocator};
  size_t count = 0;
  if (rmw_take_sequence(sub, 4, &messages, &infos, &count, nullptr) != RMW_RET_INVALID_ARGUMENT)
    return false;
  rmw_reset_error();
  auto deadline = std::chrono::steady_clock::now() + 7s;
  // Allow the native receive worker to enqueue the three-message burst.
  std::this_thread::sleep_for(300ms);
  bool saw_pair = false;
  size_t total = 0;
  while (total < 3 && std::chrono::steady_clock::now() < deadline) {
    if (rmw_take_sequence(sub, 2, &messages, &infos, &count, nullptr) != RMW_RET_OK) return false;
    if (!count) {
      std::this_thread::sleep_for(20ms);
      continue;
    }
    if (count == 2) saw_pair = true;
    if (messages.size != count || infos.size != count) return false;
    for (size_t i = 0; i < count; ++i, ++total)
      if (
        values[i].data != payload + std::to_string(total) ||
        !metadata[i].publisher_gid.implementation_identifier)
        return false;
  }
  const auto previous = messages.size;
  return saw_pair && total == 3 &&
         rmw_take_sequence(sub, 2, &messages, &infos, &count, nullptr) == RMW_RET_OK && !count &&
         messages.size == previous && infos.size == previous;
}

int main(int argc, char ** argv)
{
  if (argc != 3) return 2;
  rclcpp::init(argc, argv);
  if (!check_qos()) return 5;
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
      if (std::getenv("CHECK_BATCH")) {
        for (size_t i = 0; i < 3; ++i) {
          std_msgs::msg::String msg;
          msg.data = payload + std::to_string(i);
          pub->publish(msg);
        }
      } else if (std::getenv("CHECK_NATIVE_LOAN")) {
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
    if (std::getenv("CHECK_BATCH")) {
      success =
        check_batch(rcl_subscription_get_rmw_handle(sub->get_subscription_handle().get()), payload);
    } else if (std::getenv("CHECK_DDS_ISOLATION")) {
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
