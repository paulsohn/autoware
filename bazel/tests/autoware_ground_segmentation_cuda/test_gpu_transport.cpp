// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include <agnocast/agnocast_callback_isolated_executor.hpp>
#include <ament_index_cpp/get_package_share_directory.hpp>
#include <autoware/agnocast_wrapper/subscription.hpp>
#include <autoware/cuda_scan_ground_segmentation/cuda_scan_ground_segmentation_filter_node.hpp>
#include <autoware/point_types/memory.hpp>
#include <cuda_blackboard/cuda_adaptation.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <future>
#include <mutex>
#include <thread>
#include <vector>

namespace wrapper = autoware::agnocast_wrapper;
namespace ground = autoware::cuda_ground_segmentation;
using Cloud = sensor_msgs::msg::PointCloud2;
using Point = ground::PointTypeStruct;
using namespace std::chrono_literals;

Cloud input_cloud(bool outside = false)
{
  Cloud input;
  input.header.frame_id = "base_link";
  input.header.stamp.sec = 42;
  input.height = 1;
  input.width = outside ? 3 : 2;
  input.fields = autoware::point_types::create_fields_point_xyzirc();
  input.point_step = sizeof(Point);
  input.row_step = input.width * input.point_step;
  input.is_dense = true;
  const std::vector<Point> points{
    {5.F, .01F, 0.F, 42, 1, 7}, {5.F, .01F, 1.5F, 99, 2, 8}, {100.F, .01F, 0.F, 123, 1, 9}};
  input.data.resize(input.row_step);
  std::memcpy(input.data.data(), points.data(), input.data.size());
  return input;
}

ground::FilterParameters parameters()
{
  ground::FilterParameters p{};
  p.min_x = p.min_y = -20.F;
  p.max_x = p.max_y = 20.F;
  p.min_z = -2.5F;
  p.max_z = 2.5F;
  p.max_radius = std::hypot(20.F, 20.F);
  p.non_ground_height_threshold = .2F;
  p.global_slope_max_angle_rad = 10.F * M_PI / 180.F;
  p.local_slope_max_angle_rad = 25.F * M_PI / 180.F;
  p.global_slope_max_ratio = std::tan(p.global_slope_max_angle_rad);
  p.local_slope_max_ratio = std::tan(p.local_slope_max_angle_rad);
  p.sector_angle_rad = M_PI / 180.F;
  p.inv_sector_angle_rad = 1.F / p.sector_angle_rad;
  p.num_sectors = std::ceil(2.F * M_PI / p.sector_angle_rad);
  p.cell_divider_size_m = .5F;
  p.max_num_cells_per_sector = p.max_radius / p.cell_divider_size_m;
  p.max_num_cells = p.max_num_cells_per_sector * p.num_sectors;
  p.recheck_start_distance = 20.F;
  p.detection_range_z_max = 3.2F;
  p.use_recheck_ground_cluster = 1;
  return p;
}

std::pair<Cloud, Cloud> classify(
  ground::CudaScanGroundSegmentationFilter & filter, const Cloud & input)
{
  using Adapter = rclcpp::TypeAdapter<cuda_blackboard::CudaPointCloud2, Cloud>;
  cuda_blackboard::CudaPointCloud2 device, ground, obstacle;
  Adapter::convert_to_custom(input, device);
  filter.classifyPointCloud(device, ground, obstacle);
  EXPECT_EQ(cudaDeviceSynchronize(), cudaSuccess);
  std::pair<Cloud, Cloud> output;
  Adapter::convert_to_ros_message(ground, output.first);
  Adapter::convert_to_ros_message(obstacle, output.second);
  return output;
}

void check_metadata(const Cloud & output, const Cloud & input)
{
  EXPECT_EQ(output.header, input.header);
  EXPECT_EQ(output.fields, input.fields);
  EXPECT_EQ(output.point_step, input.point_step);
  EXPECT_EQ(output.row_step, output.width * output.point_step);
  EXPECT_EQ(output.data.size(), output.height * output.row_step);
}

void check_mixed(const std::pair<Cloud, Cloud> & output, const Cloud & input)
{
  for (const auto * cloud : {&output.first, &output.second}) {
    check_metadata(*cloud, input);
    ASSERT_EQ(cloud->width, 1U);
    ASSERT_EQ(cloud->data.size(), sizeof(Point));
  }
  Point point{};
  std::memcpy(&point, output.first.data.data(), sizeof(point));
  EXPECT_EQ(point.intensity, 42);
  EXPECT_EQ(point.channel, 7);
  EXPECT_FLOAT_EQ(point.z, 0.F);
  std::memcpy(&point, output.second.data.data(), sizeof(point));
  EXPECT_EQ(point.intensity, 99);
  EXPECT_EQ(point.channel, 8);
  EXPECT_FLOAT_EQ(point.z, 1.5F);
}

TEST(GroundCuda, PreservesGroundAndObstacleRecords)
{
  ground::CudaScanGroundSegmentationFilter filter(parameters(), 32 * 1024 * 1024);
  const auto input = input_cloud();
  check_mixed(classify(filter, input), input);
}

TEST(GroundCuda, CropsOutsideSpatialBounds)
{
  ground::CudaScanGroundSegmentationFilter filter(parameters(), 32 * 1024 * 1024);
  const auto input = input_cloud(true);
  check_mixed(classify(filter, input), input);
}

TEST(GroundCuda, EmptyFramesDoNotReplayPreviousPoints)
{
  ground::CudaScanGroundSegmentationFilter filter(parameters(), 32 * 1024 * 1024);
  auto input = input_cloud();
  classify(filter, input);
  input.width = input.row_step = 0;
  input.data.clear();
  input.header.stamp.sec++;
  const auto output = classify(filter, input);
  for (const auto * cloud : {&output.first, &output.second}) {
    check_metadata(*cloud, input);
    EXPECT_EQ(cloud->width, 0U);
    EXPECT_TRUE(cloud->data.empty());
  }
  input = input_cloud();
  check_mixed(classify(filter, input), input);
}

TEST(GroundCuda, InitialEmptyAndFullyCroppedCloudsKeepMetadata)
{
  ground::CudaScanGroundSegmentationFilter filter(parameters(), 32 * 1024 * 1024);
  auto input = input_cloud();
  input.width = input.row_step = 0;
  input.data.clear();
  for (int frame = 0; frame < 2; ++frame) {
    const auto output = classify(filter, input);
    for (const auto * cloud : {&output.first, &output.second}) {
      check_metadata(*cloud, input);
      EXPECT_EQ(cloud->width, 0U);
      EXPECT_TRUE(cloud->data.empty());
    }
    input = input_cloud();
    for (std::size_t point = 0; point < input.width; ++point) {
      const float outside = 100.F;
      std::memcpy(input.data.data() + point * input.point_step, &outside, sizeof(outside));
    }
    input.header.stamp.sec++;
  }
}

TEST(GroundCuda, SingleClassCloudsKeepEmptyOutputMetadata)
{
  ground::CudaScanGroundSegmentationFilter filter(parameters(), 32 * 1024 * 1024);
  for (const float z : {0.F, 1.5F}) {
    auto input = input_cloud();
    for (std::size_t point = 0; point < input.width; ++point) {
      std::memcpy(input.data.data() + point * input.point_step + 8, &z, sizeof(z));
    }
    const auto output = classify(filter, input);
    for (const auto * cloud : {&output.first, &output.second}) {
      check_metadata(*cloud, input);
    }
    EXPECT_EQ(output.first.width, z == 0.F ? 2U : 0U);
    EXPECT_EQ(output.second.width, z == 0.F ? 0U : 2U);
  }
}

TEST(GroundCuda, ExchangesCloudsThroughSelectedBackend)
{
  rclcpp::NodeOptions options;
  options.use_intra_process_comms(true);
  options.arguments(
    {"--ros-args", "--params-file",
     ament_index_cpp::get_package_share_directory("autoware_ground_segmentation_cuda") +
       "/config/cuda_scan_ground_segmentation_filter.param.yaml",
     "-r", "~/input/pointcloud:=/ground_cuda_gate/input", "-r",
     "~/output/pointcloud:=/ground_cuda_gate/obstacles", "-r",
     "~/output/ground_pointcloud:=/ground_cuda_gate/ground"});
  options.append_parameter_override("max_mem_pool_size_in_byte", 32L * 1024 * 1024);
  auto node = std::make_shared<ground::CudaScanGroundSegmentationFilterNode>(options);
  auto peer = std::make_shared<rclcpp::Node>(
    "ground_cuda_peer", rclcpp::NodeOptions().use_intra_process_comms(true));
  auto publisher =
    peer->create_publisher<Cloud>("/ground_cuda_gate/input", rclcpp::SensorDataQoS());
  std::promise<std::pair<Cloud, Cloud>> received;
  auto future = received.get_future();
  std::pair<Cloud, Cloud> clouds;
  bool have_ground = false, have_obstacle = false, delivered = false;
  std::mutex mutex;
  auto collect = [&](bool is_ground, const Cloud & cloud) {
    std::lock_guard lock(mutex);
    if (delivered) return;
    (is_ground ? clouds.first : clouds.second) = cloud;
    (is_ground ? have_ground : have_obstacle) = true;
    if (have_ground && have_obstacle) {
      delivered = true;
      received.set_value(clouds);
    }
  };
  auto ground_sub = wrapper::create_subscription<Cloud>(
    peer.get(), "/ground_cuda_gate/ground", rclcpp::SensorDataQoS(),
    [&](const AUTOWARE_MESSAGE_CONST_SHARED_PTR(Cloud) & msg) { collect(true, *msg); }, {});
  auto obstacle_sub = wrapper::create_subscription<Cloud>(
    peer.get(), "/ground_cuda_gate/obstacles", rclcpp::SensorDataQoS(),
    [&](const AUTOWARE_MESSAGE_CONST_SHARED_PTR(Cloud) & msg) { collect(false, *msg); }, {});
  const auto input = input_cloud();
  auto exchange = [&](auto & executor) {
    executor.add_node(node);
    executor.add_node(peer);
    std::jthread spinning([&] { executor.spin(); });
    const auto deadline = std::chrono::steady_clock::now() + 15s;
    while (future.wait_for(0s) != std::future_status::ready &&
           std::chrono::steady_clock::now() < deadline) {
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
  check_mixed(future.get(), input);
}

int main(int argc, char ** argv)
{
  testing::InitGoogleTest(&argc, argv);
  wrapper::init(argc, argv, false);
  const auto result = RUN_ALL_TESTS();
  wrapper::shutdown();
  return result;
}
