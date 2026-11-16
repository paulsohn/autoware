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

#include <autoware_control_msgs/msg/control_horizon.hpp>
#include <autoware_v2x_msgs/msg/virtual_gate_command.hpp>

#include <cassert>
#include <string>

int main()
{
  autoware_control_msgs::msg::Control control;
  control.lateral.steering_tire_angle = 0.25F;
  control.longitudinal.velocity = 3.5F;

  autoware_control_msgs::msg::ControlHorizon horizon;
  horizon.time_step_ms = 100.0F;
  horizon.controls.push_back(control);

  autoware_v2x_msgs::msg::VirtualGateAreaCommand area;
  area.command = area.ACQUIRE;
  area.sequence_id = 7;
  area.area_id = "loading_zone";
  area.gate_ids.push_back("north_gate");
  area.expected_time_arrival.emplace_back();
  area.expected_time_arrival.front().sec = 42;

  autoware_v2x_msgs::msg::VirtualGateCommand command;
  command.areas.push_back(area);

  assert(horizon.controls.size() == 1);
  assert(horizon.controls.front().lateral.steering_tire_angle == 0.25F);
  assert(horizon.controls.front().longitudinal.velocity == 3.5F);
  assert(command.areas.size() == 1);
  assert(command.areas.front().command == 1);
  assert(command.areas.front().area_id == "loading_zone");
  assert(command.areas.front().expected_time_arrival.size() == 1);
  assert(command.areas.front().expected_time_arrival.front().sec == 42);
  return 0;
}
