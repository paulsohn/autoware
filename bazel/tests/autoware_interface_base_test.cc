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

#include <autoware_map_msgs/msg/map_projector_info.hpp>
#include <autoware_perception_msgs/msg/predicted_objects.hpp>
#include <autoware_planning_msgs/msg/trajectory.hpp>
#include <autoware_sensing_msgs/msg/radar_objects.hpp>
#include <autoware_system_msgs/srv/change_operation_mode.hpp>

#include <cassert>

int main()
{
  autoware_system_msgs::srv::ChangeOperationMode::Request mode_request;
  mode_request.mode = mode_request.AUTONOMOUS;

  autoware_sensing_msgs::msg::RadarObjects radar_objects;
  radar_objects.objects.emplace_back();
  radar_objects.objects.front().object_id = 17;
  radar_objects.objects.front().measurement_status =
    radar_objects.objects.front().MEASUREMENT_STATUS_MEASURED;

  autoware_map_msgs::msg::MapProjectorInfo projector;
  projector.projector_type = projector.LOCAL_CARTESIAN;
  projector.map_origin.latitude = 35.681236;
  projector.map_origin.longitude = 139.767125;

  autoware_planning_msgs::msg::Trajectory trajectory;
  trajectory.points.emplace_back();
  trajectory.points.front().longitudinal_velocity_mps = 5.0F;

  autoware_perception_msgs::msg::PredictedObjects predicted_objects;
  predicted_objects.objects.emplace_back();
  predicted_objects.objects.front().existence_probability = 0.75F;

  assert(mode_request.mode == 2);
  assert(radar_objects.objects.front().object_id == 17);
  assert(radar_objects.objects.front().measurement_status == 1);
  assert(projector.projector_type == "LocalCartesian");
  assert(projector.map_origin.latitude == 35.681236);
  assert(trajectory.points.front().longitudinal_velocity_mps == 5.0F);
  assert(predicted_objects.objects.front().existence_probability == 0.75F);
  return 0;
}
