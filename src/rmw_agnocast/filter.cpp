#include "filter.hpp"

#include <rosidl_typesupport_introspection_cpp/field_types.hpp>

#include <charconv>
#include <limits>
#include <regex>
#include <stdexcept>
namespace
{
using namespace rosidl_typesupport_introspection_cpp;
enum class Op { eq, ne, lt, le, gt, ge };
template <class T>
bool compare(const T & left, Op op, const T & right)
{
  switch (op) {
    case Op::eq:
      return left == right;
    case Op::ne:
      return left != right;
    case Op::lt:
      return left < right;
    case Op::le:
      return left <= right;
    case Op::gt:
      return left > right;
    case Op::ge:
      return left >= right;
  }
  return false;
}
template <class T>
auto numeric(const std::string & value, size_t offset, Op op)
{
  T v{};
  auto parsed = std::from_chars(value.data(), value.data() + value.size(), v);
  if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
    throw std::invalid_argument("invalid numeric filter operand");
  return [=](const void * msg) {
    return compare(*reinterpret_cast<const T *>(static_cast<const char *>(msg) + offset), op, v);
  };
}
}  // namespace
standalone::Filter::Filter(
  const MessageMembers & members, const rmw_subscription_content_filter_options_t & options)
{
  if (!options.filter_expression) throw std::invalid_argument("missing filter expression");
  expression = options.filter_expression;
  auto & args = options.expression_parameters;
  if (args.size > 100 || (args.size && !args.data))
    throw std::invalid_argument("invalid filter parameters");
  for (size_t i = 0; i < args.size; ++i) {
    if (!args.data[i]) throw std::invalid_argument("missing filter parameter");
    parameters.emplace_back(args.data[i]);
  }
  if (expression.empty()) return;
  static const std::regex grammar(
    R"(^\s*([a-zA-Z_]\w*(?:\.[a-zA-Z_]\w*)*)\s*(<>|!=|<=|>=|=|<|>)\s*(%[0-9]{1,2}|'(?:[^']|'')*'|[+\-a-zA-Z0-9.]+)\s*$)");
  std::smatch fields;
  if (!std::regex_match(expression, fields, grammar))
    throw std::invalid_argument("supported filter: scalar field comparison with a literal or %n");
  std::string field = fields[1], operation = fields[2], value = fields[3];
  Op op = operation == "="    ? Op::eq
          : operation == "<"  ? Op::lt
          : operation == "<=" ? Op::le
          : operation == ">"  ? Op::gt
          : operation == ">=" ? Op::ge
                              : Op::ne;
  if (value.front() == '%') {
    size_t n = std::stoul(value.substr(1));
    if (n >= parameters.size()) throw std::invalid_argument("unbound filter parameter");
    value = parameters[n];
  }
  const MessageMembers * current = &members;
  const MessageMember * member = nullptr;
  size_t offset = 0, start = 0;
  while (true) {
    auto end = field.find('.', start);
    auto part = field.substr(start, end - start);
    member = std::find_if(
      current->members_, current->members_ + current->member_count_,
      [&](const auto & m) { return part == m.name_; });
    if (
      member == current->members_ + current->member_count_ || member->is_array_ ||
      member->is_rosidl_buffer_)
      throw std::invalid_argument("unknown or non-scalar filter field");
    offset += member->offset_;
    if (end == field.npos) break;
    if (member->type_id_ != ROS_TYPE_MESSAGE)
      throw std::invalid_argument("invalid nested filter path");
    current = static_cast<const MessageMembers *>(member->members_->data);
    start = end + 1;
  }
  switch (member->type_id_) {
    case ROS_TYPE_STRING: {
      if (value.size() < 2 || value.front() != '\'' || value.back() != '\'')
        throw std::invalid_argument("string filter requires a quoted value");
      std::string decoded;
      for (size_t i = 1; i + 1 < value.size(); ++i) {
        if (value[i] == '\'' && (i + 2 >= value.size() || value[++i] != '\''))
          throw std::invalid_argument("invalid quoted filter string");
        decoded += value[i];
      }
      matches = [=](const void * msg) {
        return compare(
          *reinterpret_cast<const std::string *>(static_cast<const char *>(msg) + offset), op,
          decoded);
      };
      break;
    }
    case ROS_TYPE_BOOLEAN: {
      if (value != "true" && value != "false" && value != "0" && value != "1")
        throw std::invalid_argument("invalid boolean");
      bool v = value == "true" || value == "1";
      matches = [=](const void * msg) {
        return compare(
          *reinterpret_cast<const bool *>(static_cast<const char *>(msg) + offset), op, v);
      };
      break;
    }
    case ROS_TYPE_OCTET:
    case ROS_TYPE_UINT8:
      matches = numeric<uint8_t>(value, offset, op);
      break;
    case ROS_TYPE_INT8:
      matches = numeric<int8_t>(value, offset, op);
      break;
    case ROS_TYPE_UINT16:
      matches = numeric<uint16_t>(value, offset, op);
      break;
    case ROS_TYPE_INT16:
      matches = numeric<int16_t>(value, offset, op);
      break;
    case ROS_TYPE_UINT32:
      matches = numeric<uint32_t>(value, offset, op);
      break;
    case ROS_TYPE_INT32:
      matches = numeric<int32_t>(value, offset, op);
      break;
    case ROS_TYPE_UINT64:
      matches = numeric<uint64_t>(value, offset, op);
      break;
    case ROS_TYPE_INT64:
      matches = numeric<int64_t>(value, offset, op);
      break;
    case ROS_TYPE_FLOAT:
      matches = numeric<float>(value, offset, op);
      break;
    case ROS_TYPE_DOUBLE:
      matches = numeric<double>(value, offset, op);
      break;
    default:
      throw std::invalid_argument("unsupported filter field type");
  }
}
rmw_ret_t standalone::Filter::get(
  rcutils_allocator_t * a, rmw_subscription_content_filter_options_t * out) const
{
  std::vector<const char *> args;
  for (auto & p : parameters) args.push_back(p.c_str());
  return rmw_subscription_content_filter_options_init(
    expression.c_str(), args.size(), args.data(), a, out);
}
