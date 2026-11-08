// Checks that in-repo interface packages (local modules) generate usable C++ types.
#include <cstdio>
#include <string>

#include "autoware_localization_msgs/msg/kinematic_state.hpp"
#include "autoware_localization_msgs/srv/initialize_localization.hpp"

int main()
{
  using autoware_localization_msgs::msg::KinematicState;
  using autoware_localization_msgs::srv::InitializeLocalization;
  const std::string msg = rosidl_generator_traits::name<KinematicState>();
  const std::string srv = rosidl_generator_traits::name<InitializeLocalization>();
  InitializeLocalization::Response res;
  res.status.success = true;  // field of the cross-package autoware_common_msgs/ResponseStatus
  std::printf("%s\n%s\n", msg.c_str(), srv.c_str());
  return msg == "autoware_localization_msgs/msg/KinematicState" &&
         srv == "autoware_localization_msgs/srv/InitializeLocalization" && res.status.success
           ? 0 : 1;
}
