#include <rclcpp/rclcpp.hpp>

#include <std_msgs/msg/int32.hpp>
#include <std_msgs/msg/string.hpp>

#include <rmw/features.h>
#include <rmw/get_network_flow_endpoints.h>

#include <atomic>
#include <iostream>
using namespace std::chrono_literals;
using Msg = std_msgs::msg::String;
int main(int argc, char ** argv)
{
  if (argc != 2) return 2;
  rclcpp::init(argc, argv);
  auto node = std::make_shared<rclcpp::Node>("api_checks");
  bool match = false, incompatible = false, other_type = false;
  rclcpp::PublisherOptions opts;
  opts.event_callbacks.matched_callback = [&](rclcpp::MatchedInfo & status) {
    match |= status.current_count > 0;
  };
  opts.event_callbacks.incompatible_qos_callback =
    [&](rclcpp::QOSOfferedIncompatibleQoSInfo & status) { incompatible |= status.total_count > 0; };
  opts.event_callbacks.incompatible_type_callback = [&](rclcpp::IncompatibleTypeInfo & status) {
    other_type |= status.total_count > 0;
  };
  auto pub = node->create_publisher<Msg>(argv[1], rclcpp::QoS(4).best_effort(), opts);
  auto bad = node->create_subscription<Msg>(argv[1], 4, [](const Msg &) {});
  auto wrong = node->create_subscription<std_msgs::msg::Int32>(
    argv[1], 4, [](const std_msgs::msg::Int32 &) {});
  auto sub =
    node->create_subscription<Msg>(argv[1], rclcpp::QoS(4).best_effort(), [](const Msg &) {});
  if (
    node->count_publishers(argv[1]) != 1 || node->count_subscribers(argv[1]) != 3 ||
    node->get_publishers_info_by_topic(argv[1]).size() != 1 ||
    node->get_subscriptions_info_by_topic(argv[1]).size() != 3 ||
    node->get_topic_names_and_types().at(argv[1]).size() != 2)
    return 3;
  rclcpp::executors::SingleThreadedExecutor exec;
  exec.add_node(node);
  auto until = std::chrono::steady_clock::now() + 3s;
  while (!(match && incompatible && other_type) && std::chrono::steady_clock::now() < until)
    exec.spin_once(50ms);
  std::cout << "matched=" << match << " incompatible_qos=" << incompatible
            << " incompatible_type=" << other_type << std::endl;
  if (!match || !incompatible || !other_type) return 4;
  sub->set_content_filter("data = %0", {"'accepted'"});
  if (sub->get_content_filter().filter_expression != "data = %0") return 5;
  std::atomic<size_t> callbacks{};
  auto subscription = rcl_subscription_get_rmw_handle(sub->get_subscription_handle().get());
  auto publisher = rcl_publisher_get_rmw_handle(pub->get_publisher_handle().get());
  auto notified = [](const void * ptr, size_t n) {
    static_cast<std::atomic<size_t> *>(const_cast<void *>(ptr))->fetch_add(n);
  };
  if (
    rmw_subscription_set_on_new_message_callback(subscription, notified, &callbacks) != RMW_RET_OK)
    return 6;
  rclcpp::Serialization<Msg> codec;
  rclcpp::SerializedMessage bytes;
  for (auto value : {"rejected", "accepted"}) {
    Msg m;
    m.data = value;
    codec.serialize_message(&m, &bytes);
    if (
      rmw_publish_serialized_message(publisher, &bytes.get_rcl_serialized_message(), nullptr) !=
      RMW_RET_OK)
      return 7;
  }
  until = std::chrono::steady_clock::now() + 3s;
  while (callbacks == 0 && std::chrono::steady_clock::now() < until)
    std::this_thread::sleep_for(10ms);
  bool taken = false;
  rmw_message_info_t info{};
  if (
    rmw_take_serialized_message_with_info(
      subscription, &bytes.get_rcl_serialized_message(), &taken, &info, nullptr) != RMW_RET_OK ||
    !taken)
    return 8;
  Msg decoded;
  codec.deserialize_message(&bytes, &decoded);
  if (
    decoded.data != "accepted" || callbacks != 1 || info.publication_sequence_number != 2 ||
    info.reception_sequence_number != 1 || !info.source_timestamp || !info.received_timestamp)
    return 9;
  if (rmw_subscription_set_on_new_message_callback(subscription, nullptr, nullptr) != RMW_RET_OK)
    return 10;
  auto a = rcutils_get_default_allocator();
  auto flows = rmw_get_zero_initialized_network_flow_endpoint_array();
  if (
    rmw_publisher_get_network_flow_endpoints(publisher, &a, &flows) != RMW_RET_OK || flows.size ||
    rmw_network_flow_endpoint_array_fini(&flows) != RMW_RET_OK)
    return 11;
  std::cout << "graph, callbacks, content filter, serialized transport, sequence numbers and empty "
               "network flow passed"
            << std::endl;
  rclcpp::shutdown();
  return 0;
}
