// Optional Lyrical APIs and transport capability queries.
#include "standalone.hpp"

#include <rcutils/logging.h>
#include <rmw/dynamic_message_type_support.h>
#include <rmw/error_handling.h>
#include <rmw/features.h>
#include <rmw/get_network_flow_endpoints.h>
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

bool rmw_feature_supported(rmw_feature_t feature)
{
  return feature == RMW_FEATURE_MESSAGE_INFO_PUBLICATION_SEQUENCE_NUMBER ||
         feature == RMW_FEATURE_MESSAGE_INFO_RECEPTION_SEQUENCE_NUMBER;
}

rmw_ret_t rmw_publisher_get_network_flow_endpoints(
  const rmw_publisher_t * publisher, rcutils_allocator_t * allocator,
  rmw_network_flow_endpoint_array_t * network_flow_endpoint_array)
{
  if (
    !publisher || publisher->implementation_identifier != standalone::id || !allocator ||
    !rcutils_allocator_is_valid(allocator) || !network_flow_endpoint_array)
    return RMW_RET_INVALID_ARGUMENT;
  auto ret = rmw_network_flow_endpoint_array_check_zero(network_flow_endpoint_array);
  return ret == RMW_RET_OK
           ? rmw_network_flow_endpoint_array_init(network_flow_endpoint_array, 0, allocator)
           : ret;
}

rmw_ret_t rmw_subscription_get_network_flow_endpoints(
  const rmw_subscription_t * subscription, rcutils_allocator_t * allocator,
  rmw_network_flow_endpoint_array_t * network_flow_endpoint_array)
{
  if (
    !subscription || subscription->implementation_identifier != standalone::id || !allocator ||
    !rcutils_allocator_is_valid(allocator) || !network_flow_endpoint_array)
    return RMW_RET_INVALID_ARGUMENT;
  auto ret = rmw_network_flow_endpoint_array_check_zero(network_flow_endpoint_array);
  return ret == RMW_RET_OK
           ? rmw_network_flow_endpoint_array_init(network_flow_endpoint_array, 0, allocator)
           : ret;
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

rmw_ret_t rmw_set_log_severity(rmw_log_severity_t severity)
{
  switch (severity) {
    case RMW_LOG_SEVERITY_DEBUG:
    case RMW_LOG_SEVERITY_INFO:
    case RMW_LOG_SEVERITY_WARN:
    case RMW_LOG_SEVERITY_ERROR:
    case RMW_LOG_SEVERITY_FATAL:
      return rcutils_logging_set_logger_level("Agnocast", severity) == RCUTILS_RET_OK
               ? RMW_RET_OK
               : RMW_RET_ERROR;
    default:
      return RMW_RET_INVALID_ARGUMENT;
  }
}
