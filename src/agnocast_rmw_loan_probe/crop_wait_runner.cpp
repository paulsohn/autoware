#include "fixed_scope.hpp"

#include <class_loader/class_loader.hpp>
#include <rclcpp/rclcpp.hpp>
#include <rclcpp_components/node_factory.hpp>

#include <filesystem>

int main(int argc, char ** argv)
{
  if (argc < 3) return 2;
  const std::string library = argv[1], component = argv[2];
  rclcpp::init(argc, argv);
  class_loader::ClassLoader loader(library);
  auto factory = loader.createInstance<rclcpp_components::NodeFactory>(
    "rclcpp_components::NodeFactoryTemplate<" + component + ">");
  auto instance = factory->create_node_instance(rclcpp::NodeOptions{});
  auto node = instance.get_node_base_interface();
  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(node);
  // The filter creates DDS event handlers during its first cloud callback.
  auto csv = std::filesystem::path(std::getenv("BENCH_CSV"));
  while (rclcpp::ok() && (!std::filesystem::exists(csv) || !std::filesystem::file_size(csv)))
    executor.spin_once(std::chrono::milliseconds(10));
  executor.spin_some();
  {
    FixedScope scope;
    executor.spin();
  }
  executor.remove_node(node);
  rclcpp::shutdown();
}
