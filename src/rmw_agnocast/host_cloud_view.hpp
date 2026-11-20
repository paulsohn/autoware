#pragma once
#include <rosidl_buffer/buffer.hpp>

namespace rmw_agnocast
{
// This implementation and its ownership state are constructed in the reader process.
// Keep the type non-final for RTTI across locally loaded component and RMW libraries.
class HostCloudView : public rosidl::BufferImplBase<uint8_t>
{
public:
  explicit HostCloudView(std::shared_ptr<const std::vector<uint8_t>> bytes)
  : bytes_(std::move(bytes))
  {
  }
  const std::vector<uint8_t> & bytes() const { return *bytes_; }
  std::weak_ptr<const std::vector<uint8_t>> owner() const { return bytes_; }
  std::string get_backend_type() const override { return "agnocast_host_view"; }
  size_t size() const override { return bytes_->size(); }
  std::unique_ptr<rosidl::BufferImplBase<uint8_t>> clone() const override { return to_cpu(); }
  std::unique_ptr<rosidl::BufferImplBase<uint8_t>> to_cpu() const override
  {
    auto local = std::make_unique<rosidl::CpuBufferImpl<uint8_t>>();
    local->get_storage() = *bytes_;
    return local;
  }

private:
  std::shared_ptr<const std::vector<uint8_t>> bytes_;
};
}  // namespace rmw_agnocast
