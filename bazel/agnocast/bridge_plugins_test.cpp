// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0

#include "agnocast/agnocast.hpp"
#include "agnocast/agnocast_single_threaded_executor.hpp"
#include "agnocast/bridge/agnocast_bridge_loader.hpp"
#include "gtest/gtest.h"

#include "autoware_common_msgs/msg/response_status.hpp"
#include "ros2agnocast_discovery_msgs/msg/agnocast_daemon_state.hpp"
#include "std_msgs/msg/string.hpp"
#include "std_srvs/srv/set_bool.hpp"

#include <dlfcn.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>
#include <thread>

extern char ** environ;
using namespace std::chrono_literals;
using Status = autoware_common_msgs::msg::ResponseStatus;
using String = std_msgs::msg::String;
using SetBool = std_srvs::srv::SetBool;
using SetBoolEvent = std_srvs::srv::SetBool_Event;

namespace
{
std::string plugin_path;

class Spinner
{
  agnocast::SingleThreadedAgnocastExecutor & executor_;
  std::thread thread_;

public:
  explicit Spinner(agnocast::SingleThreadedAgnocastExecutor & executor)
  : executor_(executor), thread_([&executor] { executor.spin(); })
  {
  }
  ~Spinner()
  {
    executor_.cancel();
    thread_.join();
  }
};

class Peer
{
  pid_t pid_ = -1;

public:
  Peer(std::string transport, std::string type, std::string topic)
  {
    char executable[] = "/proc/self/exe";
    char flag[] = "--peer";
    char * args[] = {executable, flag, transport.data(), type.data(), topic.data(), nullptr};
    int error = posix_spawn(&pid_, executable, nullptr, nullptr, args, environ);
    if (error) throw std::runtime_error("posix_spawn failed: " + std::to_string(error));
  }
  ~Peer()
  {
    if (pid_ > 0) {
      kill(pid_, SIGTERM);
      waitpid(pid_, nullptr, 0);
    }
  }
};

template <class Predicate>
bool wait_for(Predicate ready)
{
  const auto deadline = std::chrono::steady_clock::now() + 15s;
  while (std::chrono::steady_clock::now() < deadline) {
    if (ready()) return true;
    std::this_thread::sleep_for(20ms);
  }
  return ready();
}

void fill(String & msg)
{
  msg.data = "Bazel bridge payload";
}
void fill(Status & msg)
{
  msg.success = true;
  msg.code = 42;
  msg.message = "Bazel bridge payload";
}
void fill(SetBoolEvent & msg)
{
  msg.info.event_type = 1;
  msg.request.resize(1);
  msg.request[0].data = true;
  msg.response.resize(1);
  msg.response[0].success = true;
  msg.response[0].message = "Bazel bridge event";
}
bool valid(const String & msg)
{
  return msg.data == "Bazel bridge payload";
}
bool valid(const Status & msg)
{
  return msg.success && msg.code == 42 && msg.message == "Bazel bridge payload";
}
bool valid(const SetBoolEvent & msg)
{
  return msg.info.event_type == 1 && msg.request.size() == 1 && msg.request[0].data &&
         msg.response.size() == 1 && msg.response[0].success &&
         msg.response[0].message == "Bazel bridge event";
}

template <class Message>
int publish_peer(bool agno, const std::string & topic, bool automatic)
{
  auto node = std::make_shared<rclcpp::Node>("bridge_peer");
  if (agno) {
    agnocast::Publisher<Message> publisher(
      node.get(), topic, rclcpp::QoS(10), agnocast::PublisherOptions{},
      automatic ? agnocast::PublisherRole::Default : agnocast::PublisherRole::AgnocastOnly);
    for (int i = 0; i < 300 && rclcpp::ok(); ++i) {
      auto msg = publisher.borrow_loaned_message();
      fill(*msg);
      publisher.publish(std::move(msg));
      std::this_thread::sleep_for(100ms);
    }
  } else {
    auto publisher = node->create_publisher<Message>(topic, rclcpp::QoS(10));
    Message msg;
    fill(msg);
    for (int i = 0; i < 300 && rclcpp::ok(); ++i) {
      publisher->publish(msg);
      std::this_thread::sleep_for(100ms);
    }
  }
  return 0;
}

template <class Message>
void pubsub(bool r2a, const std::string & type, bool automatic = false)
{
  const auto topic = "/bridge_" + std::to_string(getpid()) + (r2a ? "_r2a_" : "_a2r_") + type;
  auto node = std::make_shared<rclcpp::Node>("bridge_test");
  agnocast::BridgeLoader loader(node->get_logger());
  auto message_type = type;
  // Package names can contain underscores; the kind is the unambiguous separator.
  auto kind = message_type.find("_msg_");
  if (kind == std::string::npos) kind = message_type.find("_srv_");
  message_type[kind] = '/';
  message_type[kind + 4] = '/';
  PubsubBridgeResult bridge;
  if (!automatic) {
    bridge = r2a ? loader.create_r2a_pubsub_bridge(node, topic, message_type, rclcpp::QoS(10))
                 : loader.create_a2r_pubsub_bridge(node, topic, message_type, rclcpp::QoS(10));
    ASSERT_TRUE(bridge.entity_handle);
    ASSERT_TRUE(bridge.callback_group);
    void * loaded = dlopen(plugin_path.c_str(), RTLD_NOW | RTLD_NOLOAD);
    ASSERT_NE(loaded, nullptr) << "BridgeLoader must load the typed plugin library";
    dlclose(loaded);
  }
  std::atomic<bool> received{false};
  typename agnocast::Subscription<Message>::SharedPtr agno_sub;
  typename rclcpp::Subscription<Message>::SharedPtr ros_sub;
  if (r2a) {
    agnocast::SubscriptionOptions options;
    options.callback_group =
      node->create_callback_group(rclcpp::CallbackGroupType::MutuallyExclusive);
    agno_sub = std::make_shared<agnocast::Subscription<Message>>(
      node.get(), topic, rclcpp::QoS(10),
      [&received](agnocast::ipc_shared_ptr<Message> msg) {
        if (valid(*msg)) received = true;
      },
      options,
      automatic ? agnocast::SubscriptionRole::Default : agnocast::SubscriptionRole::AgnocastOnly);
  } else {
    ros_sub = node->create_subscription<Message>(
      topic, rclcpp::QoS(10), [&received](typename Message::ConstSharedPtr msg) {
        if (valid(*msg)) received = true;
      });
  }
  agnocast::SingleThreadedAgnocastExecutor executor;
  executor.add_node(node);
  if (!automatic)
    executor.add_callback_group(bridge.callback_group, node->get_node_base_interface());
  Spinner spinner(executor);
  Peer peer(r2a ? "ros" : (automatic ? "agno_auto" : "agno"), type, topic);
  EXPECT_TRUE(wait_for([&] { return received.load(); })) << type;
}

TEST(BridgePlugins, StringRosToAgnocast)
{
  pubsub<String>(true, "std_msgs_msg_String");
}
TEST(BridgePlugins, StringAgnocastToRos)
{
  pubsub<String>(false, "std_msgs_msg_String");
}
TEST(BridgePlugins, LocalRosToAgnocast)
{
  pubsub<Status>(true, "autoware_common_msgs_msg_ResponseStatus");
}
TEST(BridgePlugins, LocalAgnocastToRos)
{
  pubsub<Status>(false, "autoware_common_msgs_msg_ResponseStatus");
}

TEST(BridgePlugins, ServiceEventRosToAgnocast)
{
  pubsub<SetBoolEvent>(true, "std_srvs_srv_SetBool_Event");
}
TEST(BridgePlugins, ServiceEventAgnocastToRos)
{
  pubsub<SetBoolEvent>(false, "std_srvs_srv_SetBool_Event");
}

TEST(AutomaticBridge, LocalRosToAgnocast)
{
  pubsub<Status>(true, "autoware_common_msgs_msg_ResponseStatus", true);
}
TEST(AutomaticBridge, LocalAgnocastToRos)
{
  pubsub<Status>(false, "autoware_common_msgs_msg_ResponseStatus", true);
}

TEST(DiscoveryAgent, AutomaticallyPublishesTypedEndpoint)
{
  using State = ros2agnocast_discovery_msgs::msg::AgnocastDaemonState;
  const auto topic = "/discovery_" + std::to_string(getpid());
  auto node = std::make_shared<rclcpp::Node>("discovery_test");
  std::atomic<bool> observed{false};
  auto subscription = node->create_subscription<State>(
    "/_agnocast_discovery", rclcpp::QoS(1).reliable().transient_local(),
    [&](State::ConstSharedPtr state) {
      if (state->schema_version != 1 || state->host_uuid.empty()) return;
      for (const auto & entry : state->topics) {
        if (entry.topic_name != topic || entry.type_name != "std_msgs/msg/String") continue;
        for (const auto & publisher : entry.publishers) {
          if (publisher.pid == getpid() && !publisher.is_bridge) observed = true;
        }
      }
    });
  agnocast::Publisher<String> publisher(
    node.get(), topic, rclcpp::QoS(10), agnocast::PublisherOptions{});
  agnocast::SingleThreadedAgnocastExecutor executor;
  executor.add_node(node);
  Spinner spinner(executor);
  EXPECT_TRUE(wait_for([&] { return observed.load(); }));
}

void service(bool r2a)
{
  auto node = std::make_shared<rclcpp::Node>("bridge_service_test");
  const auto name = "/bridge_service_" + std::to_string(getpid()) + (r2a ? "_r2a" : "_a2r");
  auto qos = rclcpp::ServicesQoS();
  agnocast::BridgeLoader loader(node->get_logger());
  auto bridge = r2a ? loader.create_r2a_service_bridge(node, name, "std_srvs/srv/SetBool", qos)
                    : loader.create_a2r_service_bridge(node, name, "std_srvs/srv/SetBool", qos);
  ASSERT_TRUE(bridge.entity_handle);
  ASSERT_TRUE(bridge.srv_cb_group);
  ASSERT_TRUE(bridge.client_cb_group);
  agnocast::Service<SetBool>::SharedPtr agno_service;
  rclcpp::Service<SetBool>::SharedPtr ros_service;
  auto agno_group = node->create_callback_group(rclcpp::CallbackGroupType::MutuallyExclusive);
  if (r2a) {
    agno_service = std::make_shared<agnocast::Service<SetBool>>(
      node.get(), name,
      [](
        agnocast::ipc_shared_ptr<SetBool::Request> request,
        agnocast::ipc_shared_ptr<SetBool::Response> response) {
        response->success = request->data;
        response->message = "bridged response";
      },
      qos, agno_group);
  } else {
    ros_service = node->create_service<SetBool>(
      name, [](SetBool::Request::SharedPtr request, SetBool::Response::SharedPtr response) {
        response->success = request->data;
        response->message = "bridged response";
      });
  }
  auto ros_client = r2a ? node->create_client<SetBool>(name) : nullptr;
  auto agno_client =
    r2a ? nullptr : std::make_shared<agnocast::Client<SetBool>>(node.get(), name, qos, agno_group);
  agnocast::SingleThreadedAgnocastExecutor executor;
  executor.add_node(node);
  executor.add_callback_group(bridge.srv_cb_group, node->get_node_base_interface());
  executor.add_callback_group(bridge.client_cb_group, node->get_node_base_interface());
  Spinner spinner(executor);
  if (r2a) {
    ASSERT_TRUE(ros_client->wait_for_service(15s));
    auto request = std::make_shared<SetBool::Request>();
    request->data = true;
    auto future = ros_client->async_send_request(request);
    ASSERT_EQ(future.wait_for(15s), std::future_status::ready);
    auto response = future.get();
    EXPECT_TRUE(response->success);
    EXPECT_EQ(response->message, "bridged response");
  } else {
    ASSERT_TRUE(wait_for([&] { return agno_client->service_is_ready(); }));
    auto request = agno_client->borrow_loaned_request();
    request->data = true;
    auto future = agno_client->async_send_request(std::move(request));
    ASSERT_EQ(future.wait_for(15s), std::future_status::ready);
    auto response = future.get();
    EXPECT_TRUE(response->success);
    EXPECT_EQ(response->message, "bridged response");
  }
}
TEST(BridgePlugins, ServiceRosToAgnocast)
{
  service(true);
}
TEST(BridgePlugins, ServiceAgnocastToRos)
{
  service(false);
}
}  // namespace

int main(int argc, char ** argv)
{
  rclcpp::init(0, nullptr);
  agnocast::init(0, nullptr);
  int result;
  if (argc == 5 && std::string(argv[1]) == "--peer") {
    bool automatic = std::string(argv[2]) == "agno_auto";
    bool agno = automatic || std::string(argv[2]) == "agno";
    const std::string type = argv[3];
    if (type == "std_msgs_msg_String") {
      result = publish_peer<String>(agno, argv[4], automatic);
    } else if (type == "std_srvs_srv_SetBool_Event") {
      result = publish_peer<SetBoolEvent>(agno, argv[4], automatic);
    } else {
      result = publish_peer<Status>(agno, argv[4], automatic);
    }
  } else {
    if (argc < 2) return 2;
    plugin_path = std::filesystem::absolute(argv[1]);
    setenv(
      "AGNOCAST_BRIDGE_PLUGINS_PATH", std::filesystem::path(plugin_path).parent_path().c_str(), 1);
    for (int i = 1; i + 1 < argc; ++i) argv[i] = argv[i + 1];
    --argc;
    testing::InitGoogleTest(&argc, argv);
    result = RUN_ALL_TESTS();
  }
  agnocast::shutdown();
  rclcpp::shutdown();
  return result;
}
