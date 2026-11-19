#include <agnocast/agnocast_callback_isolated_executor.hpp>
#include <agnocast/bridge/agnocast_bridge_loader.hpp>

#include <fstream>

int main(int argc, char ** argv)
{
  if (argc != 2) return 2;
  rclcpp::init(argc, argv);
  auto node = std::make_shared<rclcpp::Node>("manual_path_bridge");
  agnocast::BridgeLoader loader(node->get_logger());
  const char * topic = "/trajectory_loan_probe";
  const char * type = "nav_msgs/msg/Path";
  auto qos = rclcpp::QoS(4);
  auto bridge = std::string(argv[1]) == "r2a"
                  ? loader.create_r2a_pubsub_bridge(node, topic, type, qos)
                  : loader.create_a2r_pubsub_bridge(node, topic, type, qos);
  if (!bridge.entity_handle) return 1;
  agnocast::CallbackIsolatedAgnocastExecutor exec;
  exec.add_node(node);
  exec.add_callback_group(bridge.callback_group, node->get_node_base_interface());
  std::ofstream("bridge_ready") << "ready";
  exec.spin();
  rclcpp::shutdown();
}
