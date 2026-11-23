#include <rclcpp/rclcpp.hpp>

#include <geometry_msgs/msg/pose_with_covariance.hpp>
#include <std_msgs/msg/string.hpp>

#include <cstdlib>
#include <iostream>
#include <thread>
using namespace std::chrono_literals;
using Msg = geometry_msgs::msg::PoseWithCovariance;
namespace
{
thread_local bool counting = false;
thread_local size_t allocations = 0;
}  // namespace
void * operator new(size_t n)
{
  if (counting) ++allocations;
  if (auto p = std::malloc(n ? n : 1)) return p;
  throw std::bad_alloc();
}
void operator delete(void * p) noexcept
{
  std::free(p);
}
void operator delete(void * p, size_t) noexcept
{
  std::free(p);
}
int main(int argc, char ** argv)
{
  if (argc != 2) return 2;
  rclcpp::init(argc, argv);
  auto node = std::make_shared<rclcpp::Node>("deadline_allocation_probe");
  int offered = 0, requested = 0;
  rclcpp::PublisherOptions pub_options;
  pub_options.event_callbacks.deadline_callback = [&](auto & e) {
    offered += e.total_count_change;
    if (offered != e.total_count) std::abort();
  };
  rclcpp::SubscriptionOptions sub_options;
  sub_options.event_callbacks.deadline_callback = [&](auto & e) {
    requested += e.total_count_change;
    if (requested != e.total_count) std::abort();
  };
  auto qos = rclcpp::QoS(16).deadline(250ms);
  auto sub = node->create_subscription<Msg>(argv[1], qos, [](const Msg &) {}, sub_options);
  auto pub = node->create_publisher<Msg>(argv[1], rclcpp::QoS(16).deadline(180ms), pub_options);
  auto incompatible = node->create_publisher<Msg>(argv[1], rclcpp::QoS(16));
  if (
    incompatible->get_subscription_count() != 0 || sub->get_publisher_count() != 1 ||
    pub->get_subscription_count() != 1)
    return 3;
  auto raw = rcl_subscription_get_rmw_handle(sub->get_subscription_handle().get());
  rmw_subscription_allocation_t plan{};
  auto ts = rosidl_typesupport_cpp::get_message_type_support_handle<Msg>();
  if (
    rmw_init_subscription_allocation(
      rosidl_typesupport_cpp::get_message_type_support_handle<std_msgs::msg::String>(), nullptr,
      &plan) != RMW_RET_UNSUPPORTED ||
    plan.data)
    return 4;
  rmw_reset_error();
  if (rmw_init_subscription_allocation(ts, nullptr, &plan) != RMW_RET_OK) return 5;
  Msg sample, out;
  // Resolve the distro dispatcher's lazy RMW function pointers before measuring the middleware.
  bool empty = true;
  if (rmw_take(raw, &out, &empty, &plan) != RMW_RET_OK || empty) return 12;
  void * slot = &out;
  rmw_message_info_t metadata{};
  rmw_message_sequence_t destinations{&slot, 0, 1, nullptr};
  rmw_message_info_sequence_t metadata_sequence{&metadata, 0, 1, nullptr};
  size_t n = 1;
  if (rmw_take_sequence(raw, 1, &destinations, &metadata_sequence, &n, &plan) != RMW_RET_OK || n)
    return 13;
  incompatible->publish(sample);
  for (int i = 1; i <= 32; ++i) {
    sample.pose.position.x = i;
    sample.covariance.back() = -i;
    pub->publish(sample);
    auto until = std::chrono::steady_clock::now() + 2s;
    bool taken = false;
    while (!taken && std::chrono::steady_clock::now() < until) {
      counting = true;
      rmw_ret_t ret;
      if (i % 2) {
        ret = rmw_take(raw, &out, &taken, &plan);
      } else {
        void * dest = &out;
        rmw_message_info_t meta{};
        rmw_message_sequence_t msgs{&dest, 0, 1, nullptr};
        rmw_message_info_sequence_t infos{&meta, 0, 1, nullptr};
        size_t n{};
        ret = rmw_take_sequence(raw, 1, &msgs, &infos, &n, &plan);
        taken = n == 1;
      }
      counting = false;
      if (ret != RMW_RET_OK) return 6;
      if (!taken) std::this_thread::sleep_for(1ms);
    }
    if (!taken || out != sample) return 7;
  }
  std::cout << "typed takes=32 C++ new calls=" << allocations << std::endl;
  if (allocations || rmw_fini_subscription_allocation(&plan) != RMW_RET_OK || plan.data) return 8;
  rclcpp::executors::SingleThreadedExecutor exec;
  exec.add_node(node);
  auto wait_for_misses = [&](int o, int r) {
    auto until = std::chrono::steady_clock::now() + 2s;
    while ((offered < o || requested < r) && std::chrono::steady_clock::now() < until)
      exec.spin_once(1s);
    return offered >= o && requested >= r;
  };
  if (!wait_for_misses(2, 2)) return 9;
  pub->publish(sample);
  exec.spin_some();
  auto o = offered, r = requested;
  auto until = std::chrono::steady_clock::now() + 400ms;
  while (std::chrono::steady_clock::now() < until) {
    pub->publish(sample);
    exec.spin_some();
    std::this_thread::sleep_for(20ms);
  }
  exec.spin_some();
  if (offered != o || requested != r || !wait_for_misses(o + 1, r + 1)) return 10;
  std::cout << "offered=" << offered << " requested=" << requested << " pause/resume passed"
            << std::endl;
  pub.reset();
  auto settle = std::chrono::steady_clock::now() + 350ms;
  while (std::chrono::steady_clock::now() < settle) exec.spin_once(20ms);
  auto disconnected = requested;
  settle = std::chrono::steady_clock::now() + 550ms;
  while (std::chrono::steady_clock::now() < settle) exec.spin_once(20ms);
  if (sub->get_publisher_count() != 0 || disconnected != requested) return 11;
  std::cout << "requested deadline disarmed after writer exit" << std::endl;
  rclcpp::shutdown();
}
