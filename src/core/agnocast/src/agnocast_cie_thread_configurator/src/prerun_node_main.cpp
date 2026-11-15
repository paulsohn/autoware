#include "agnocast_cie_thread_configurator/announcement_sources.hpp"
#include "agnocast_cie_thread_configurator/prerun_node.hpp"
#include "rclcpp/rclcpp.hpp"

#include <filesystem>
#include <iostream>
#include <memory>

int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);

  try {
    auto node = std::make_shared<PrerunNode>();
    agnocast_cie_thread_configurator::spin_domain_nodes(node, node->get_domain_nodes());

    node->stop();
    node->dump_yaml_config(std::filesystem::current_path());
  } catch (const std::exception & e) {
    std::cerr << "[ERROR] " << e.what() << std::endl;
    rclcpp::shutdown();
    return 1;
  }

  rclcpp::shutdown();
  return 0;
}
