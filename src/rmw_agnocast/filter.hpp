#pragma once
#include <rosidl_typesupport_introspection_cpp/message_introspection.hpp>

#include <rmw/subscription_content_filter_options.h>

#include <functional>
#include <string>
#include <vector>
namespace standalone
{
struct Filter
{
  std::string expression;
  std::vector<std::string> parameters;
  std::function<bool(const void *)> matches;
  Filter() = default;
  Filter(
    const rosidl_typesupport_introspection_cpp::MessageMembers &,
    const rmw_subscription_content_filter_options_t &);
  rmw_ret_t get(rcutils_allocator_t *, rmw_subscription_content_filter_options_t *) const;
};
}  // namespace standalone
