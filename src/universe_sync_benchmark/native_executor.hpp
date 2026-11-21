#pragma once

#include <agnocast/agnocast_callback_isolated_executor.hpp>

#include <cstdlib>

namespace native_sync
{
// Keep the bundled default available for a controlled test of mixed callback groups.
class Executor : public agnocast::CallbackIsolatedAgnocastExecutor
{
public:
  Executor()
  : agnocast::CallbackIsolatedAgnocastExecutor(
      rclcpp::ExecutorOptions{}, std::getenv("SYNC_SHORT_WAIT") ? 1 : 50)
  {
  }
};
}  // namespace native_sync
