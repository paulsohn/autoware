// Lyrical loader entry points outside the standalone topic milestone.
#include <rmw/dynamic_message_type_support.h>
#include <rmw/error_handling.h>
#include <rmw/event.h>
#include <rmw/features.h>
#include <rmw/get_network_flow_endpoints.h>
#include <rmw/get_node_info_and_types.h>
#include <rmw/get_service_endpoint_info.h>
#include <rmw/get_service_names_and_types.h>
#include <rmw/get_topic_endpoint_info.h>
#include <rmw/get_topic_names_and_types.h>
#include <rmw/qos_profiles.h>
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

rmw_ret_t rmw_publisher_event_init(
  rmw_event_t * rmw_event, const rmw_publisher_t * publisher, rmw_event_type_t event_type)
{
  return unsupported(__func__);
}

rmw_ret_t rmw_subscription_event_init(
  rmw_event_t * rmw_event, const rmw_subscription_t * subscription, rmw_event_type_t event_type)
{
  return unsupported(__func__);
}

rmw_ret_t rmw_take_event(const rmw_event_t * event_handle, void * event_info, bool * taken)
{
  return unsupported(__func__);
}

bool rmw_event_type_is_supported(rmw_event_type_t rmw_event_type)
{
  return false;
}

bool rmw_feature_supported(rmw_feature_t feature)
{
  return false;
}

rmw_ret_t rmw_publisher_get_network_flow_endpoints(
  const rmw_publisher_t * publisher, rcutils_allocator_t * allocator,
  rmw_network_flow_endpoint_array_t * network_flow_endpoint_array)
{
  return unsupported(__func__);
}

rmw_ret_t rmw_subscription_get_network_flow_endpoints(
  const rmw_subscription_t * subscription, rcutils_allocator_t * allocator,
  rmw_network_flow_endpoint_array_t * network_flow_endpoint_array)
{
  return unsupported(__func__);
}

rmw_ret_t rmw_get_subscriber_names_and_types_by_node(
  const rmw_node_t * node, rcutils_allocator_t * allocator, const char * node_name,
  const char * node_namespace, bool no_demangle, rmw_names_and_types_t * topic_names_and_types)
{
  return unsupported(__func__);
}

rmw_ret_t rmw_get_publisher_names_and_types_by_node(
  const rmw_node_t * node, rcutils_allocator_t * allocator, const char * node_name,
  const char * node_namespace, bool no_demangle, rmw_names_and_types_t * topic_names_and_types)
{
  return unsupported(__func__);
}

rmw_ret_t rmw_get_service_names_and_types_by_node(
  const rmw_node_t * node, rcutils_allocator_t * allocator, const char * node_name,
  const char * node_namespace, rmw_names_and_types_t * service_names_and_types)
{
  return unsupported(__func__);
}

rmw_ret_t rmw_get_client_names_and_types_by_node(
  const rmw_node_t * node, rcutils_allocator_t * allocator, const char * node_name,
  const char * node_namespace, rmw_names_and_types_t * service_names_and_types)
{
  return unsupported(__func__);
}

rmw_ret_t rmw_get_clients_info_by_service(
  const rmw_node_t * node, rcutils_allocator_t * allocator, const char * service_name,
  bool no_mangle, rmw_service_endpoint_info_array_t * clients_info)
{
  return unsupported(__func__);
}

rmw_ret_t rmw_get_servers_info_by_service(
  const rmw_node_t * node, rcutils_allocator_t * allocator, const char * service_name,
  bool no_mangle, rmw_service_endpoint_info_array_t * servers_info)
{
  return unsupported(__func__);
}

rmw_ret_t rmw_get_service_names_and_types(
  const rmw_node_t * node, rcutils_allocator_t * allocator,
  rmw_names_and_types_t * service_names_and_types)
{
  return unsupported(__func__);
}

rmw_ret_t rmw_get_publishers_info_by_topic(
  const rmw_node_t * node, rcutils_allocator_t * allocator, const char * topic_name, bool no_mangle,
  rmw_topic_endpoint_info_array_t * publishers_info)
{
  return unsupported(__func__);
}

rmw_ret_t rmw_get_subscriptions_info_by_topic(
  const rmw_node_t * node, rcutils_allocator_t * allocator, const char * topic_name, bool no_mangle,
  rmw_topic_endpoint_info_array_t * subscriptions_info)
{
  return unsupported(__func__);
}

rmw_ret_t rmw_get_topic_names_and_types(
  const rmw_node_t * node, rcutils_allocator_t * allocator, bool no_demangle,
  rmw_names_and_types_t * topic_names_and_types)
{
  return unsupported(__func__);
}

const char * rmw_get_serialization_format(void)
{
  RMW_SET_ERROR_MSG("standalone RMW does not implement rmw_get_serialization_format");
  return nullptr;
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

rmw_ret_t rmw_publish_serialized_message(
  const rmw_publisher_t * publisher, const rmw_serialized_message_t * serialized_message,
  rmw_publisher_allocation_t * allocation)
{
  return unsupported(__func__);
}

rmw_ret_t rmw_get_serialized_message_size(
  const rosidl_message_type_support_t * type_support,
  const rosidl_runtime_c__Sequence__bound * message_bounds, size_t * size)
{
  return unsupported(__func__);
}

rmw_ret_t rmw_publisher_assert_liveliness(const rmw_publisher_t * publisher)
{
  return unsupported(__func__);
}

rmw_ret_t rmw_publisher_wait_for_all_acked(
  const rmw_publisher_t * publisher, rmw_time_t wait_timeout)
{
  return unsupported(__func__);
}

rmw_ret_t rmw_serialize(
  const void * ros_message, const rosidl_message_type_support_t * type_support,
  rmw_serialized_message_t * serialized_message)
{
  return unsupported(__func__);
}

rmw_ret_t rmw_deserialize(
  const rmw_serialized_message_t * serialized_message,
  const rosidl_message_type_support_t * type_support, void * ros_message)
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

rmw_ret_t rmw_subscription_set_content_filter(
  rmw_subscription_t * subscription, const rmw_subscription_content_filter_options_t * options)
{
  return unsupported(__func__);
}

rmw_ret_t rmw_subscription_get_content_filter(
  const rmw_subscription_t * subscription, rcutils_allocator_t * allocator,
  rmw_subscription_content_filter_options_t * options)
{
  return unsupported(__func__);
}

rmw_ret_t rmw_take_serialized_message(
  const rmw_subscription_t * subscription, rmw_serialized_message_t * serialized_message,
  bool * taken, rmw_subscription_allocation_t * allocation)
{
  return unsupported(__func__);
}

rmw_ret_t rmw_take_serialized_message_with_info(
  const rmw_subscription_t * subscription, rmw_serialized_message_t * serialized_message,
  bool * taken, rmw_message_info_t * message_info, rmw_subscription_allocation_t * allocation)
{
  return unsupported(__func__);
}

rmw_client_t * rmw_create_client(
  const rmw_node_t * node, const rosidl_service_type_support_t * type_support,
  const char * service_name, const rmw_qos_profile_t * qos_policies)
{
  RMW_SET_ERROR_MSG("standalone RMW does not implement rmw_create_client");
  return nullptr;
}

rmw_ret_t rmw_destroy_client(rmw_node_t * node, rmw_client_t * client)
{
  return unsupported(__func__);
}

rmw_ret_t rmw_send_request(
  const rmw_client_t * client, const void * ros_request, int64_t * sequence_id)
{
  return unsupported(__func__);
}

rmw_ret_t rmw_take_response(
  const rmw_client_t * client, rmw_service_info_t * request_header, void * ros_response,
  bool * taken)
{
  return unsupported(__func__);
}

rmw_ret_t rmw_client_request_publisher_get_actual_qos(
  const rmw_client_t * client, rmw_qos_profile_t * qos)
{
  return unsupported(__func__);
}

rmw_ret_t rmw_client_response_subscription_get_actual_qos(
  const rmw_client_t * client, rmw_qos_profile_t * qos)
{
  return unsupported(__func__);
}

rmw_service_t * rmw_create_service(
  const rmw_node_t * node, const rosidl_service_type_support_t * type_support,
  const char * service_name, const rmw_qos_profile_t * qos_profile)
{
  RMW_SET_ERROR_MSG("standalone RMW does not implement rmw_create_service");
  return nullptr;
}

rmw_ret_t rmw_destroy_service(rmw_node_t * node, rmw_service_t * service)
{
  return unsupported(__func__);
}

rmw_ret_t rmw_take_request(
  const rmw_service_t * service, rmw_service_info_t * request_header, void * ros_request,
  bool * taken)
{
  return unsupported(__func__);
}

rmw_ret_t rmw_send_response(
  const rmw_service_t * service, rmw_request_id_t * request_header, void * ros_response)
{
  return unsupported(__func__);
}

rmw_ret_t rmw_service_request_subscription_get_actual_qos(
  const rmw_service_t * service, rmw_qos_profile_t * qos)
{
  return unsupported(__func__);
}

rmw_ret_t rmw_service_response_publisher_get_actual_qos(
  const rmw_service_t * service, rmw_qos_profile_t * qos)
{
  return unsupported(__func__);
}

rmw_ret_t rmw_get_node_names(
  const rmw_node_t * node, rcutils_string_array_t * node_names,
  rcutils_string_array_t * node_namespaces)
{
  return unsupported(__func__);
}

rmw_ret_t rmw_get_node_names_with_enclaves(
  const rmw_node_t * node, rcutils_string_array_t * node_names,
  rcutils_string_array_t * node_namespaces, rcutils_string_array_t * enclaves)
{
  return unsupported(__func__);
}

rmw_ret_t rmw_count_publishers(const rmw_node_t * node, const char * topic_name, size_t * count)
{
  return unsupported(__func__);
}

rmw_ret_t rmw_count_subscribers(const rmw_node_t * node, const char * topic_name, size_t * count)
{
  return unsupported(__func__);
}

rmw_ret_t rmw_count_clients(const rmw_node_t * node, const char * service_name, size_t * count)
{
  return unsupported(__func__);
}

rmw_ret_t rmw_count_services(const rmw_node_t * node, const char * service_name, size_t * count)
{
  return unsupported(__func__);
}

rmw_ret_t rmw_get_gid_for_client(const rmw_client_t * client, rmw_gid_t * gid)
{
  return unsupported(__func__);
}

rmw_ret_t rmw_service_server_is_available(
  const rmw_node_t * node, const rmw_client_t * client, bool * is_available)
{
  return unsupported(__func__);
}

rmw_ret_t rmw_set_log_severity(rmw_log_severity_t severity)
{
  return unsupported(__func__);
}

rmw_ret_t rmw_subscription_set_on_new_message_callback(
  rmw_subscription_t * subscription, rmw_event_callback_t callback, const void * user_data)
{
  return unsupported(__func__);
}

rmw_ret_t rmw_service_set_on_new_request_callback(
  rmw_service_t * service, rmw_event_callback_t callback, const void * user_data)
{
  return unsupported(__func__);
}

rmw_ret_t rmw_client_set_on_new_response_callback(
  rmw_client_t * client, rmw_event_callback_t callback, const void * user_data)
{
  return unsupported(__func__);
}

rmw_ret_t rmw_event_set_callback(
  rmw_event_t * event, rmw_event_callback_t callback, const void * user_data)
{
  return unsupported(__func__);
}
