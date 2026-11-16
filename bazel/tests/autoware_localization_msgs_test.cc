// Copyright 2026 Taeseung Sohn
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <autoware_localization_msgs/msg/kinematic_state.hpp>
#include <autoware_localization_msgs/srv/initialize_localization.hpp>

#include <cassert>
#include <string>

int main()
{
  autoware_localization_msgs::msg::KinematicState state;
  state.header.frame_id = "map";
  state.child_frame_id = "base_link";

  autoware_localization_msgs::srv::InitializeLocalization::Response response;
  response.status.success = false;
  response.status.code = response.ERROR_UNSAFE;
  response.status.message = "unsafe to initialize";

  assert(state.header.frame_id == "map");
  assert(state.child_frame_id == "base_link");
  assert(!response.status.success);
  assert(response.status.code == 1);
  assert(response.status.message == "unsafe to initialize");
  return 0;
}
