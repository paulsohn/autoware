#pragma once

#include <rclcpp/executors/multi_threaded_executor.hpp>

namespace crop_benchmark
{
template<size_t Workers>
class WorkerExecutor : public rclcpp::executors::MultiThreadedExecutor
{
public:
  WorkerExecutor() : MultiThreadedExecutor(rclcpp::ExecutorOptions{}, Workers) {}
};
using TwoWorkerExecutor = WorkerExecutor<2>;
using ThreeWorkerExecutor = WorkerExecutor<3>;
}  // namespace crop_benchmark
