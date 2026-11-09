#pragma once

#include <rclcpp/version.h>

// rclcpp 32 (Lyrical) passes shared pointers to Executor, ClientBase, ServiceBase and NodeLoggingInterface virtual
// methods by const reference; rclcpp 28 (Jazzy) passed them by value. Versions in between were not checked.
// Use for parameters of overrides of those methods: AGNOCAST_RCLCPP_SHARED_PTR_PARAM(rclcpp::Node::SharedPtr) node.
#if RCLCPP_VERSION_MAJOR >= 32
#define AGNOCAST_RCLCPP_SHARED_PTR_PARAM(...) const __VA_ARGS__ &
#else
#define AGNOCAST_RCLCPP_SHARED_PTR_PARAM(...) __VA_ARGS__
#endif
