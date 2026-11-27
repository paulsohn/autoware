// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include <agnocast/agnocast_callback_isolated_executor.hpp>
#include <ament_index_cpp/get_package_share_directory.hpp>
#include <autoware/agnocast_wrapper/subscription.hpp>
#include <autoware/cuda_pointcloud_preprocessor/cuda_pointcloud_preprocessor_node.hpp>
#include <autoware/point_types/memory.hpp>
#include <cuda_blackboard/cuda_adaptation.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <future>
#include <thread>
#include <vector>

namespace wrapper = autoware::agnocast_wrapper;
using namespace std::chrono_literals;

namespace
{
sensor_msgs::msg::PointCloud2 input_cloud()
{
  sensor_msgs::msg::PointCloud2 input;
  using Point = autoware::point_types::PointXYZIRCAEDT;
  input.header.frame_id = "base_link";
  input.header.stamp.sec = 42;
  input.height = 1;
  input.width = 2;
  input.fields = autoware::point_types::create_fields_point_xyzircaedt();
  input.point_step = sizeof(Point);
  input.row_step = input.width * input.point_step;
  input.data.resize(input.row_step);
  input.is_dense = true;
  for (int i = 0; i < 2; ++i) {
    Point point{};
    point.x = 5.F + i;
    point.distance = point.x;
    point.intensity = 30;
    point.channel = i;
    std::memcpy(input.data.data() + i * sizeof(Point), &point, sizeof(Point));
  }
  return input;
}
}  // namespace

TEST(CudaPreprocessorTransport, ProcessesPointsThroughSelectedBackend)
{
  rclcpp::NodeOptions options;
  options.use_intra_process_comms(true);
  options.arguments(
    {"--ros-args", "--params-file",
     ament_index_cpp::get_package_share_directory("autoware_cuda_pointcloud_preprocessor") +
       "/config/cuda_pointcloud_preprocessor.param.yaml",
     "-r", "~/input/pointcloud:=/cuda_transport/input", "-r",
     "~/input/twist:=/cuda_transport/twist", "-r", "~/output/pointcloud:=/cuda_transport/output"});
  options.append_parameter_override("crop_box.negative", std::vector<bool>{true});
  options.append_parameter_override("use_imu", false);
  options.append_parameter_override("enable_ring_outlier_filter", false);
  options.append_parameter_override("max_input_point_count", 128L);
  auto node =
    std::make_shared<autoware::cuda_pointcloud_preprocessor::CudaPointcloudPreprocessorNode>(
      options);
  auto peer = std::make_shared<rclcpp::Node>(
    "cuda_transport_peer", rclcpp::NodeOptions().use_intra_process_comms(true));
  auto publisher = wrapper::create_publisher<sensor_msgs::msg::PointCloud2>(
    peer.get(), "/cuda_transport/input", rclcpp::SensorDataQoS());
  auto twist = peer->create_publisher<geometry_msgs::msg::TwistWithCovarianceStamped>(
    "/cuda_transport/twist", 10);
  std::promise<sensor_msgs::msg::PointCloud2> received;
  auto future = received.get_future();
  std::atomic<bool> delivered{false};
  using Cloud = sensor_msgs::msg::PointCloud2;
  auto subscription = wrapper::create_subscription<Cloud>(
    peer.get(), "/cuda_transport/output", rclcpp::SensorDataQoS(),
    [&](const AUTOWARE_MESSAGE_CONST_SHARED_PTR(Cloud) & message) {
      if (!delivered.exchange(true)) received.set_value(*message);
    },
    agnocast::SubscriptionOptions{});
  const auto input = input_cloud();
  geometry_msgs::msg::TwistWithCovarianceStamped velocity;
  velocity.header = input.header;
  auto exchange = [&](auto & executor) {
    executor.add_node(node);
    executor.add_node(peer);
    std::jthread spinning([&] { executor.spin(); });
    const auto deadline = std::chrono::steady_clock::now() + 15s;
    while (future.wait_for(0s) != std::future_status::ready &&
           std::chrono::steady_clock::now() < deadline) {
      twist->publish(velocity);
      publisher->publish(input);
      std::this_thread::sleep_for(50ms);
    }
    executor.cancel();
  };
  if (wrapper::use_agnocast()) {
    agnocast::CallbackIsolatedAgnocastExecutor executor;
    exchange(executor);
  } else {
    rclcpp::executors::SingleThreadedExecutor executor;
    exchange(executor);
  }
  ASSERT_EQ(future.wait_for(0s), std::future_status::ready);
  const auto output = future.get();
  EXPECT_EQ(output.header, input.header);
  ASSERT_EQ(output.width * output.height, 2U);
  std::vector<float> positions;
  for (int i = 0; i < 2; ++i) {
    float x;
    std::memcpy(&x, output.data.data() + i * output.point_step, sizeof(x));
    positions.push_back(x);
  }
  std::sort(positions.begin(), positions.end());
  EXPECT_EQ(positions, (std::vector<float>{5.F, 6.F}));
}

TEST(CudaPreprocessorMath, EqualTimeMotionSamplesDeskewStaticPoints)
{
  namespace cuda_pc = autoware::cuda_pointcloud_preprocessor;
  auto input = input_cloud();
  using Point = autoware::point_types::PointXYZIRCAEDT;
  constexpr double dt = .05;
  // A stationary landmark seen from a vehicle moving at body vx=1, yaw rate=1.
  Point second{};
  std::memcpy(&second, input.data.data() + sizeof(Point), sizeof(Point));
  second.x = static_cast<float>(5. * std::cos(dt) - std::sin(dt));
  second.y = static_cast<float>(1. - std::cos(dt) - 5. * std::sin(dt));
  second.distance = std::hypot(second.x, second.y);
  second.time_stamp = 50'000'000;
  std::memcpy(input.data.data() + sizeof(Point), &second, sizeof(Point));
  cuda_pc::PreprocessorCapacity capacity{};
  capacity.max_input_point_count = 2;
  capacity.max_twist_struct_count = 4;
  cuda_pc::CudaPointcloudPreprocessor preprocessor(capacity);
  preprocessor.setRingOutlierFilterActive(false);
  preprocessor.setUndistortionType(
    cuda_pc::CudaPointcloudPreprocessor::UndistortionType::Undistortion3D);
  geometry_msgs::msg::TransformStamped transform;
  transform.transform.rotation.w = 1.;
  geometry_msgs::msg::TwistWithCovarianceStamped twist;
  twist.header = input.header;
  twist.header.stamp.nanosec = 50'000'000;
  twist.twist.twist.linear.x = 1.;
  geometry_msgs::msg::Vector3Stamped angular;
  angular.header = twist.header;
  angular.vector.z = 1.;
  const auto output = preprocessor.process(input, transform, {twist}, {angular}, 0U);
  ASSERT_EQ(output->width * output->height, 2U);
  cudaPointerAttributes attributes{};
  ASSERT_EQ(cudaPointerGetAttributes(&attributes, output->data.get()), cudaSuccess);
  EXPECT_EQ(attributes.type, cudaMemoryTypeDevice);
  std::vector<cuda_pc::OutputPointType> points(2);
  ASSERT_EQ(
    cudaMemcpy(points.data(), output->data.get(), output->row_step, cudaMemcpyDeviceToHost),
    cudaSuccess);
  for (const auto & point : points) {
    EXPECT_NEAR(point.x, 5.F, 1e-4F);
    EXPECT_NEAR(point.y, 0.F, 1e-4F);
    EXPECT_NEAR(point.z, 0.F, 1e-4F);
  }
}

TEST(CudaPreprocessorMath, MotionSampleAtCloudStartRetainsPoints)
{
  namespace cuda_pc = autoware::cuda_pointcloud_preprocessor;
  const auto input = input_cloud();
  geometry_msgs::msg::TransformStamped transform;
  transform.transform.rotation.w = 1.;
  geometry_msgs::msg::TwistWithCovarianceStamped twist;
  twist.header = input.header;
  geometry_msgs::msg::Vector3Stamped angular;
  angular.header = input.header;
  for (const auto mode :
       {cuda_pc::CudaPointcloudPreprocessor::UndistortionType::Undistortion2D,
        cuda_pc::CudaPointcloudPreprocessor::UndistortionType::Undistortion3D}) {
    cuda_pc::PreprocessorCapacity capacity{};
    capacity.max_input_point_count = 2;
    capacity.max_twist_struct_count = 2;
    cuda_pc::CudaPointcloudPreprocessor preprocessor(capacity);
    preprocessor.setRingOutlierFilterActive(false);
    preprocessor.setUndistortionType(mode);
    const auto output = preprocessor.process(input, transform, {twist}, {angular}, 0U);
    ASSERT_EQ(output->width * output->height, 2U);
    std::vector<cuda_pc::OutputPointType> points(2);
    ASSERT_EQ(
      cudaMemcpy(points.data(), output->data.get(), output->row_step, cudaMemcpyDeviceToHost),
      cudaSuccess);
    for (int i = 0; i < 2; ++i) {
      EXPECT_FLOAT_EQ(points[i].x, 5.F + i);
      EXPECT_FLOAT_EQ(points[i].y, 0.F);
      EXPECT_FLOAT_EQ(points[i].z, 0.F);
    }
  }
}

int main(int argc, char ** argv)
{
  testing::InitGoogleTest(&argc, argv);
  wrapper::init(argc, argv, false);
  const auto result = RUN_ALL_TESTS();
  wrapper::shutdown();
  return result;
}
