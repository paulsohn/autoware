#pragma once
#include <sensor_msgs/msg/point_cloud2.hpp>

// Same-host CPU transport. Only strings, vectors and values cross processes.
using Cloud = sensor_msgs::msg::PointCloud2;
struct HostCloud
{
  explicit HostCloud(const Cloud & msg)
  : header(msg.header),
    height(msg.height),
    width(msg.width),
    fields(msg.fields),
    big_endian(msg.is_bigendian),
    point_step(msg.point_step),
    row_step(msg.row_step),
    bytes(static_cast<const std::vector<uint8_t> &>(msg.data)),
    dense(msg.is_dense)
  {
  }
  void restore_metadata(Cloud & msg) const
  {
    msg.header = header;
    msg.height = height;
    msg.width = width;
    msg.fields = fields;
    msg.is_bigendian = big_endian;
    msg.point_step = point_step;
    msg.row_step = row_step;
    msg.is_dense = dense;
  }
  Cloud::_header_type header;
  uint32_t height, width;
  Cloud::_fields_type fields;
  bool big_endian;
  uint32_t point_step, row_step;
  std::vector<uint8_t> bytes;
  bool dense;
};
