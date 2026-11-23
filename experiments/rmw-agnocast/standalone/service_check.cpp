#include <rclcpp/rclcpp.hpp>

#include <std_srvs/srv/set_bool.hpp>

#include <iostream>
using namespace std::chrono_literals;
int main(int argc, char ** argv)
{
  if (argc != 3) return 2;
  rclcpp::init(argc, argv);
  std::string role = argv[1];
  auto node = std::make_shared<rclcpp::Node>("api_" + role);
  using Service = std_srvs::srv::SetBool;
  if (role == "server") {
    auto server = node->create_service<Service>(
      argv[2],
      [](std::shared_ptr<Service::Request> request, std::shared_ptr<Service::Response> response) {
        response->success = request->data;
        response->message = request->data ? "true client" : "false client";
      });
    rclcpp::spin(node);
  } else {
    auto client = node->create_client<Service>(argv[2]);
    auto until = std::chrono::steady_clock::now() + 5s;
    while (!client->service_is_ready() && std::chrono::steady_clock::now() < until)
      std::this_thread::sleep_for(20ms);
    if (!client->service_is_ready()) return 3;
    if (
      node->count_services(argv[2]) != 1 || node->count_clients(argv[2]) < 1 ||
      node->get_servers_info_by_service(argv[2]).size() != 1 ||
      node->get_clients_info_by_service(argv[2]).empty() ||
      node->get_service_names_and_types_by_node("api_server", "/").at(argv[2]).at(0) !=
        "std_srvs/srv/SetBool")
      return 6;
    auto req = std::make_shared<Service::Request>();
    req->data = role == "first";
    auto future = client->async_send_request(req);
    if (rclcpp::spin_until_future_complete(node, future, 5s) != rclcpp::FutureReturnCode::SUCCESS)
      return 4;
    auto res = future.get();
    std::cout << role << " received " << res->message << std::endl;
    if (res->success != req->data || res->message != (req->data ? "true client" : "false client"))
      return 5;
  }
  node.reset();
  rclcpp::shutdown();
  return 0;
}
