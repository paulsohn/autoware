# Autoware Universe coverage ledger

Snapshot: 2026-11-23, source inventory at bazel commit 3f34693a. Scope is every
ROS package under src/universe/autoware_universe plus the dependencies needed by
its actual consumers. Other imported repositories supply that required closure;
this is not a claim to adopt every package in those repositories at once.
Existing system-designer exclusions remain outside this Universe package census.

There are **244 packages**: **107 have both package-local MODULE.bazel and
BUILD.bazel**, and all 107 have a matching root local_path_override; **137 still
need package-local definitions**. Of the 244 manifests, 243 use ament_cmake and
one uses ament_python. Declaration presence is an inventory fact, not proof of
complete library/plugin/test/resource or runtime coverage. Existing journal
results remain reusable evidence; Phase 9.20 reconciles that evidence with each
package's full contract without automatically rerunning unchanged gates.

PLAN.md owns [sequence and acceptance](PLAN.md#phase-9-broader-universe-coverage-in-progress).
This ledger owns exact package assignment. Each current package appears in one
package entry below: 107 existing declarations and 137 pending packages. A phase
may contain multiple package commits and smaller execution batches. Update the
ledger when a package closes or the source snapshot changes; this is a planning
record, not a new build-time completeness validator.

Current adoption progress, 2026-11-27: **176/244** packages have both definitions
and root local overrides; **68** remain without definitions. The census table
below preserves the initial 2026-11-23 baseline. Checked ledger items record
completed module gates; Phase 9.8 is complete. See PLAN.md and the
package journals for configuration and behavior evidence.

## Census by source category

| Category | Packages | Existing MODULE/BUILD pair | Pending definitions |
| --- | ---: | ---: | ---: |
| common | 15 | 7 | 8 |
| control | 21 | 13 | 8 |
| e2e | 1 | 0 | 1 |
| evaluator | 7 | 3 | 4 |
| examples | 1 | 0 | 1 |
| localization | 14 | 0 | 14 |
| map | 1 | 1 | 0 |
| perception | 51 | 7 | 44 |
| planning | 68 | 47 | 21 |
| sensing | 11 | 6 | 5 |
| simulator | 8 | 4 | 4 |
| system | 30 | 12 | 18 |
| vehicle | 4 | 2 | 2 |
| visualization | 12 | 5 | 7 |
| **Total** | **244** | **107** | **137** |

## Existing declarations: reuse evidence and audit the full contract in 9.20

The five recent radar packages have the complete Phase 9.1 gate recorded in
PLAN/JOURNAL. Other rows point to prior adoption work; match their supported
configuration, test inventory, public plugins/entrypoints and resources to that
evidence during 9.20. Do not mark a partial resource export, disabled optional
branch or missing test group as full package acceptance.

| Package | Source category | Coverage record |
| --- | --- | --- |
| [autoware_component_interface_specs_universe](../../src/universe/autoware_universe/common/autoware_component_interface_specs_universe/package.xml) | common | Existing declarations; map prior evidence in 9.20 |
| [autoware_deprecated_boundary_departure_checker](../../src/universe/autoware_universe/common/autoware_deprecated_boundary_departure_checker/package.xml) | common | Existing declarations; map prior evidence in 9.20 |
| [autoware_fake_test_node](../../src/universe/autoware_universe/common/autoware_fake_test_node/package.xml) | common | Existing declarations; map prior evidence in 9.20 |
| [autoware_grid_map_utils](../../src/universe/autoware_universe/common/autoware_grid_map_utils/package.xml) | common | Existing declarations; map prior evidence in 9.20 |
| [autoware_traffic_light_utils](../../src/universe/autoware_universe/common/autoware_traffic_light_utils/package.xml) | common | Existing declarations; map prior evidence in 9.20 |
| [autoware_universe_utils](../../src/universe/autoware_universe/common/autoware_universe_utils/package.xml) | common | Existing declarations; map prior evidence in 9.20 |
| [tier4_api_utils](../../src/universe/autoware_universe/common/tier4_api_utils/package.xml) | common | Existing declarations; map prior evidence in 9.20 |
| [autoware_autonomous_emergency_braking](../../src/universe/autoware_universe/control/autoware_autonomous_emergency_braking/package.xml) | control | Existing declarations; map prior evidence in 9.20 |
| [autoware_collision_detector](../../src/universe/autoware_universe/control/autoware_collision_detector/package.xml) | control | Existing declarations; map prior evidence in 9.20 |
| [autoware_control_validator](../../src/universe/autoware_universe/control/autoware_control_validator/package.xml) | control | Existing declarations; map prior evidence in 9.20 |
| [autoware_external_cmd_selector](../../src/universe/autoware_universe/control/autoware_external_cmd_selector/package.xml) | control | Existing declarations; map prior evidence in 9.20 |
| [autoware_lane_departure_checker](../../src/universe/autoware_universe/control/autoware_lane_departure_checker/package.xml) | control | Existing declarations; map prior evidence in 9.20 |
| [autoware_mpc_lateral_controller](../../src/universe/autoware_universe/control/autoware_mpc_lateral_controller/package.xml) | control | Existing declarations; map prior evidence in 9.20 |
| [autoware_operation_mode_transition_manager](../../src/universe/autoware_universe/control/autoware_operation_mode_transition_manager/package.xml) | control | Existing declarations; map prior evidence in 9.20 |
| [autoware_pid_longitudinal_controller](../../src/universe/autoware_universe/control/autoware_pid_longitudinal_controller/package.xml) | control | Existing declarations; map prior evidence in 9.20 |
| [autoware_pure_pursuit](../../src/universe/autoware_universe/control/autoware_pure_pursuit/package.xml) | control | Existing declarations; map prior evidence in 9.20 |
| [autoware_shift_decider](../../src/universe/autoware_universe/control/autoware_shift_decider/package.xml) | control | Existing declarations; map prior evidence in 9.20 |
| [autoware_trajectory_follower_base](../../src/universe/autoware_universe/control/autoware_trajectory_follower_base/package.xml) | control | Existing declarations; map prior evidence in 9.20 |
| [autoware_trajectory_follower_node](../../src/universe/autoware_universe/control/autoware_trajectory_follower_node/package.xml) | control | Existing declarations; map prior evidence in 9.20 |
| [autoware_vehicle_cmd_gate](../../src/universe/autoware_universe/control/autoware_vehicle_cmd_gate/package.xml) | control | Existing declarations; map prior evidence in 9.20 |
| [autoware_control_evaluator](../../src/universe/autoware_universe/evaluator/autoware_control_evaluator/package.xml) | evaluator | Existing declarations; map prior evidence in 9.20 |
| [autoware_perception_online_evaluator](../../src/universe/autoware_universe/evaluator/autoware_perception_online_evaluator/package.xml) | evaluator | Existing declarations; map prior evidence in 9.20 |
| [autoware_planning_evaluator](../../src/universe/autoware_universe/evaluator/autoware_planning_evaluator/package.xml) | evaluator | Existing declarations; map prior evidence in 9.20 |
| [autoware_map_tf_generator](../../src/universe/autoware_universe/map/autoware_map_tf_generator/package.xml) | map | Existing declarations; map prior evidence in 9.20 |
| [autoware_map_based_prediction](../../src/universe/autoware_universe/perception/autoware_map_based_prediction/package.xml) | perception | Existing declarations; map prior evidence in 9.20 |
| [autoware_multi_object_tracker](../../src/universe/autoware_universe/perception/autoware_multi_object_tracker/package.xml) | perception | Existing declarations; map prior evidence in 9.20 |
| [autoware_probabilistic_occupancy_grid_map](../../src/universe/autoware_universe/perception/autoware_probabilistic_occupancy_grid_map/package.xml) | perception | Existing declarations; map prior evidence in 9.20 |
| [autoware_tensorrt_classifier](../../src/universe/autoware_universe/perception/autoware_tensorrt_classifier/package.xml) | perception | Existing declarations; map prior evidence in 9.20 |
| [autoware_tensorrt_plugins](../../src/universe/autoware_universe/perception/autoware_tensorrt_plugins/package.xml) | perception | Existing declarations; map prior evidence in 9.20 |
| [autoware_tensorrt_yolox](../../src/universe/autoware_universe/perception/autoware_tensorrt_yolox/package.xml) | perception | Existing declarations; map prior evidence in 9.20 |
| [perception_utils](../../src/universe/autoware_universe/perception/perception_utils/package.xml) | perception | Existing declarations; map prior evidence in 9.20 |
| [autoware_behavior_path_avoidance_by_lane_change_module](../../src/universe/autoware_universe/planning/behavior_path_planner/autoware_behavior_path_avoidance_by_lane_change_module/package.xml) | planning | Existing declarations; map prior evidence in 9.20 |
| [autoware_behavior_path_bidirectional_traffic_module](../../src/universe/autoware_universe/planning/behavior_path_planner/autoware_behavior_path_bidirectional_traffic_module/package.xml) | planning | Existing declarations; map prior evidence in 9.20 |
| [autoware_behavior_path_goal_planner_module](../../src/universe/autoware_universe/planning/behavior_path_planner/autoware_behavior_path_goal_planner_module/package.xml) | planning | Existing declarations; map prior evidence in 9.20 |
| [autoware_behavior_path_lane_change_module](../../src/universe/autoware_universe/planning/behavior_path_planner/autoware_behavior_path_lane_change_module/package.xml) | planning | Existing declarations; map prior evidence in 9.20 |
| [autoware_behavior_path_planner](../../src/universe/autoware_universe/planning/behavior_path_planner/autoware_behavior_path_planner/package.xml) | planning | Existing declarations; map prior evidence in 9.20 |
| [autoware_behavior_path_planner_common](../../src/universe/autoware_universe/planning/behavior_path_planner/autoware_behavior_path_planner_common/package.xml) | planning | Existing declarations; map prior evidence in 9.20 |
| [autoware_behavior_path_side_shift_module](../../src/universe/autoware_universe/planning/behavior_path_planner/autoware_behavior_path_side_shift_module/package.xml) | planning | Existing declarations; map prior evidence in 9.20 |
| [autoware_behavior_path_start_planner_module](../../src/universe/autoware_universe/planning/behavior_path_planner/autoware_behavior_path_start_planner_module/package.xml) | planning | Existing declarations; map prior evidence in 9.20 |
| [autoware_behavior_path_static_obstacle_avoidance_module](../../src/universe/autoware_universe/planning/behavior_path_planner/autoware_behavior_path_static_obstacle_avoidance_module/package.xml) | planning | Existing declarations; map prior evidence in 9.20 |
| [autoware_behavior_velocity_blind_spot_module](../../src/universe/autoware_universe/planning/behavior_velocity_planner/autoware_behavior_velocity_blind_spot_module/package.xml) | planning | Existing declarations; map prior evidence in 9.20 |
| [autoware_behavior_velocity_crosswalk_module](../../src/universe/autoware_universe/planning/behavior_velocity_planner/autoware_behavior_velocity_crosswalk_module/package.xml) | planning | Existing declarations; map prior evidence in 9.20 |
| [autoware_behavior_velocity_detection_area_module](../../src/universe/autoware_universe/planning/behavior_velocity_planner/autoware_behavior_velocity_detection_area_module/package.xml) | planning | Existing declarations; map prior evidence in 9.20 |
| [autoware_behavior_velocity_intersection_module](../../src/universe/autoware_universe/planning/behavior_velocity_planner/autoware_behavior_velocity_intersection_module/package.xml) | planning | Existing declarations; map prior evidence in 9.20 |
| [autoware_behavior_velocity_no_stopping_area_module](../../src/universe/autoware_universe/planning/behavior_velocity_planner/autoware_behavior_velocity_no_stopping_area_module/package.xml) | planning | Existing declarations; map prior evidence in 9.20 |
| [autoware_behavior_velocity_rtc_interface](../../src/universe/autoware_universe/planning/behavior_velocity_planner/autoware_behavior_velocity_rtc_interface/package.xml) | planning | Existing declarations; map prior evidence in 9.20 |
| [autoware_behavior_velocity_traffic_light_module](../../src/universe/autoware_universe/planning/behavior_velocity_planner/autoware_behavior_velocity_traffic_light_module/package.xml) | planning | Existing declarations; map prior evidence in 9.20 |
| [autoware_behavior_velocity_virtual_traffic_light_module](../../src/universe/autoware_universe/planning/behavior_velocity_planner/autoware_behavior_velocity_virtual_traffic_light_module/package.xml) | planning | Existing declarations; map prior evidence in 9.20 |
| [autoware_behavior_velocity_walkway_module](../../src/universe/autoware_universe/planning/behavior_velocity_planner/autoware_behavior_velocity_walkway_module/package.xml) | planning | Existing declarations; map prior evidence in 9.20 |
| [autoware_bezier_sampler](../../src/universe/autoware_universe/planning/sampling_based_planner/autoware_bezier_sampler/package.xml) | planning | Existing declarations; map prior evidence in 9.20 |
| [autoware_costmap_generator](../../src/universe/autoware_universe/planning/autoware_costmap_generator/package.xml) | planning | Existing declarations; map prior evidence in 9.20 |
| [autoware_external_velocity_limit_selector](../../src/universe/autoware_universe/planning/autoware_external_velocity_limit_selector/package.xml) | planning | Existing declarations; map prior evidence in 9.20 |
| [autoware_freespace_planner](../../src/universe/autoware_universe/planning/autoware_freespace_planner/package.xml) | planning | Existing declarations; map prior evidence in 9.20 |
| [autoware_freespace_planning_algorithms](../../src/universe/autoware_universe/planning/autoware_freespace_planning_algorithms/package.xml) | planning | Existing declarations; map prior evidence in 9.20 |
| [autoware_frenet_planner](../../src/universe/autoware_universe/planning/sampling_based_planner/autoware_frenet_planner/package.xml) | planning | Existing declarations; map prior evidence in 9.20 |
| [autoware_hazard_lights_selector](../../src/universe/autoware_universe/planning/autoware_hazard_lights_selector/package.xml) | planning | Existing declarations; map prior evidence in 9.20 |
| [autoware_manual_lane_change_handler](../../src/universe/autoware_universe/planning/autoware_manual_lane_change_handler/package.xml) | planning | Existing declarations; map prior evidence in 9.20 |
| [autoware_mission_planner_universe](../../src/universe/autoware_universe/planning/autoware_mission_planner_universe/package.xml) | planning | Existing declarations; map prior evidence in 9.20 |
| [autoware_motion_velocity_boundary_departure_prevention_module](../../src/universe/autoware_universe/planning/motion_velocity_planner/autoware_motion_velocity_boundary_departure_prevention_module/package.xml) | planning | Existing declarations; map prior evidence in 9.20 |
| [autoware_motion_velocity_dynamic_obstacle_stop_module](../../src/universe/autoware_universe/planning/motion_velocity_planner/autoware_motion_velocity_dynamic_obstacle_stop_module/package.xml) | planning | Existing declarations; map prior evidence in 9.20 |
| [autoware_motion_velocity_obstacle_cruise_module](../../src/universe/autoware_universe/planning/motion_velocity_planner/autoware_motion_velocity_obstacle_cruise_module/package.xml) | planning | Existing declarations; map prior evidence in 9.20 |
| [autoware_motion_velocity_obstacle_slow_down_module](../../src/universe/autoware_universe/planning/motion_velocity_planner/autoware_motion_velocity_obstacle_slow_down_module/package.xml) | planning | Existing declarations; map prior evidence in 9.20 |
| [autoware_motion_velocity_obstacle_velocity_limiter_module](../../src/universe/autoware_universe/planning/motion_velocity_planner/autoware_motion_velocity_obstacle_velocity_limiter_module/package.xml) | planning | Existing declarations; map prior evidence in 9.20 |
| [autoware_motion_velocity_out_of_lane_module](../../src/universe/autoware_universe/planning/motion_velocity_planner/autoware_motion_velocity_out_of_lane_module/package.xml) | planning | Existing declarations; map prior evidence in 9.20 |
| [autoware_motion_velocity_road_user_stop_module](../../src/universe/autoware_universe/planning/motion_velocity_planner/autoware_motion_velocity_road_user_stop_module/package.xml) | planning | Existing declarations; map prior evidence in 9.20 |
| [autoware_motion_velocity_run_out_module](../../src/universe/autoware_universe/planning/motion_velocity_planner/autoware_motion_velocity_run_out_module/package.xml) | planning | Existing declarations; map prior evidence in 9.20 |
| [autoware_path_optimizer](../../src/universe/autoware_universe/planning/autoware_path_optimizer/package.xml) | planning | Existing declarations; map prior evidence in 9.20 |
| [autoware_path_smoother](../../src/universe/autoware_universe/planning/autoware_path_smoother/package.xml) | planning | Existing declarations; map prior evidence in 9.20 |
| [autoware_planning_validator](../../src/universe/autoware_universe/planning/planning_validator/autoware_planning_validator/package.xml) | planning | Existing declarations; map prior evidence in 9.20 |
| [autoware_planning_validator_intersection_collision_checker](../../src/universe/autoware_universe/planning/planning_validator/autoware_planning_validator_intersection_collision_checker/package.xml) | planning | Existing declarations; map prior evidence in 9.20 |
| [autoware_planning_validator_latency_checker](../../src/universe/autoware_universe/planning/planning_validator/autoware_planning_validator_latency_checker/package.xml) | planning | Existing declarations; map prior evidence in 9.20 |
| [autoware_planning_validator_rear_collision_checker](../../src/universe/autoware_universe/planning/planning_validator/autoware_planning_validator_rear_collision_checker/package.xml) | planning | Existing declarations; map prior evidence in 9.20 |
| [autoware_planning_validator_test_utils](../../src/universe/autoware_universe/planning/planning_validator/autoware_planning_validator_test_utils/package.xml) | planning | Existing declarations; map prior evidence in 9.20 |
| [autoware_planning_validator_trajectory_checker](../../src/universe/autoware_universe/planning/planning_validator/autoware_planning_validator_trajectory_checker/package.xml) | planning | Existing declarations; map prior evidence in 9.20 |
| [autoware_remaining_distance_time_calculator](../../src/universe/autoware_universe/planning/autoware_remaining_distance_time_calculator/package.xml) | planning | Existing declarations; map prior evidence in 9.20 |
| [autoware_rtc_interface](../../src/universe/autoware_universe/planning/autoware_rtc_interface/package.xml) | planning | Existing declarations; map prior evidence in 9.20 |
| [autoware_sampler_common](../../src/universe/autoware_universe/planning/sampling_based_planner/autoware_sampler_common/package.xml) | planning | Existing declarations; map prior evidence in 9.20 |
| [autoware_scenario_selector](../../src/universe/autoware_universe/planning/autoware_scenario_selector/package.xml) | planning | Existing declarations; map prior evidence in 9.20 |
| [autoware_image_transport_decompressor](../../src/universe/autoware_universe/sensing/autoware_image_transport_decompressor/package.xml) | sensing | Existing declarations; map prior evidence in 9.20 |
| [autoware_radar_objects_adapter](../../src/universe/autoware_universe/sensing/autoware_radar_objects_adapter/package.xml) | sensing | Phase 9.1 qualified; include in closure audit |
| [autoware_radar_scan_to_pointcloud2](../../src/universe/autoware_universe/sensing/autoware_radar_scan_to_pointcloud2/package.xml) | sensing | Phase 9.1 qualified; include in closure audit |
| [autoware_radar_static_pointcloud_filter](../../src/universe/autoware_universe/sensing/autoware_radar_static_pointcloud_filter/package.xml) | sensing | Phase 9.1 qualified; include in closure audit |
| [autoware_radar_threshold_filter](../../src/universe/autoware_universe/sensing/autoware_radar_threshold_filter/package.xml) | sensing | Phase 9.1 qualified; include in closure audit |
| [autoware_radar_tracks_noise_filter](../../src/universe/autoware_universe/sensing/autoware_radar_tracks_noise_filter/package.xml) | sensing | Phase 9.1 qualified; include in closure audit |
| [autoware_dummy_diag_publisher](../../src/universe/autoware_universe/simulator/autoware_dummy_diag_publisher/package.xml) | simulator | Existing declarations; map prior evidence in 9.20 |
| [autoware_dummy_traffic_light_publisher](../../src/universe/autoware_universe/simulator/autoware_dummy_traffic_light_publisher/package.xml) | simulator | Existing declarations; map prior evidence in 9.20 |
| [autoware_vehicle_door_simulator](../../src/universe/autoware_universe/simulator/autoware_vehicle_door_simulator/package.xml) | simulator | Existing declarations; map prior evidence in 9.20 |
| [tier4_dummy_object_rviz_plugin](../../src/universe/autoware_universe/simulator/tier4_dummy_object_rviz_plugin/package.xml) | simulator | Existing declarations; map prior evidence in 9.20 |
| [autoware_component_state_monitor](../../src/universe/autoware_universe/system/autoware_component_state_monitor/package.xml) | system | Existing declarations; map prior evidence in 9.20 |
| [autoware_default_adapi_universe](../../src/universe/autoware_universe/system/autoware_default_adapi_universe/package.xml) | system | Existing declarations; map prior evidence in 9.20 |
| [autoware_diagnostic_graph_aggregator](../../src/universe/autoware_universe/system/autoware_diagnostic_graph_aggregator/package.xml) | system | Existing declarations; map prior evidence in 9.20 |
| [autoware_diagnostic_graph_utils](../../src/universe/autoware_universe/system/autoware_diagnostic_graph_utils/package.xml) | system | Existing declarations; map prior evidence in 9.20 |
| [autoware_duplicated_node_checker](../../src/universe/autoware_universe/system/autoware_duplicated_node_checker/package.xml) | system | Existing declarations; map prior evidence in 9.20 |
| [autoware_hazard_status_converter](../../src/universe/autoware_universe/system/autoware_hazard_status_converter/package.xml) | system | Existing declarations; map prior evidence in 9.20 |
| [autoware_mrm_comfortable_stop_operator](../../src/universe/autoware_universe/system/autoware_mrm_comfortable_stop_operator/package.xml) | system | Existing declarations; map prior evidence in 9.20 |
| [autoware_mrm_emergency_stop_operator](../../src/universe/autoware_universe/system/autoware_mrm_emergency_stop_operator/package.xml) | system | Existing declarations; map prior evidence in 9.20 |
| [autoware_mrm_handler](../../src/universe/autoware_universe/system/autoware_mrm_handler/package.xml) | system | Existing declarations; map prior evidence in 9.20 |
| [autoware_pipeline_latency_monitor](../../src/universe/autoware_universe/system/autoware_pipeline_latency_monitor/package.xml) | system | Existing declarations; map prior evidence in 9.20 |
| [autoware_processing_time_checker](../../src/universe/autoware_universe/system/autoware_processing_time_checker/package.xml) | system | Existing declarations; map prior evidence in 9.20 |
| [autoware_topic_state_monitor](../../src/universe/autoware_universe/system/autoware_topic_state_monitor/package.xml) | system | Existing declarations; map prior evidence in 9.20 |
| [autoware_external_cmd_converter](../../src/universe/autoware_universe/vehicle/autoware_external_cmd_converter/package.xml) | vehicle | Existing declarations; map prior evidence in 9.20 |
| [autoware_raw_vehicle_cmd_converter](../../src/universe/autoware_universe/vehicle/autoware_raw_vehicle_cmd_converter/package.xml) | vehicle | Existing declarations; map prior evidence in 9.20 |
| [tier4_adapi_rviz_plugin](../../src/universe/autoware_universe/visualization/tier4_adapi_rviz_plugin/package.xml) | visualization | Existing declarations; map prior evidence in 9.20 |
| [tier4_control_mode_rviz_plugin](../../src/universe/autoware_universe/visualization/tier4_control_mode_rviz_plugin/package.xml) | visualization | Existing declarations; map prior evidence in 9.20 |
| [tier4_planning_factor_rviz_plugin](../../src/universe/autoware_universe/visualization/tier4_planning_factor_rviz_plugin/package.xml) | visualization | Existing declarations; map prior evidence in 9.20 |
| [tier4_state_rviz_plugin](../../src/universe/autoware_universe/visualization/tier4_state_rviz_plugin/package.xml) | visualization | Existing declarations; map prior evidence in 9.20 |
| [tier4_vehicle_rviz_plugin](../../src/universe/autoware_universe/visualization/tier4_vehicle_rviz_plugin/package.xml) | visualization | Existing declarations; map prior evidence in 9.20 |

Manifest audit leads already visible: autoware_probabilistic_occupancy_grid_map
names autoware_pointcloud_preprocessor (9.4), and autoware_behavior_path_planner
names autoware_path_sampler (9.11). Check the actual supported targets/optional
branches when those providers enter the graph. A manifest edge alone does not
prove a current BUILD defect or justify an unused dependency/patch.

## Pending package assignments

All following checkboxes remain open until their complete package gate passes.
Descriptions below are initial acceptance scopes, not asserted test inventories,
SDK selections or anticipated fixes. Inventory each original CMake/test/launch
contract before starting its task; a dependency provider needed earlier moves
into the current closure without renumbering existing phase identifiers.

### Phase 9.2: Radar object conversion, fusion and tracking

Build on 9.1. Preserve the tracker/merger original suites and add conversion, synchronization, frame/covariance, identity/lifecycle and merge-output ROS checks. The current CMake inventory has eight original C++ test declarations in the tracker/merger and none in the converter/fusion packages; confirm the complete inventory when starting.

- [x] [autoware_radar_fusion_to_detected_object](../../src/universe/autoware_universe/perception/autoware_radar_fusion_to_detected_object/package.xml)
- [x] [autoware_radar_object_tracker](../../src/universe/autoware_universe/perception/autoware_radar_object_tracker/package.xml)
- [x] [autoware_radar_tracks_msgs_converter](../../src/universe/autoware_universe/perception/autoware_radar_tracks_msgs_converter/package.xml)
- [x] [autoware_simple_object_merger](../../src/universe/autoware_universe/perception/autoware_simple_object_merger/package.xml)

Phase 9.2 complete, 2026-11-23: all four modules and 25 native cases pass with
Cyclone DDS/no-agnocast, zero failures/errors/skips. See PLAN and the four module
journal entries for exact gates and separate compatibility fixes. The census
above remains the initial 3f34693a snapshot; these four additions bring definition
coverage to 111/244, leaving 133 packages without definitions. Full acceptance
still includes the 9.20 contract/evidence audit.

### Phase 9.3: Shared time, logging and command-mode types

Publish the actual time/logging/type APIs and resources, with clock-boundary, logger lifecycle and compiled-consumer checks. Inspect exported effects before adding any source-less facade. Keep command-mode types below later gates/managers.

- [x] [autoware_command_mode_types](../../src/universe/autoware_universe/system/autoware_command_mode_types/package.xml)
- [x] [autoware_glog_component](../../src/universe/autoware_universe/common/autoware_glog_component/package.xml)
- [x] [autoware_time_utils](../../src/universe/autoware_universe/common/autoware_time_utils/package.xml)

Phase 9.3 complete, 2026-11-23: three packages and eight consumer cases pass;
the local glog dependency also passes 15 native/driver targets and all five CIE
container launch cases. See the module journals for exact scopes. Definition
coverage is now 114/244, leaving 130 packages without definitions. Upstream
Universe#13540 removes command-mode packages; this ledger retains the selected
snapshot until a source refresh explicitly changes the census.

### Phase 9.4: CPU point-cloud and IMU preprocessing

Integrate PCL extensions before their consumers. Inventory every preprocessor filter/plugin/executable and original fixture, including parameter, TF, timestamp and point-field contracts. Recheck affected existing occupancy-grid consumers when the local provider enters their actual build graph.

- [x] [autoware_imu_corrector](../../src/universe/autoware_universe/sensing/autoware_imu_corrector/package.xml)
- [x] [autoware_pcl_extensions](../../src/universe/autoware_universe/sensing/autoware_pcl_extensions/package.xml)
- [x] [autoware_pointcloud_preprocessor](../../src/universe/autoware_universe/sensing/autoware_pointcloud_preprocessor/package.xml)

Phase 9.4 complete, 2026-11-23: three Universe modules plus the local managed-TF
and generic CGAL prerequisites pass their complete selected gates. The preprocessor
includes all 185 original C++ cases and eight Python scenarios; IMU includes all
nine original cases. Added checks cover voxel geometry, frame/covariance/header
contracts and selected Agnocast-only transport. See the module journals for
exact profiles, independent source corrections and limitations. Definition
coverage is now 117/244, leaving 127 packages without definitions; the initial
census above is retained. Complete package-contract reconciliation remains 9.20.

### Phase 9.5: Clustering, shape estimation and map-aware detection

Reuse 9.4 and existing map/GridMap providers. Order shape estimation before clustering/detection, and validation before raindrop filtering. Inventory shape estimation's conditional CUDA/TensorRT path; CPU tests alone do not qualify a required learned-shape configuration. Check geometry, frames, cluster/shape outputs, maps and all existing datasets.

- [x] [autoware_cluster_merger](../../src/universe/autoware_universe/perception/autoware_cluster_merger/package.xml)
- [x] [autoware_compare_map_segmentation](../../src/universe/autoware_universe/perception/autoware_compare_map_segmentation/package.xml)
- [x] [autoware_detected_object_validation](../../src/universe/autoware_universe/perception/autoware_detected_object_validation/package.xml)
- [x] [autoware_elevation_map_loader](../../src/universe/autoware_universe/perception/autoware_elevation_map_loader/package.xml)
- [x] [autoware_ground_segmentation](../../src/universe/autoware_universe/perception/autoware_ground_segmentation/package.xml)
- [x] [autoware_occupancy_grid_map_outlier_filter](../../src/universe/autoware_universe/perception/autoware_occupancy_grid_map_outlier_filter/package.xml)
- [x] [autoware_shape_estimation](../../src/universe/autoware_universe/perception/autoware_shape_estimation/package.xml)
- [x] [autoware_euclidean_cluster](../../src/universe/autoware_universe/perception/autoware_euclidean_cluster/package.xml)
- [x] [autoware_raindrop_cluster_filter](../../src/universe/autoware_universe/perception/autoware_raindrop_cluster_filter/package.xml)
- [x] [autoware_detection_by_tracker](../../src/universe/autoware_universe/perception/autoware_detection_by_tracker/package.xml)

Phase 9.5 completed 2026-11-24: all ten checked package boundaries pass their
complete selected module gates, including conditional CPU/GPU shape estimation
and the required enabled transports. Provider and source fixes have separate
commits/evidence. Current coverage is 133/244 with 111 definitions pending.
PLAN.md records the gate summary; module journals contain exact commands,
prerequisites and limitations. The final contract audit remains Phase 9.20.

### Phase 9.6: Object association, geometry and postprocessing

Validate transforms, projection/association, covariance/identity preservation, ordering, feature removal and path outputs. Preserve all original suites and add actual message-flow checks for untested adapters. Reuse 9.2 outputs and existing tracking/geometry libraries.

- [x] [autoware_detected_object_feature_remover](../../src/universe/autoware_universe/perception/autoware_detected_object_feature_remover/package.xml)
- [x] [autoware_image_object_locator](../../src/universe/autoware_universe/perception/autoware_image_object_locator/package.xml)
- [x] [autoware_object_merger](../../src/universe/autoware_universe/perception/autoware_object_merger/package.xml)
- [x] [autoware_object_sorter](../../src/universe/autoware_universe/perception/autoware_object_sorter/package.xml)
- [x] [autoware_predicted_path_postprocessor](../../src/universe/autoware_universe/perception/autoware_predicted_path_postprocessor/package.xml)
- [x] [autoware_tracking_object_merger](../../src/universe/autoware_universe/perception/autoware_tracking_object_merger/package.xml)

### Phase 9.7: Traffic-light geometry, fusion and postprocessing

Cover ROI/map association, visibility/occlusion, camera synchronization, state arbitration and marker/image outputs. Separate these operations from model execution in 9.13. Supply maps/images through declared fixtures and retain frame/time/confidence and original suite contracts.

- [x] [autoware_crosswalk_traffic_light_estimator](../../src/universe/autoware_universe/perception/autoware_crosswalk_traffic_light_estimator/package.xml)
- [x] [autoware_traffic_light_arbiter](../../src/universe/autoware_universe/perception/autoware_traffic_light_arbiter/package.xml)
- [x] [autoware_traffic_light_category_merger](../../src/universe/autoware_universe/perception/autoware_traffic_light_category_merger/package.xml)
- [x] [autoware_traffic_light_map_based_detector](../../src/universe/autoware_universe/perception/autoware_traffic_light_map_based_detector/package.xml)
- [x] [autoware_traffic_light_multi_camera_fusion](../../src/universe/autoware_universe/perception/autoware_traffic_light_multi_camera_fusion/package.xml)
- [x] [autoware_traffic_light_occlusion_predictor](../../src/universe/autoware_universe/perception/autoware_traffic_light_occlusion_predictor/package.xml)
- [x] [autoware_traffic_light_recognition_marker_publisher](../../src/universe/autoware_universe/common/autoware_traffic_light_recognition_marker_publisher/package.xml)
- [x] [autoware_traffic_light_selector](../../src/universe/autoware_universe/perception/autoware_traffic_light_selector/package.xml)
- [x] [autoware_traffic_light_visualization](../../src/universe/autoware_universe/perception/autoware_traffic_light_visualization/package.xml)

### Phase 9.8: Localization adapters, landmarks and monitoring

Adopt landmark support before its localizers, using point-cloud support from 9.4 where required. Validate projection, covariance, initialization/service state, marker/image fixtures, TF, arbitration and diagnostic timing. Physical sensor qualification is distinct from replay/ROS behavior evidence.

- [x] [autoware_geo_pose_projector](../../src/universe/autoware_universe/localization/autoware_geo_pose_projector/package.xml)
- [x] [autoware_landmark_manager](../../src/universe/autoware_universe/localization/autoware_landmark_based_localizer/autoware_landmark_manager/package.xml)
- [x] [autoware_localization_error_monitor](../../src/universe/autoware_universe/localization/autoware_localization_error_monitor/package.xml)
- [x] [autoware_pose_covariance_modifier](../../src/universe/autoware_universe/localization/autoware_pose_covariance_modifier/package.xml)
- [x] [autoware_pose_estimator_arbiter](../../src/universe/autoware_universe/localization/autoware_pose_estimator_arbiter/package.xml)
- [x] [autoware_pose_initialization_requester](../../src/universe/autoware_universe/localization/autoware_pose_initialization_requester/package.xml)
- [x] [autoware_pose_instability_detector](../../src/universe/autoware_universe/localization/autoware_pose_instability_detector/package.xml)
- [x] [autoware_ar_tag_based_localizer](../../src/universe/autoware_universe/localization/autoware_landmark_based_localizer/autoware_ar_tag_based_localizer/package.xml)
- [x] [autoware_lidar_marker_localizer](../../src/universe/autoware_universe/localization/autoware_landmark_based_localizer/autoware_lidar_marker_localizer/package.xml)

### Phase 9.9: YabLoc visual localization

Start with yabloc_common, then its declared consumers. Declare map/image/camera-model inputs and any new third-party providers when exposed. Run complete image-processing/filter suites, deterministic seeded/replay checks where supported, initialization and monitor behavior; do not equate a build with localization accuracy.

- [x] [yabloc_common](../../src/universe/autoware_universe/localization/yabloc/yabloc_common/package.xml)
- [x] [yabloc_monitor](../../src/universe/autoware_universe/localization/yabloc/yabloc_monitor/package.xml)
- [x] [yabloc_image_processing](../../src/universe/autoware_universe/localization/yabloc/yabloc_image_processing/package.xml)
- [x] [yabloc_particle_filter](../../src/universe/autoware_universe/localization/yabloc/yabloc_particle_filter/package.xml)
- [x] [yabloc_pose_initializer](../../src/universe/autoware_universe/localization/yabloc/yabloc_pose_initializer/package.xml)

### Phase 9.10: Trajectory contracts and common motion checks

Integrate common checkers before validator/modifier; ranker follows validator and selector follows its adapters/ranker. Cover trajectory interfaces, bounds, obstacle/signal rules, scoring/selection, continuity and gate state. Preserve Python plotting/tool dependencies when an actual original tool/test needs them.

- [x] [autoware_boundary_departure_checker](../../src/universe/autoware_universe/common/autoware_boundary_departure_checker/package.xml)
- [x] [autoware_goal_distance_calculator](../../src/universe/autoware_universe/common/autoware_goal_distance_calculator/package.xml)
- [x] [autoware_obstacle_proximity_checker](../../src/universe/autoware_universe/common/autoware_obstacle_proximity_checker/package.xml)
- [x] [autoware_path_distance_calculator](../../src/universe/autoware_universe/common/autoware_path_distance_calculator/package.xml)
- [x] [autoware_traffic_light_compliance_checker](../../src/universe/autoware_universe/common/autoware_traffic_light_compliance_checker/package.xml)
- [x] [autoware_trajectory_adapter](../../src/universe/autoware_universe/planning/autoware_trajectory_adapter/package.xml)
- [x] [autoware_trajectory_concatenator](../../src/universe/autoware_universe/planning/autoware_trajectory_concatenator/package.xml)
- [x] [autoware_trajectory_gate](../../src/universe/autoware_universe/planning/autoware_trajectory_gate/package.xml)
- [x] [autoware_trajectory_modifier](../../src/universe/autoware_universe/planning/autoware_trajectory_modifier/package.xml)
- [x] [autoware_trajectory_validator](../../src/universe/autoware_universe/planning/autoware_trajectory_validator/package.xml)
- [x] [autoware_trajectory_ranker](../../src/universe/autoware_universe/planning/autoware_trajectory_ranker/package.xml)
- [x] [autoware_trajectory_selector](../../src/universe/autoware_universe/planning/autoware_trajectory_selector/package.xml)

### Phase 9.11: Additional planners and behavior plugins

Publish sampler support before sampling plugins, and consume 9.10 checkers. Revisit the existing behavior-path planner's optional sampler registration when required. Cover every original plugin suite plus individual/joint factory loading, lifecycle/reload and planner outputs; keep stable plugin type identities and indexed YAML/resources.

- [x] [autoware_behavior_path_direction_change_module](../../src/universe/autoware_universe/planning/behavior_path_planner/autoware_behavior_path_direction_change_module/package.xml)
- [x] [autoware_behavior_path_dynamic_obstacle_avoidance_module](../../src/universe/autoware_universe/planning/behavior_path_planner/autoware_behavior_path_dynamic_obstacle_avoidance_module/package.xml)
- [x] [autoware_behavior_path_external_request_lane_change_module](../../src/universe/autoware_universe/planning/behavior_path_planner/autoware_behavior_path_external_request_lane_change_module/package.xml)
- [x] [autoware_behavior_velocity_no_drivable_lane_module](../../src/universe/autoware_universe/planning/behavior_velocity_planner/autoware_behavior_velocity_no_drivable_lane_module/package.xml)
- [x] [autoware_behavior_velocity_occlusion_spot_module](../../src/universe/autoware_universe/planning/behavior_velocity_planner/autoware_behavior_velocity_occlusion_spot_module/package.xml)
- [x] [autoware_behavior_velocity_roundabout_module](../../src/universe/autoware_universe/planning/behavior_velocity_planner/autoware_behavior_velocity_roundabout_module/package.xml)
- [x] [autoware_behavior_velocity_speed_bump_module](../../src/universe/autoware_universe/planning/behavior_velocity_planner/autoware_behavior_velocity_speed_bump_module/package.xml)
- [x] [autoware_behavior_velocity_template_module](../../src/universe/autoware_universe/planning/behavior_velocity_planner/autoware_behavior_velocity_template_module/package.xml)
- [ ] [autoware_in_lane_mrm_planner](../../src/universe/autoware_universe/planning/autoware_in_lane_mrm_planner/package.xml)
- [ ] [autoware_minimum_rule_based_planner](../../src/universe/autoware_universe/planning/autoware_minimum_rule_based_planner/package.xml)
- [ ] [autoware_path_sampler](../../src/universe/autoware_universe/planning/sampling_based_planner/autoware_path_sampler/package.xml)
- [ ] [autoware_surround_obstacle_checker](../../src/universe/autoware_universe/planning/autoware_surround_obstacle_checker/package.xml)
- [ ] [autoware_behavior_path_sampling_planner_module](../../src/universe/autoware_universe/planning/behavior_path_planner/autoware_behavior_path_sampling_planner_module/package.xml)

### Phase 9.12: Control gates, operators and vehicle calibration tools

Use command-mode types from 9.3 and the relevant motion providers. Validate gating/state transitions, collision/path checks, joystick input messages, command outputs, diagnostics and calibration replay/tools. Pin required Python scientific/plotting inputs through Bazel. Real-vehicle calibration/control performance remains a separately evidenced hardware result.

- [ ] [autoware_accel_brake_map_calibrator](../../src/universe/autoware_universe/vehicle/autoware_accel_brake_map_calibrator/package.xml)
- [ ] [autoware_control_command_gate](../../src/universe/autoware_universe/control/autoware_control_command_gate/package.xml)
- [ ] [autoware_control_performance_analysis](../../src/universe/autoware_universe/control/autoware_control_performance_analysis/package.xml)
- [ ] [autoware_joy_controller](../../src/universe/autoware_universe/control/autoware_joy_controller/package.xml)
- [ ] [autoware_obstacle_collision_checker](../../src/universe/autoware_universe/control/autoware_obstacle_collision_checker/package.xml)
- [ ] [autoware_predicted_path_checker](../../src/universe/autoware_universe/control/autoware_predicted_path_checker/package.xml)
- [ ] [autoware_spheric_collision_detector](../../src/universe/autoware_universe/control/autoware_spheric_collision_detector/package.xml)
- [ ] [autoware_steer_offset_estimator](../../src/universe/autoware_universe/vehicle/autoware_steer_offset_estimator/package.xml)
- [ ] [autoware_stop_mode_operator](../../src/universe/autoware_universe/control/autoware_stop_mode_operator/package.xml)

### Phase 9.13: Traffic-light model execution and image tracking

Compose the CPU traffic-light stages from 9.7 with the existing YOLOX/classifier SDK support. Supply the selected model repositories and metadata, then run complete GPU/model and ROS suites, including composed pipeline tests. Add one explicit product-owned preparation profile when a concrete consumer first needs it; preserve current generation APIs until a qualified replacement exists.

- [ ] [autoware_bytetrack](../../src/universe/autoware_universe/perception/autoware_bytetrack/package.xml)
- [ ] [autoware_calibration_status_classifier](../../src/universe/autoware_universe/sensing/autoware_calibration_status_classifier/package.xml)
- [ ] [autoware_traffic_light_classifier](../../src/universe/autoware_universe/perception/autoware_traffic_light_classifier/package.xml)
- [ ] [autoware_traffic_light_fine_detector](../../src/universe/autoware_universe/perception/autoware_traffic_light_fine_detector/package.xml)
- [ ] [autoware_traffic_light_pipeline](../../src/universe/autoware_universe/perception/autoware_traffic_light_pipeline/package.xml)

### Phase 9.14: CUDA sensing and LiDAR model inference

Use 9.4 preprocessing and inspect actual CUDA-blackboard/SDK dependencies. Qualify CUDA filters and supported model/precision/shape profiles with real GPU execution and original suites. Import only the required model repository revisions with validated materialization/indexes. A CUDA/TensorRT CMake shim may be omitted only after its effects and full consumer gate are verified.

- [ ] [autoware_cuda_pointcloud_preprocessor](../../src/universe/autoware_universe/sensing/autoware_cuda_pointcloud_preprocessor/package.xml)
- [ ] [autoware_lidar_apollo_instance_segmentation](../../src/universe/autoware_universe/perception/autoware_lidar_apollo_instance_segmentation/package.xml)
- [ ] [autoware_lidar_centerpoint](../../src/universe/autoware_universe/perception/autoware_lidar_centerpoint/package.xml)
- [ ] [autoware_lidar_frnet](../../src/universe/autoware_universe/perception/autoware_lidar_frnet/package.xml)
- [ ] [autoware_lidar_transfusion](../../src/universe/autoware_universe/perception/autoware_lidar_transfusion/package.xml)
- [ ] [autoware_ptv3](../../src/universe/autoware_universe/perception/autoware_ptv3/package.xml)
- [ ] [autoware_ground_segmentation_cuda](../../src/universe/autoware_universe/perception/autoware_ground_segmentation_cuda/package.xml)

### Phase 9.15: BEV and multimodal perception fusion

Consume CenterPoint/clustering/preprocessing from earlier groups when required. Declare camera calibration, transforms, model inputs, custom plugins and temporal state explicitly. Validate actual GPU outputs, multi-camera/LiDAR association, history/reset behavior, ROS delivery and complete existing suites. Treat ABI/precision/SDK choices as hypotheses until tested.

- [ ] [autoware_bevfusion](../../src/universe/autoware_universe/perception/autoware_bevfusion/package.xml)
- [ ] [autoware_camera_streampetr](../../src/universe/autoware_universe/perception/autoware_camera_streampetr/package.xml)
- [ ] [autoware_image_projection_based_fusion](../../src/universe/autoware_universe/perception/autoware_image_projection_based_fusion/package.xml)
- [ ] [autoware_tensorrt_bevdet](../../src/universe/autoware_universe/perception/autoware_tensorrt_bevdet/package.xml)
- [ ] [autoware_tensorrt_bevformer](../../src/universe/autoware_universe/perception/autoware_tensorrt_bevformer/package.xml)

### Phase 9.16: Learned prediction, planning and control

Handle the larger model/scientific-Python closures per component, with separately committed model providers. Preserve the flat, launch-selected diffusion model convention. Check input/history state, output trajectories/commands, model metadata, interpreter/native ABI lifetimes, GPU behavior when required and complete original tools/tests. Do not label research smoke outputs as vehicle/accuracy qualification.

- [ ] [autoware_diffusion_planner](../../src/universe/autoware_universe/planning/autoware_diffusion_planner/package.xml)
- [ ] [autoware_simpl_prediction](../../src/universe/autoware_universe/perception/autoware_simpl_prediction/package.xml)
- [ ] [autoware_smart_mpc_trajectory_follower](../../src/universe/autoware_universe/control/autoware_smart_mpc_trajectory_follower/package.xml)
- [ ] [autoware_tensorrt_vad](../../src/universe/autoware_universe/e2e/autoware_tensorrt_vad/package.xml)

### Phase 9.17: System command modes, MRM, diagnostics and monitoring

Use 9.3 types and the control/operator interfaces. Order plugin providers after their base interfaces/managers. Inventory native and Python entrypoints, dynamically loaded plugins, OS/tool/driver prerequisites and diagnostic fixtures. Validate service/state transitions, loss/recovery, timeout behavior and monitors with declared fixtures or real required prerequisites; do not replace OS provisioning with build-time mutation.

- [ ] [autoware_adapi_visualizers](../../src/universe/autoware_universe/system/autoware_default_adapi_helpers/autoware_adapi_visualizers/package.xml)
- [ ] [autoware_command_mode_decider](../../src/universe/autoware_universe/system/autoware_command_mode_decider/package.xml)
- [ ] [autoware_command_mode_switcher](../../src/universe/autoware_universe/system/autoware_command_mode_switcher/package.xml)
- [ ] [autoware_diagnostic_graph_merger](../../src/universe/autoware_universe/system/autoware_diagnostic_graph_merger/package.xml)
- [ ] [autoware_diagnostic_graph_test_examples](../../src/universe/autoware_universe/examples/autoware_diagnostic_graph_test_examples/package.xml)
- [ ] [autoware_driving_mode_manager](../../src/universe/autoware_universe/system/autoware_driving_mode_manager/package.xml)
- [ ] [autoware_generic_service_divider](../../src/universe/autoware_universe/system/autoware_generic_service_divider/package.xml)
- [ ] [autoware_mrm_in_lane_stop_operator](../../src/universe/autoware_universe/system/autoware_mrm_in_lane_stop_operator/package.xml)
- [ ] [autoware_mrm_reset_manager](../../src/universe/autoware_universe/system/autoware_mrm_reset_manager/package.xml)
- [ ] [autoware_mrm_steering_hold_stop_operator](../../src/universe/autoware_universe/system/autoware_mrm_steering_hold_stop_operator/package.xml)
- [ ] [autoware_redundancy_adapi_switcher](../../src/universe/autoware_universe/system/autoware_redundancy_adapi_switcher/package.xml)
- [ ] [autoware_redundancy_switcher_interface](../../src/universe/autoware_universe/system/autoware_redundancy_switcher_interface/package.xml)
- [ ] [autoware_system_monitor](../../src/universe/autoware_universe/system/autoware_system_monitor/package.xml)
- [ ] [autoware_topic_relay_controller](../../src/universe/autoware_universe/system/autoware_topic_relay_controller/package.xml)
- [ ] [autoware_velodyne_monitor](../../src/universe/autoware_universe/system/autoware_velodyne_monitor/package.xml)
- [ ] [autoware_command_mode_decider_plugins](../../src/universe/autoware_universe/system/autoware_command_mode_decider_plugins/package.xml)
- [ ] [autoware_command_mode_switcher_plugins](../../src/universe/autoware_universe/system/autoware_command_mode_switcher_plugins/package.xml)
- [ ] [autoware_redundancy_switcher_interface_plugins](../../src/universe/autoware_universe/system/autoware_redundancy_switcher_interface_plugins/package.xml)

### Phase 9.18: Evaluation and simulator adapters

Bring the required local scenario-simulator/CARLA client and interface closure into Bazel transitively. Declare bags/scenarios and pinned external-service inputs where original tests require them. Check metrics, adapter protocols, simulated infrastructure, failure/recovery and clean shutdown; a mock does not satisfy an original test that requires a real service. Full adoption of other simulator repositories is a separate scope beyond this required closure.

- [ ] [autoware_carla_interface](../../src/universe/autoware_universe/simulator/autoware_carla_interface/package.xml)
- [ ] [autoware_dummy_infrastructure](../../src/universe/autoware_universe/simulator/autoware_dummy_infrastructure/package.xml)
- [ ] [autoware_evaluation_adapter](../../src/universe/autoware_universe/evaluator/autoware_evaluation_adapter/package.xml)
- [ ] [autoware_fault_injection](../../src/universe/autoware_universe/simulator/autoware_fault_injection/package.xml)
- [ ] [autoware_kinematic_evaluator](../../src/universe/autoware_universe/evaluator/autoware_kinematic_evaluator/package.xml)
- [ ] [autoware_localization_evaluator](../../src/universe/autoware_universe/evaluator/autoware_localization_evaluator/package.xml)
- [ ] [autoware_redundancy_command_selector](../../src/universe/autoware_universe/simulator/autoware_redundancy_command_selector/package.xml)
- [ ] [autoware_scenario_simulator_v2_adapter](../../src/universe/autoware_universe/evaluator/autoware_scenario_simulator_v2_adapter/package.xml)

### Phase 9.19: Remaining RViz and interactive tools

Reuse the qualified Qt/RViz/native-runtime providers. Validate factories, plugin/resource indexes, moc/slot ownership, publisher/service interactions and graphical operation where required. Declare bag playback and UI assets. Reuse the existing GUI simulator evidence until this product/plugin change actually requires its affected expensive gate.

- [ ] [autoware_bag_time_manager_rviz_plugin](../../src/universe/autoware_universe/visualization/autoware_bag_time_manager_rviz_plugin/package.xml)
- [ ] [autoware_traffic_light_rviz_plugin](../../src/universe/autoware_universe/visualization/autoware_traffic_light_rviz_plugin/package.xml)
- [ ] [tier4_active_generator_rviz_plugin](../../src/universe/autoware_universe/visualization/tier4_active_generator_rviz_plugin/package.xml)
- [ ] [tier4_camera_view_rviz_plugin](../../src/universe/autoware_universe/visualization/tier4_camera_view_rviz_plugin/package.xml)
- [ ] [tier4_manual_lane_change_rviz_plugin](../../src/universe/autoware_universe/visualization/tier4_manual_lane_change_rviz_plugin/package.xml)
- [ ] [tier4_perception_rviz_plugin](../../src/universe/autoware_universe/visualization/tier4_perception_rviz_plugin/package.xml)
- [ ] [tier4_traffic_light_rviz_plugin](../../src/universe/autoware_universe/visualization/tier4_traffic_light_rviz_plugin/package.xml)

## Closure work after package adoption

- [ ] **9.20 — Full package/evidence audit.** Reconcile all 244 rows with public
  libraries, components, standalone/Python tools, plugins, interfaces, installed
  configuration/data and complete original tests. Inventory optional compile
  branches and required backend/platform/model profiles. Reuse unchanged passing
  evidence; open concrete package tasks for gaps and run affected gates. The
  107 baseline declarations do not exempt their remaining contracts from this audit.
- [ ] **9.21 — Product and distribution acceptance.** Follow the
  [full modular runtime steps](PLAN.md#phase-921-full-modular-runtime-acceptance-planned). Compose affected sensing,
  localization, perception, planning/control and system/diagnostic products under
  bazel/. Validate representative end-to-end flows, model/map/config selection,
  plugin coexistence and startup/shutdown. Run affected native/OCI/simulator gates
  through --config=expensive only where the changed closure needs them. Publish
  the exact supported platform/backend/profile matrix and unresolved prerequisites.

Unavailable required models, SDKs, devices or services leave the associated gate
incomplete. Explicit platform incompatibility is not a passing test. The separate
Phase 8 JetPack scope and unqualified hardware behavior must remain visible; they
do not silently waive required Phase 9 module acceptance. No new package exclusion
is authorized by this roadmap.
