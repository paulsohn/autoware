#include "agnocast_cie_thread_configurator/announcement_sources.hpp"

#include "agnocast_cie_thread_configurator/cie_thread_configurator.hpp"

#include <chrono>
#include <cstddef>
#include <set>
#include <thread>
#include <utility>
#include <vector>

namespace agnocast_cie_thread_configurator
{

void spin_domain_nodes(
  const rclcpp::Node::SharedPtr & node, const std::vector<rclcpp::Node::SharedPtr> & domain_nodes)
{
  if (domain_nodes.empty()) {
    rclcpp::spin(node);
    return;
  }

  std::vector<rclcpp::executors::SingleThreadedExecutor::SharedPtr> executors;
  auto nodes = domain_nodes;
  nodes.push_back(node);
  for (const auto & domain_node : nodes) {
    rclcpp::ExecutorOptions options;
    options.context = domain_node->get_node_base_interface()->get_context();
    auto executor = std::make_shared<rclcpp::executors::SingleThreadedExecutor>(options);
    executor->add_node(domain_node);
    executors.push_back(std::move(executor));
  }
  // These daemon callbacks share state; separate executor threads would race.
  while (rclcpp::ok(node->get_node_base_interface()->get_context())) {
    for (const auto & executor : executors) {
      executor->spin_some();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

AnnouncementSources::AnnouncementSources(
  rclcpp::Node & node, size_t default_domain_id, const std::set<size_t> & domain_ids,
  CallbackGroupCallback on_callback_group, NonRosThreadInfoListener::Callback on_non_ros_thread)
: non_ros_thread_listener_(std::move(on_non_ros_thread), node.get_logger())
{
  // Must stay compatible with the client publishers in agnocastlib
  // (cie_client_utils.cpp). reliable + transient_local so announcements made
  // before the daemon starts are still delivered.
  const auto qos = rclcpp::QoS(rclcpp::KeepAll()).reliable().transient_local();
  const auto subscribe = [&](rclcpp::Node & target, size_t domain_id) {
    return target.create_subscription<CallbackGroupInfo>(
      k_callback_group_info_topic, qos,
      [on_callback_group, domain_id](CallbackGroupInfo::SharedPtr msg) {
        on_callback_group(domain_id, std::move(msg));
      });
  };

  subscriptions_.push_back(subscribe(node, default_domain_id));

  for (const size_t domain_id : domain_ids) {
    if (domain_id == default_domain_id) {
      continue;
    }
    domain_nodes_.push_back(create_node_for_domain(domain_id));
    subscriptions_.push_back(subscribe(*domain_nodes_.back(), domain_id));
    RCLCPP_INFO(node.get_logger(), "Created subscription for domain ID: %zu", domain_id);
  }
}

}  // namespace agnocast_cie_thread_configurator
