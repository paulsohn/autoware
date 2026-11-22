// Component-manager fixture for explicitly bounded, stable-topology wait scopes.
#include "fixed_scope.hpp"
#include <rclcpp_components/component_manager.hpp>

class LifecycleManager : public rclcpp_components::ComponentManager
{
public:
  using ComponentManager::ComponentManager;
  void disarm() { scope_.reset(); }

protected:
  void add_node_to_executor(uint64_t id) override
  {
    disarm();
    ComponentManager::add_node_to_executor(id);
  }
  void remove_node_from_executor(uint64_t id) override
  {
    disarm();
    ComponentManager::remove_node_from_executor(id);
  }
  void on_list_nodes(
    const std::shared_ptr<rmw_request_id_t> header, const std::shared_ptr<ListNodes::Request> request,
    std::shared_ptr<ListNodes::Response> response) override
  {
    ComponentManager::on_list_nodes(header, request, response);
    // The driver lists one warmed crop per generation to start its measured window.
    if (!node_wrappers_.empty() && !scope_) scope_ = std::make_unique<FixedScope>();
  }

private:
  std::unique_ptr<FixedScope> scope_;
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  auto exec = std::make_shared<rclcpp::executors::SingleThreadedExecutor>();
  auto manager = std::make_shared<LifecycleManager>(exec, "lifecycle");
  exec->add_node(manager);
  exec->spin();
  manager->disarm();
  exec->remove_node(manager);
  rclcpp::shutdown();
}
