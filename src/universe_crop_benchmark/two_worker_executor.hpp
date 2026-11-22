#pragma once

#include <rclcpp/executors/multi_threaded_executor.hpp>

namespace crop_benchmark
{
class TwoWorkerExecutor : public rclcpp::executors::MultiThreadedExecutor
{
public:
  TwoWorkerExecutor() : MultiThreadedExecutor(rclcpp::ExecutorOptions{}, 2) {}
};
}  // namespace crop_benchmark
