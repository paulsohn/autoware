// Stage-2 wrapper nodes share one executor, as the ROS crop components do.
#include <agnocast/node/agnocast_only_single_threaded_executor.hpp>
#include <autoware/agnocast_wrapper/node.hpp>
#include <class_loader/class_loader.hpp>
#include <rclcpp_components/node_factory.hpp>

#include <iostream>

int main(int argc, char ** argv)
{
  namespace wrapper = autoware::agnocast_wrapper;
  wrapper::init(1, argv, true);
  {
    class_loader::ClassLoader loader("libuniverse_crop.so");
    auto factory = loader.createInstance<rclcpp_components::NodeFactory>(
      "rclcpp_components::NodeFactoryTemplate<autoware::pointcloud_preprocessor::"
      "CropBoxFilterComponent>");
    std::vector<std::shared_ptr<wrapper::Node>> nodes;
    agnocast::AgnocastOnlySingleThreadedExecutor executor;
    for (int i = 1; i < argc;) {
      if (std::string(argv[i++]) != "--crop") throw std::runtime_error("expected crop options");
      std::vector<std::string> args;
      while (i < argc && std::string(argv[i]) != "--crop") args.emplace_back(argv[i++]);
      auto instance = factory->create_node_instance(rclcpp::NodeOptions{}.arguments(args));
      auto crop = std::static_pointer_cast<wrapper::Node>(instance.get_node_instance());
      executor.add_node(crop->get_agnocast_node());
      std::cout << "native_crop " << crop->get_fully_qualified_name() << std::endl;
      nodes.push_back(std::move(crop));
    }
    if (nodes.size() != 3) throw std::runtime_error("expected three crops");
    executor.spin();
  }
  wrapper::shutdown();
}
