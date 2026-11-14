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

#include <autoware_simulation_msgs/msg/simulated_object.hpp>
#include <autoware_vehicle_msgs/msg/actuation_command_stamped.hpp>

#include <cassert>

int main()
{
  autoware_vehicle_msgs::msg::ActuationCommandStamped command;
  command.header.frame_id = "base_link";
  command.actuation_command.accel_cmd = 0.5;
  command.actuation_command.brake_cmd = 0.1;
  command.actuation_command.steer_cmd = 0.05;

  autoware_simulation_msgs::msg::SimulatedObject simulated_object;
  simulated_object.header.frame_id = "map";
  simulated_object.classification.label = simulated_object.classification.CAR;
  simulated_object.shape.type = simulated_object.shape.BOUNDING_BOX;
  simulated_object.max_velocity = 8.0F;
  simulated_object.action = simulated_object.ADD;
  simulated_object.movement_model = simulated_object.STRAIGHT_LINE;

  assert(command.header.frame_id == "base_link");
  assert(command.actuation_command.accel_cmd == 0.5);
  assert(command.actuation_command.brake_cmd == 0.1);
  assert(command.actuation_command.steer_cmd == 0.05);
  assert(simulated_object.header.frame_id == "map");
  assert(simulated_object.classification.label == 1);
  assert(simulated_object.shape.type == 0);
  assert(simulated_object.max_velocity == 8.0F);
  assert(simulated_object.action == 0);
  assert(simulated_object.movement_model == 0);
  return 0;
}
