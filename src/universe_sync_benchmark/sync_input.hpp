#pragma once

#include <rmw_agnocast/host_cloud_view.hpp>

#include <autoware/point_types/types.hpp>
#include <autoware/pointcloud_preprocessor/utility/memory.hpp>

#include <algorithm>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

namespace autoware::pointcloud_preprocessor::host_view_input
{
using Cloud = sensor_msgs::msg::PointCloud2;
using Bytes = std::vector<uint8_t>;

// Keep no strong references. Collect weak references only in the lifetime check.
struct LoanAudit
{
  std::vector<std::weak_ptr<const Bytes>> loans;
  size_t bytes = 0, views = 0, copies = 0;
  ~LoanAudit()
  {
    auto released =
      std::count_if(loans.begin(), loans.end(), [](const auto & p) { return p.expired(); });
    std::fprintf(stderr, "sync_input views=%zu copies=%zu\n", views, copies);
    if (loans.empty()) return;
    std::fprintf(
      stderr, "sync_view_audit loans=%zu released=%zu bytes=%zu\n", loans.size(),
      static_cast<size_t>(released), bytes);
  }
};

// The caller initializes the canonical XYZIRC fields and frame before conversion.
inline void convert_xyzirc(const Cloud & src, Cloud & dst)
{
  using Point = autoware::point_types::PointXYZIRC;
  static thread_local LoanAudit audit;
  const Bytes * bytes;
  if (const auto * view = dynamic_cast<const rmw_agnocast::HostCloudView *>(src.data.get_impl())) {
    ++audit.views;
    bytes = &view->bytes();
    if (std::getenv("SYNC_AUDIT")) {
      if (view->owner().lock().get() != bytes) throw std::runtime_error("unexpected loan storage");
      audit.loans.push_back(view->owner());
      audit.bytes += bytes->size();
    }
  } else {
    ++audit.copies;
    bytes = &static_cast<const Bytes &>(src.data);
  }
  if (
    !autoware::pointcloud_preprocessor::utils::is_data_layout_compatible_with_point_xyzirc(src) ||
    src.point_step < sizeof(Point) ||
    src.is_bigendian != (std::endian::native == std::endian::big) ||
    uint64_t(src.width) * src.point_step > src.row_step ||
    uint64_t(src.row_step) * src.height != bytes->size())
    throw std::runtime_error("expected native-endian XYZIRC prefix and valid row strides");

  const auto n = size_t(src.width) * src.height;
  if (n > UINT32_MAX / sizeof(Point)) throw std::length_error("XYZIRC output row too large");
  dst.height = 1;
  dst.width = n;
  dst.row_step = n * sizeof(Point);
  dst.data.resize(dst.row_step);
  if (n == 0) return;
  auto * out = dst.data.data();
  for (size_t row = 0; row < src.height; ++row) {
    auto * in = bytes->data() + row * src.row_step;
    if (src.point_step == sizeof(Point)) {
      std::memcpy(out, in, size_t(src.width) * sizeof(Point));
      out += size_t(src.width) * sizeof(Point);
    } else {
      for (size_t col = 0; col < src.width; ++col, out += sizeof(Point), in += src.point_step)
        std::memcpy(out, in, sizeof(Point));
    }
  }
}
}  // namespace autoware::pointcloud_preprocessor::host_view_input
