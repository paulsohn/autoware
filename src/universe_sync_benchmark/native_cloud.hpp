#pragma once

#include <agnocast/agnocast.hpp>
#include <rmw_agnocast/host_cloud_view.hpp>

#include <sensor_msgs/msg/point_cloud2.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace native_sync
{
using Cloud = sensor_msgs::msg::PointCloud2;
using Loan = agnocast::ipc_shared_ptr<const Cloud>;

inline Cloud::ConstSharedPtr retain(Loan loan)
{
  const auto * address = loan.get();
  return Cloud::ConstSharedPtr(std::make_shared<Loan>(std::move(loan)), address);
}

inline Cloud::SharedPtr metadata(const Cloud & src)
{
  auto local = std::make_shared<Cloud>();
  local->header = src.header;
  local->fields = src.fields;
  local->height = src.height;
  local->width = src.width;
  local->row_step = src.row_step;
  local->point_step = src.point_step;
  local->is_bigendian = src.is_bigendian;
  local->is_dense = src.is_dense;
  return local;
}

inline Cloud::SharedPtr cpu_copy(Loan loan)
{
  auto local = metadata(*loan);
  local->data = static_cast<const std::vector<uint8_t> &>(loan->data);
  return local;
}

// Lyrical's Buffer implementation has a process-local vtable. Retain the native
// CPU vector and construct only the message metadata and Buffer facade locally.
inline Cloud::ConstSharedPtr collector_view(Loan loan)
{
  auto owner = retain(std::move(loan));
  if (std::getenv("SYNC_AUDIT")) {
    struct Audit
    {
      std::vector<std::weak_ptr<const Cloud>> owners;
      ~Audit()
      {
        const auto released = std::count_if(
          owners.begin(), owners.end(), [](const auto & owner) { return owner.expired(); });
        std::fprintf(
          stderr, "native_input owners=%zu released=%zu\n", owners.size(), size_t(released));
      }
    };
    static Audit audit;
    audit.owners.push_back(owner);
  }
  auto local = metadata(*owner);
  const auto & bytes = static_cast<const std::vector<uint8_t> &>(owner->data);
  local->data = rosidl::Buffer<uint8_t>(std::make_unique<rmw_agnocast::HostCloudView>(
    std::shared_ptr<const std::vector<uint8_t>>(owner, &bytes)));
  return local;
}

inline void publish(agnocast::Publisher<Cloud> & publisher, const Cloud & private_cloud)
{
  auto loan = publisher.borrow_loaned_message();
  *loan = private_cloud;
  publisher.publish(std::move(loan));
}
}  // namespace native_sync
