// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include "shape_estimation_node.hpp"

#include <ament_index_cpp/get_package_share_directory.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <future>
#include <thread>
namespace wrapper = autoware::agnocast_wrapper;
using Message = tier4_perception_msgs::msg::DetectedObjectsWithFeature;
void check_shape(int batch)
{
  rclcpp::NodeOptions options;
  options.arguments(
    {"--ros-args", "--params-file",
     ament_index_cpp::get_package_share_directory("autoware_shape_estimation") +
       "/config/shape_estimation.param.yaml"});
  options.append_parameter_override("use_corrector", false);
  options.append_parameter_override("use_filter", false);
#ifdef SHAPE_BATCH1
  const auto model =
    std::filesystem::path(std::getenv("TEST_TMPDIR")) / ("shape" + std::to_string(batch) + ".onnx");
  std::filesystem::copy_file(
    batch == 1 ? SHAPE_BATCH1 : SHAPE_BATCH2, model,
    std::filesystem::copy_options::overwrite_existing);
  options.append_parameter_override("model_params.use_ml_shape_estimator", true);
  options.append_parameter_override("model_params.batch_size", batch);
  options.append_parameter_override("model_path", model.string());
#endif
  auto node = std::make_shared<autoware::shape_estimation::ShapeEstimationNode>(options);
  auto peer = std::make_shared<wrapper::Node>("shape_peer");
  auto pub = peer->create_publisher<Message>("input", 1);
  std::promise<Message> received;
  auto result = received.get_future();
  bool delivered = false;
  auto sub = peer->create_subscription<Message>(
    "objects", rclcpp::QoS(1), [&](const AUTOWARE_MESSAGE_CONST_SHARED_PTR(Message) & msg) {
      if (!delivered) {
        delivered = true;
        received.set_value(*msg);
      }
    });
  Message input;
  input.header.frame_id = "base_link";
  input.header.stamp.sec = 123;
  input.header.stamp.nanosec = 456;
  input.feature_objects.resize(batch ? 3 : 1);
  for (size_t i = 0; i < input.feature_objects.size(); ++i) {
    auto & f = input.feature_objects[i];
    f.object.classification.resize(1);
    f.object.classification[0].label = i == 1 ? 2 : 1;
    f.object.classification[0].probability = .9;
    f.object.existence_probability = .8;
    f.object.kinematics.pose_with_covariance.covariance[0] = 7 + i;
    f.feature.roi.x_offset = i + 9;
    pcl::PointCloud<pcl::PointXYZ> cloud;
    for (int p = 0; p < (batch && i == 2 ? 600 : 32); ++p)
      cloud.emplace_back(
        5 + i + (batch ? 0 : (p & 1 ? 2 : -2)), 2 + (batch ? 0 : (p & 2 ? 1 : -1)),
        1 + (batch ? 0 : (p & 4 ? 1 : -1)));
    pcl::toROSMsg(cloud, f.feature.cluster);
  }
  auto exchange = [&](auto & executor) {
    std::jthread spin([&] { executor.spin(); });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (result.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready &&
           std::chrono::steady_clock::now() < deadline) {
      pub->publish(input);
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
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
  ASSERT_EQ(result.wait_for(std::chrono::milliseconds(0)), std::future_status::ready);
  const auto output = result.get();
  EXPECT_EQ(output.header, input.header);
  ASSERT_EQ(output.feature_objects.size(), input.feature_objects.size());
  for (size_t i = 0; i < output.feature_objects.size(); ++i) {
    const auto & out = output.feature_objects[i];
    const auto & expected = input.feature_objects[i];
    EXPECT_EQ(out.feature, expected.feature);
    auto retained = out.object;
    retained.shape = expected.object.shape;
    retained.kinematics.pose_with_covariance.pose =
      expected.object.kinematics.pose_with_covariance.pose;
    EXPECT_EQ(retained, expected.object);
    const auto & pose = out.object.kinematics.pose_with_covariance.pose;
    EXPECT_NEAR(pose.position.x, 5 + i, 1e-4);
    EXPECT_NEAR(pose.position.y, 2, 1e-4);
    EXPECT_NEAR(pose.position.z, 1, 1e-4);
    EXPECT_NEAR(
      pose.orientation.z * pose.orientation.z + pose.orientation.w * pose.orientation.w, 1, 1e-4);
    if (batch) {
      EXPECT_NEAR(pose.orientation.z, -std::sqrt(.5), 1e-4);
      EXPECT_NEAR(out.object.shape.dimensions.x, i == 1 ? 6.936331 : 4.6344314, 1e-4);
      EXPECT_NEAR(out.object.shape.dimensions.y, i == 1 ? 2.5178623 : 1.9600292, 1e-4);
      EXPECT_NEAR(out.object.shape.dimensions.z, i == 1 ? 2.8506238 : 1.7375569, 1e-4);
    } else {
      EXPECT_NEAR(out.object.shape.dimensions.x * out.object.shape.dimensions.y, 8, 1e-4);
      EXPECT_NEAR(out.object.shape.dimensions.z, 2, 1e-4);
    }
  }
}
#ifdef SHAPE_BATCH1
TEST(ShapeTransport, GPUFullAndPartialBatches)
{
  check_shape(1);
  check_shape(2);
}
#else
TEST(ShapeTransport, GeometricBoundingBox)
{
  check_shape(0);
}
#endif
int main(int argc, char ** argv)
{
  testing::InitGoogleTest(&argc, argv);
  wrapper::init(argc, argv, true);
  const auto result = RUN_ALL_TESTS();
  wrapper::shutdown();
  return result;
}
