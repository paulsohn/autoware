#include <rclcpp/rclcpp.hpp>

#include <std_msgs/msg/string.hpp>
#include <std_srvs/srv/set_bool.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace std::chrono_literals;
using Msg = std_msgs::msg::String;
using Service = std_srvs::srv::SetBool;

static void require(bool ok, const char * message)
{
  if (!ok) throw std::runtime_error(message);
}

template <class Predicate>
static void wait_until(Predicate predicate, const char * message)
{
  auto end = std::chrono::steady_clock::now() + 5s;
  while (!predicate()) {
    require(std::chrono::steady_clock::now() < end, message);
    std::this_thread::sleep_for(1ms);
  }
}

static void topics(rclcpp::Node & node)
{
  auto regular = rclcpp::QoS(4);
  auto raw = regular;
  raw.avoid_ros_namespace_conventions(true);
  auto reader = node.create_subscription<Msg>("/alias", raw, [](Msg::ConstSharedPtr) {});
  auto wrong = node.create_publisher<Msg>("/alias", regular);
  auto writer = node.create_publisher<Msg>("/alias", raw);
  require(
    wrong->get_subscription_count() == 0 && writer->get_subscription_count() == 1 &&
      reader->get_publisher_count() == 1,
    "raw topic match isolation");
  require(
    node.get_publishers_info_by_topic("/alias").size() == 1 &&
      node.get_publishers_info_by_topic("/alias", true).size() == 1,
    "graph namespaces");
  Msg msg;
  msg.data = "ordinary";
  wrong->publish(msg);
  msg.data = "native";
  writer->publish(msg);
  rclcpp::MessageInfo info;
  wait_until([&] { return reader->take(msg, info); }, "raw topic delivery");
  require(msg.data == "native", "raw topic payload");

  auto p = rmw_qos_profile_default;
  auto s = p;
  auto expect = [&](rmw_qos_compatibility_type_t expected) {
    rmw_qos_compatibility_type_t result;
    require(
      rmw_qos_profile_check_compatible(p, s, &result, nullptr, 0) == RMW_RET_OK &&
        result == expected,
      "offered/requested lease comparison");
  };
  p.liveliness_lease_duration = s.liveliness_lease_duration = {1, 0};
  expect(RMW_QOS_COMPATIBILITY_OK);
  s.liveliness_lease_duration = {2, 0};
  expect(RMW_QOS_COMPATIBILITY_OK);
  std::swap(p, s);
  expect(RMW_QOS_COMPATIBILITY_ERROR);

  auto sub = node.create_subscription<Msg>(
    "/history", rclcpp::QoS(rclcpp::KeepAll()), [](Msg::ConstSharedPtr) {});
  require(sub->get_actual_qos().history() == rclcpp::HistoryPolicy::KeepAll, "history policy");
  auto pub = node.create_publisher<Msg>("/history", 4);
  auto handle = rcl_subscription_get_rmw_handle(sub->get_subscription_handle().get());
  rmw_event_t event = rmw_get_zero_initialized_event();
  require(
    rmw_subscription_event_init(&event, handle, RMW_EVENT_MESSAGE_LOST) == RMW_RET_OK,
    "loss event");
  std::atomic<size_t> admitted{}, rejected{};
  auto count = [](const void * ptr, size_t n) {
    static_cast<std::atomic<size_t> *>(const_cast<void *>(ptr))->fetch_add(n);
  };
  require(
    rmw_subscription_set_on_new_message_callback(handle, count, &admitted) == RMW_RET_OK,
    "arrival callback");
  require(rmw_event_set_callback(&event, count, &rejected) == RMW_RET_OK, "loss callback");
  auto payload = [](size_t i) { return std::to_string(i) + std::string(4096, 'x'); };
  // Pace the native link independently; the application takes nothing during the burst.
  for (size_t i = 0; i < 272; ++i) {
    msg.data = payload(i);
    pub->publish(msg);
    wait_until([&] { return admitted.load() + rejected.load() == i + 1; }, "burst delivery");
  }
  require(admitted == 256 && rejected == 16, "bounded reader admission");
  bool taken{};
  rmw_message_lost_status_t status{};
  require(
    rmw_take_event(&event, &status, &taken) == RMW_RET_OK && taken && status.total_count == 16 &&
      status.total_count_change == 16,
    "loss count");
  for (size_t i = 0; i < 256; ++i)
    require(sub->take(msg, info) && msg.data == payload(i), "oldest unread sample was lost");
  require(!sub->take(msg, info), "unexpected additional sample");
  msg.data = "after drain";
  pub->publish(msg);
  wait_until([&] { return sub->take(msg, info); }, "reader did not recover");
  require(msg.data == "after drain", "recovery payload");
  require(
    rmw_event_set_callback(&event, nullptr, nullptr) == RMW_RET_OK &&
      rmw_subscription_set_on_new_message_callback(handle, nullptr, nullptr) == RMW_RET_OK &&
      rmw_event_fini(&event) == RMW_RET_OK,
    "callback cleanup");
  std::cout << "raw topic isolated; QoS direction verified; 256 retained, 16 reported lost; reader "
               "recovered\n";
}

static void services(std::shared_ptr<rclcpp::Node> node, const std::string & role)
{
  auto qos = rclcpp::ServicesQoS()
               .deadline(2s)
               .lifespan(1s)
               .liveliness_lease_duration(300ms)
               .avoid_ros_namespace_conventions(true);
  if (role == "server") {
    auto respond = [](
                     std::shared_ptr<Service::Request> req,
                     std::shared_ptr<Service::Response> res) { res->success = req->data; };
    auto server = node->create_service<Service>("/lease", respond, qos);
    auto deadline =
      node->create_service<Service>("/only_deadline", respond, rclcpp::ServicesQoS().deadline(2s));
    auto lifespan =
      node->create_service<Service>("/only_lifespan", respond, rclcpp::ServicesQoS().lifespan(1s));
    auto lease = node->create_service<Service>(
      "/only_lease", respond, rclcpp::ServicesQoS().liveliness_lease_duration(300ms));
    rclcpp::spin(node);
    return;
  }
  auto client = node->create_client<Service>("/lease", qos);
  auto wrong = node->create_client<Service>("/lease", qos.avoid_ros_namespace_conventions(false));
  wait_until([&] { return client->service_is_ready(); }, "finite service missing");
  std::this_thread::sleep_for(950ms);
  require(
    client->service_is_ready() && !wrong->service_is_ready(), "idle lease or raw name isolation");
  auto call = [&](const auto & endpoint) {
    wait_until([&] { return endpoint->service_is_ready(); }, "service unavailable");
    auto req = std::make_shared<Service::Request>();
    req->data = true;
    auto response = endpoint->async_send_request(req);
    require(
      rclcpp::spin_until_future_complete(node, response, 2s) == rclcpp::FutureReturnCode::SUCCESS &&
        response.get()->success,
      "service round trip");
  };
  call(client);
  call(node->create_client<Service>("/only_deadline", rclcpp::ServicesQoS().deadline(2s)));
  call(node->create_client<Service>("/only_lifespan", rclcpp::ServicesQoS().lifespan(1s)));
  call(node->create_client<Service>(
    "/only_lease", rclcpp::ServicesQoS().liveliness_lease_duration(300ms)));
  std::ofstream("stop-ready").put('1');
  wait_until([&] { return !client->service_is_ready(); }, "stopped server retained finite lease");
  std::ofstream("expired").put('1');
  wait_until([&] { return client->service_is_ready(); }, "resumed server did not renew");
  call(client);
  std::ofstream("kill-ready").put('1');
  wait_until([&] { return !client->service_is_ready(); }, "dead server still available");
  std::cout << "raw RPC round trips; idle lease, stop/expiry, resume and process death verified\n";
}
int main(int argc, char ** argv)
{
  if (argc != 2) return 2;
  try {
    rclcpp::init(argc, argv);
    std::string role = argv[1];
    auto node = std::make_shared<rclcpp::Node>("qos_" + role);
    if (role == "topics")
      topics(*node);
    else
      services(node, role);
    rclcpp::shutdown();
  } catch (const std::exception & e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
