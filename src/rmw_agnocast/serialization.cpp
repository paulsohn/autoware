#include "standalone.hpp"

#include <fastcdr/FastBuffer.h>
#include <rosidl_typesupport_fastrtps_cpp/message_type_support.h>

#include <cstring>

namespace
{
const message_type_support_callbacks_t * codec(const rosidl_message_type_support_t * ts)
{
  auto support = get_message_typesupport_handle(ts, "rosidl_typesupport_fastrtps_cpp");
  if (!support) {
    rmw_reset_error();
    support = get_message_typesupport_handle(ts, "rosidl_typesupport_fastrtps_c");
  }
  return support ? static_cast<const message_type_support_callbacks_t *>(support->data) : nullptr;
}
}  // namespace
const char * rmw_get_serialization_format()
{
  return "cdr";
}

rmw_ret_t rmw_serialize(
  const void * msg, const rosidl_message_type_support_t * ts, rmw_serialized_message_t * out)
{
  if (!msg || !ts || !out) return RMW_RET_INVALID_ARGUMENT;
  try {
    auto cb = codec(ts);
    if (!cb) return RMW_RET_UNSUPPORTED;
    size_t size = size_t(cb->get_serialized_size(msg)) + 4;
    if (out->buffer_capacity < size) {
      auto ret = rmw_serialized_message_resize(out, size);
      if (ret != RMW_RET_OK) return ret;
    }
    eprosima::fastcdr::FastBuffer buffer(
      reinterpret_cast<char *>(out->buffer), out->buffer_capacity);
    eprosima::fastcdr::Cdr stream(
      buffer, eprosima::fastcdr::Cdr::DEFAULT_ENDIAN, eprosima::fastcdr::CdrVersion::XCDRv1);
    stream.serialize_encapsulation();
    if (!cb->cdr_serialize(msg, stream)) return RMW_RET_ERROR;
    out->buffer_length = stream.get_serialized_data_length();
    return RMW_RET_OK;
  } catch (const std::exception & e) {
    RMW_SET_ERROR_MSG(e.what());
    return RMW_RET_ERROR;
  }
}
rmw_ret_t rmw_deserialize(
  const rmw_serialized_message_t * in, const rosidl_message_type_support_t * ts, void * msg)
{
  if (!in || !in->buffer || in->buffer_length > in->buffer_capacity || !ts || !msg)
    return RMW_RET_INVALID_ARGUMENT;
  try {
    auto cb = codec(ts);
    if (!cb) return RMW_RET_UNSUPPORTED;
    eprosima::fastcdr::FastBuffer buffer(reinterpret_cast<char *>(in->buffer), in->buffer_length);
    eprosima::fastcdr::Cdr stream(
      buffer, eprosima::fastcdr::Cdr::DEFAULT_ENDIAN, eprosima::fastcdr::CdrVersion::XCDRv1);
    stream.read_encapsulation();
    return cb->cdr_deserialize(stream, msg) ? RMW_RET_OK : RMW_RET_ERROR;
  } catch (const std::exception & e) {
    RMW_SET_ERROR_MSG(e.what());
    return RMW_RET_ERROR;
  }
}
rmw_ret_t rmw_get_serialized_message_size(
  const rosidl_message_type_support_t * ts, const rosidl_runtime_c__Sequence__bound * bounds,
  size_t * size)
{
  if (!ts || !bounds || !size) return RMW_RET_INVALID_ARGUMENT;
  auto cb = codec(ts);
  if (!cb) return RMW_RET_UNSUPPORTED;
  char kind{};
  auto max = cb->max_serialized_size(kind);
  if (!kind) {
    RMW_SET_ERROR_MSG("generated codec has no finite maximum for this type");
    return RMW_RET_UNSUPPORTED;
  }
  *size = max + 4;
  return RMW_RET_OK;
}
