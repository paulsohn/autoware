// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include <ament_index_cpp/get_package_share_directory.hpp>
#include <autoware/image_projection_based_fusion/roi_detected_object_fusion/node.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <thread>

namespace wrapper = autoware::agnocast_wrapper;
using Objects = autoware_perception_msgs::msg::DetectedObjects;
using Regions = tier4_perception_msgs::msg::DetectedObjectsWithFeature;
using Camera = sensor_msgs::msg::CameraInfo;
using namespace std::chrono_literals;

TEST(FusionTransport, CameraAssociationThroughSelectedBackend)
{
  const auto config =
    ament_index_cpp::get_package_share_directory("autoware_image_projection_based_fusion") +
    "/config/";
  rclcpp::NodeOptions options;
  options.arguments(
    {"--ros-args", "--params-file", config + "fusion_common.param.yaml", "--params-file",
     config + "roi_detected_object_fusion.param.yaml", "-r", "input:=/fusion_native/input", "-r",
     "output:=/fusion_native/output"});
  options.append_parameter_override("rois_number", 1);
  options.append_parameter_override("rois_timestamp_offsets", std::vector<double>{0.});
  options.append_parameter_override("input/rois0", "/fusion_native/rois");
  options.append_parameter_override("input/camera_info0", "/fusion_native/camera");
  options.append_parameter_override("matching_strategy.type", "naive");
  options.append_parameter_override("matching_strategy.threshold", .05);
  auto node =
    std::make_shared<autoware::image_projection_based_fusion::RoiDetectedObjectFusionNode>(options);
  auto peer = std::make_shared<wrapper::Node>("fusion_native_peer");
  auto source = peer->create_publisher<Objects>("/fusion_native/input", rclcpp::QoS(1));
  auto rois = peer->create_publisher<Regions>("/fusion_native/rois", rclcpp::QoS(1));
  auto camera = peer->create_publisher<Camera>("/fusion_native/camera", rclcpp::QoS(1));
  Objects input;
  input.header.frame_id = "base_link";
  input.header.stamp.sec = 42;
  input.objects.resize(1);
  auto & object = input.objects.front();
  object.existence_probability = .1F;
  object.classification.resize(1);
  object.classification.front().label = autoware_perception_msgs::msg::ObjectClassification::CAR;
  object.classification.front().probability = 1.F;
  object.kinematics.pose_with_covariance.pose.position.z = 10.;
  object.kinematics.pose_with_covariance.pose.orientation.w = 1.;
  object.shape.type = autoware_perception_msgs::msg::Shape::BOUNDING_BOX;
  object.shape.dimensions.x = object.shape.dimensions.y = object.shape.dimensions.z = 2.;
  Regions regions;
  regions.header = input.header;
  regions.feature_objects.resize(1);
  regions.feature_objects.front().object.classification = object.classification;
  auto & roi = regions.feature_objects.front().feature.roi;
  roi.x_offset = 27;
  roi.y_offset = 19;
  roi.width = roi.height = 10;
  Camera info;
  info.header = input.header;
  info.width = 64;
  info.height = 48;
  info.distortion_model = "plumb_bob";
  info.d.resize(5, 0.);
  info.k = {40., 0., 32., 0., 40., 24., 0., 0., 1.};
  info.r = {1., 0., 0., 0., 1., 0., 0., 0., 1.};
  info.p = {40., 0., 32., 0., 0., 40., 24., 0., 0., 0., 1., 0.};
  std::promise<Objects> received;
  auto future = received.get_future();
  bool delivered = false;
  auto subscription = peer->create_subscription<Objects>(
    "/fusion_native/output", rclcpp::QoS(1),
    [&](const AUTOWARE_MESSAGE_CONST_SHARED_PTR(Objects) & message) {
      if (!delivered && !message->objects.empty()) {
        delivered = true;
        received.set_value(*message);
      }
    });
  auto exchange = [&](auto & executor) {
    std::jthread spinning([&] { executor.spin(); });
    const auto calibration_deadline = std::chrono::steady_clock::now() + 300ms;
    while (std::chrono::steady_clock::now() < calibration_deadline) {
      camera->publish(info);
      std::this_thread::sleep_for(20ms);
    }
    const auto deadline = std::chrono::steady_clock::now() + 15s;
    while (future.wait_for(0s) != std::future_status::ready &&
           std::chrono::steady_clock::now() < deadline) {
      camera->publish(info);
      source->publish(input);
      rois->publish(regions);
      std::this_thread::sleep_for(50ms);
    }
    executor.cancel();
  };
  if (wrapper::use_agnocast()) {
    EXPECT_FALSE(rclcpp::ok());
    agnocast::AgnocastOnlySingleThreadedExecutor executor;
    executor.add_node(node->get_agnocast_node());
    executor.add_node(peer->get_agnocast_node());
    exchange(executor);
  } else {
    rclcpp::executors::SingleThreadedExecutor executor;
    executor.add_node(node->get_rclcpp_node());
    executor.add_node(peer->get_rclcpp_node());
    exchange(executor);
  }
  ASSERT_EQ(future.wait_for(0s), std::future_status::ready);
  const auto output = future.get();
  EXPECT_EQ(output.header, input.header);
  EXPECT_EQ(output.objects, input.objects);
}

int main(int argc, char ** argv)
{
  testing::InitGoogleTest(&argc, argv);
  wrapper::init(argc, argv, true);
  const auto result = RUN_ALL_TESTS();
  wrapper::shutdown();
  return result;
}
