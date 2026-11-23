// Lyrical RMW entry points that always return RMW_RET_UNSUPPORTED.
#include <rmw/dynamic_message_type_support.h>
#include <rmw/error_handling.h>
#include <rmw/rmw.h>

namespace
{
rmw_ret_t unsupported(const char * operation)
{
  RMW_SET_ERROR_MSG_WITH_FORMAT_STRING("standalone RMW does not implement %s", operation);
  return RMW_RET_UNSUPPORTED;
}
}  // namespace

rmw_ret_t rmw_take_dynamic_message(
  const rmw_subscription_t * subscription,
  rosidl_dynamic_typesupport_dynamic_data_t * dynamic_message, bool * taken,
  rmw_subscription_allocation_t * allocation)
{
  return unsupported(__func__);
}

rmw_ret_t rmw_take_dynamic_message_with_info(
  const rmw_subscription_t * subscription,
  rosidl_dynamic_typesupport_dynamic_data_t * dynamic_message, bool * taken,
  rmw_message_info_t * message_info, rmw_subscription_allocation_t * allocation)
{
  return unsupported(__func__);
}

rmw_ret_t rmw_serialization_support_init(
  const char * serialization_lib_name, rcutils_allocator_t * allocator,
  rosidl_dynamic_typesupport_serialization_support_t * serialization_support)
{
  return unsupported(__func__);
}

rmw_ret_t rmw_init_publisher_allocation(
  const rosidl_message_type_support_t * type_support,
  const rosidl_runtime_c__Sequence__bound * message_bounds, rmw_publisher_allocation_t * allocation)
{
  return unsupported(__func__);
}

rmw_ret_t rmw_fini_publisher_allocation(rmw_publisher_allocation_t * allocation)
{
  return unsupported(__func__);
}

rmw_ret_t rmw_init_subscription_allocation(
  const rosidl_message_type_support_t * type_support,
  const rosidl_runtime_c__Sequence__bound * message_bounds,
  rmw_subscription_allocation_t * allocation)
{
  return unsupported(__func__);
}

rmw_ret_t rmw_fini_subscription_allocation(rmw_subscription_allocation_t * allocation)
{
  return unsupported(__func__);
}
