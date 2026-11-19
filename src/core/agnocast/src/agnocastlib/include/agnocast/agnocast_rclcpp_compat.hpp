#pragma once

#include <rclcpp/version.h>

// rclcpp 32 passes shared pointers to overridden interfaces by const reference.
// The source API supported by Agnocast passed them by value.
#if RCLCPP_VERSION_MAJOR >= 32
#define AGNOCAST_RCLCPP_SHARED_PTR_PARAM(...) const __VA_ARGS__ &
#else
#define AGNOCAST_RCLCPP_SHARED_PTR_PARAM(...) __VA_ARGS__
#endif
