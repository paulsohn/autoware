// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
#include <autoware/agnocast_wrapper/node.hpp>
#include <autoware/agnocast_wrapper/polling_subscriber.hpp>
#include <cuda_blackboard/cuda_adaptation.hpp>
#include <cuda_blackboard/cuda_blackboard_async_publisher.hpp>

#include <gtest/gtest.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>
#include <vector>

extern char ** environ;
using namespace std::chrono_literals;

sensor_msgs::msg::Image image()
{
  sensor_msgs::msg::Image msg;
  msg.header.frame_id = "gpu_payload";
  msg.height = 4;
  msg.width = 8;
  msg.encoding = "mono8";
  msg.step = 8;
  for (int i = 0; i < 32; ++i) msg.data.push_back(i * 7);
  return msg;
}

TEST(CudaPayload, ImageDeviceRoundTrip)
{
  const auto expected = image();
  cuda_blackboard::CudaImage device(expected);
  cudaPointerAttributes attributes{};
  ASSERT_EQ(cudaPointerGetAttributes(&attributes, device.data.get()), cudaSuccess);
  EXPECT_EQ(attributes.type, cudaMemoryTypeDevice);
  sensor_msgs::msg::Image actual;
  rclcpp::TypeAdapter<cuda_blackboard::CudaImage, sensor_msgs::msg::Image>::convert_to_ros_message(
    device, actual);
  EXPECT_EQ(actual, expected);
}

TEST(CudaPayload, PointCloudDeviceRoundTrip)
{
  sensor_msgs::msg::PointCloud2 expected;
  expected.header.frame_id = "cloud_payload";
  expected.height = 1;
  expected.width = 8;
  expected.point_step = 16;
  expected.row_step = 128;
  expected.data.resize(128);
  for (std::size_t i = 0; i < expected.data.size(); ++i) expected.data[i] = i;
  cuda_blackboard::CudaPointCloud2 device(expected);
  cudaPointerAttributes attributes{};
  ASSERT_EQ(cudaPointerGetAttributes(&attributes, device.data.get()), cudaSuccess);
  EXPECT_EQ(attributes.type, cudaMemoryTypeDevice);
  sensor_msgs::msg::PointCloud2 actual;
  rclcpp::TypeAdapter<cuda_blackboard::CudaPointCloud2, sensor_msgs::msg::PointCloud2>::
    convert_to_ros_message(device, actual);
  EXPECT_EQ(actual, expected);
}

int peer()
{
  auto node = std::make_shared<autoware::agnocast_wrapper::Node>("cuda_payload_peer");
  auto sub =
    autoware::agnocast_wrapper::polling::create_polling_subscriber<sensor_msgs::msg::Image>(
      node.get(), "/cuda_payload", rclcpp::SensorDataQoS().keep_last(1));
  const auto deadline = std::chrono::steady_clock::now() + 20s;
  while (std::chrono::steady_clock::now() < deadline) {
    auto received = sub->take_data();
    if (received) return *received == image() ? 0 : 2;
    std::this_thread::sleep_for(10ms);
  }
  return 3;
}

TEST(CudaPayload, AsyncPublisherDeliversExactPayloadAcrossProcesses)
{
  auto node = std::make_shared<rclcpp::Node>("cuda_payload_producer");
  cuda_blackboard::CudaBlackboardAsyncPublisher<cuda_blackboard::CudaImage> pub(
    *node, "/cuda_payload");
  // exec a fresh process: do not fork a live CUDA context into the peer.
  pid_t child;
  char executable[] = "/proc/self/exe";
  char peer_arg[] = "--payload-peer";
  char * args[] = {executable, peer_arg, nullptr};
  ASSERT_EQ(posix_spawn(&child, executable, nullptr, nullptr, args, environ), 0);
  int status = 0;
  bool finished = false;
  const auto deadline = std::chrono::steady_clock::now() + 25s;
  while (std::chrono::steady_clock::now() < deadline) {
    if (waitpid(child, &status, WNOHANG) == child) {
      finished = true;
      break;
    }
    pub.publish(std::make_unique<cuda_blackboard::CudaImage>(image()));
    rclcpp::spin_some(node);
    std::this_thread::sleep_for(20ms);
  }
  if (!finished) {
    kill(child, SIGKILL);
    waitpid(child, &status, 0);
  }
  ASSERT_TRUE(finished) << "payload peer timed out";
  ASSERT_TRUE(WIFEXITED(status));
  EXPECT_EQ(WEXITSTATUS(status), 0);
}

int main(int argc, char ** argv)
{
  rclcpp::init(0, nullptr);
  int result;
  if (argc == 2 && std::string(argv[1]) == "--payload-peer") {
    result = peer();
  } else {
    ::testing::InitGoogleTest(&argc, argv);
    result = RUN_ALL_TESTS();
  }
  rclcpp::shutdown();
  return result;
}
