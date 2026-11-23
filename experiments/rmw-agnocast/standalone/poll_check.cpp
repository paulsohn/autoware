#include <rclcpp/rclcpp.hpp>

#include <std_msgs/msg/int32.hpp>
#include <std_srvs/srv/set_bool.hpp>

#include <filesystem>
#include <iostream>
#include <thread>

using namespace std::chrono_literals;
using Msg = std_msgs::msg::Int32;
using Service = std_srvs::srv::SetBool;

size_t eventfd_count()
{
  size_t count = 0;
  for (const auto & entry : std::filesystem::directory_iterator("/proc/self/fd")) {
    std::error_code ec;
    if (std::filesystem::read_symlink(entry.path(), ec) == "anon_inode:[eventfd]") ++count;
  }
  return count;
}

int main(int argc, char ** argv)
{
  const char * topic = std::getenv("AGNOCAST_RMW_POLL_TOPIC");
  if (!topic) return 1;
  rclcpp::init(argc, argv);
  auto node = std::make_shared<rclcpp::Node>("poll_check");
  auto context = node->get_node_base_interface()->get_context()->get_rcl_context();
  auto ctx = rcl_context_get_rmw_context(context.get());
  auto sub = node->create_subscription<Msg>(topic, 64, [](const Msg &) {});
  auto pub = node->create_publisher<Msg>(topic, rclcpp::QoS(64).deadline(120ms));
  auto server = node->create_service<Service>(
    "poll_rpc", [](Service::Request::SharedPtr, Service::Response::SharedPtr) {});
  auto client = node->create_client<Service>("poll_rpc");
  auto raw = rcl_subscription_get_rmw_handle(sub->get_subscription_handle().get());
  auto srv = rcl_service_get_rmw_handle(server->get_service_handle().get());
  auto guard = rmw_create_guard_condition(ctx);
  auto set = rmw_create_wait_set(ctx, 3);
  if (!guard || !set || !client->wait_for_service(2s)) return 2;
  void *subscription, *trigger, *service;
  auto wait = [&](const rmw_time_t * timeout, bool include_service = false) {
    subscription = raw->data;
    trigger = guard->data;
    service = srv->data;
    rmw_subscriptions_t subs{1, &subscription};
    rmw_guard_conditions_t guards{1, &trigger};
    rmw_services_t services{1, &service};
    return rmw_wait(
      &subs, &guards, include_service ? &services : nullptr, nullptr, nullptr, set, timeout);
  };
  rmw_time_t short_timeout{0, 30000000};
  auto start = std::chrono::steady_clock::now();
  if (wait(&short_timeout) != RMW_RET_TIMEOUT || subscription || trigger) return 3;
  if (std::chrono::steady_clock::now() - start < 30ms) return 12;
  // Retained mixed wait sets each own one descriptor until destruction. Idle
  // notifications accumulate, so guard-only and mixed waits must recheck readiness.
  const auto baseline = eventfd_count();
  const auto option = std::getenv("AGNOCAST_RMW_REUSE_WAIT_FD");
  const bool reuse = option && (std::string_view(option) == "1" || std::string_view(option) == "drain");
  for (int i = 0; i < 16; ++i) {
    auto second = rmw_create_wait_set(ctx, 2);
    if (!second) return 20;
    rmw_time_t immediate{};
    for (int repeat = 0; repeat < 3; ++repeat) {
      void * reader = raw->data;
      rmw_subscriptions_t readers{1, &reader};
      if (
        rmw_wait(&readers, nullptr, nullptr, nullptr, nullptr, second, &immediate) !=
          RMW_RET_TIMEOUT ||
        reader || eventfd_count() != baseline + size_t(reuse))
        return 21;
      if (
        rmw_trigger_guard_condition(guard) != RMW_RET_OK || wait(&immediate) != RMW_RET_OK ||
        !trigger || subscription)
        return 22;
    }
    if (rmw_destroy_wait_set(second) != RMW_RET_OK || eventfd_count() != baseline) return 23;
  }
  std::cout << "wait-set descriptor lifetime and stale wake checks passed, reuse=" << reuse << '\n';
  auto later = [](auto wake) {
    return std::thread([wake] {
      std::this_thread::sleep_for(30ms);
      wake();
    });
  };
  auto wake = later([&] {
    if (rmw_trigger_guard_condition(guard) != RMW_RET_OK) std::abort();
  });
  auto result = wait(nullptr);
  wake.join();
  if (result != RMW_RET_OK || subscription || !trigger) return 4;
  Msg sample;
  sample.data = 42;
  pub->publish(sample);
  if (rmw_trigger_guard_condition(guard) != RMW_RET_OK) return 5;
  rmw_time_t zero{};
  if (wait(&zero) != RMW_RET_OK || !subscription || !trigger) return 6;
  Msg received;
  bool taken{};
  if (rmw_take(raw, &received, &taken, nullptr) != RMW_RET_OK || !taken || received != sample)
    return 7;
  pub->publish(sample);
  if (rmw_take(raw, &received, &taken, nullptr) != RMW_RET_OK || !taken || received != sample)
    return 8;
  // Exceed MAX_RECEIVE_NUM on each of two compatible native durability routes.
  auto durable = node->create_publisher<Msg>(topic, rclcpp::QoS(64).transient_local());
  for (int seq = 0; seq < 16; ++seq) {
    sample.data = seq;
    pub->publish(sample);
    sample.data = 100 + seq;
    durable->publish(sample);
  }
  int volatile_seq = 0, durable_seq = 0;
  for (int i = 0; i < 32; ++i) {
    if (
      wait(&short_timeout) != RMW_RET_OK || !subscription ||
      rmw_take(raw, &received, &taken, nullptr) != RMW_RET_OK || !taken)
      return 16;
    if (received.data >= 100) {
      if (received.data != 100 + durable_seq++) return 17;
    } else if (received.data != volatile_seq++)
      return 18;
  }
  if (volatile_seq != 16 || durable_seq != 16 || wait(&zero) != RMW_RET_TIMEOUT) return 19;
  wake = later([&] { client->async_send_request(std::make_shared<Service::Request>()); });
  result = wait(nullptr, true);
  wake.join();
  if (result != RMW_RET_OK || !service || subscription || trigger) return 9;
  rmw_event_t deadline = rmw_get_zero_initialized_event();
  auto writer = rcl_publisher_get_rmw_handle(pub->get_publisher_handle().get());
  if (rmw_publisher_event_init(&deadline, writer, RMW_EVENT_OFFERED_DEADLINE_MISSED) != RMW_RET_OK)
    return 13;
  void * event = &deadline;
  rmw_events_t events{1, &event};
  subscription = raw->data;
  rmw_subscriptions_t subs{1, &subscription};
  if (
    rmw_wait(&subs, nullptr, nullptr, nullptr, &events, set, nullptr) != RMW_RET_OK ||
    subscription || !event)
    return 14;
  if (rmw_event_fini(&deadline) != RMW_RET_OK) return 15;
  wake = later([&] { rclcpp::shutdown(); });
  result = wait(nullptr);
  wake.join();
  if (result != RMW_RET_OK || subscription || trigger) return 10;
  if (rmw_destroy_wait_set(set) != RMW_RET_OK || rmw_destroy_guard_condition(guard) != RMW_RET_OK)
    return 11;
  std::cout << "timeout, guard, simultaneous data and guard, independent take, two-route burst, RPC, deadline and "
               "shutdown passed\n";
}
