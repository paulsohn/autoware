#pragma once

#include "../rmw_agnocast/host_cloud_view.hpp"
#include "../universe_sync_benchmark/stage_trace.hpp"

#include <agnocast/agnocast_publisher.hpp>
#include <cuda_buffer/cuda_buffer_api.hpp>
#include <shmem_buffer/shmem_buffer_api.hpp>

#include <cstdlib>
#include <cstring>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <unistd.h>

namespace benchmark {

inline const std::string & buffer_backend()
{
  static const std::string backend = [] {
    const char * value = std::getenv("BENCH_BUFFER");
    return std::string(value ? value : "cpu");
  }();
  return backend;
}

inline void resize_point_payload(rosidl::Buffer<uint8_t> & buffer, size_t size)
{
  static const bool scoped = std::getenv("BENCH_PAYLOAD_SCOPE") != nullptr;
  if (scoped) {
    struct SharedAllocation {
      SharedAllocation() { agnocast::increment_borrowed_publisher_num(); }
      ~SharedAllocation() { agnocast::decrement_borrowed_publisher_num(); }
    } allocation;
    buffer.resize(size);
  } else {
    buffer.resize(size);
  }
}

inline bool explicit_subscription()
{
  return buffer_backend() != "cpu" || std::getenv("BENCH_TYPED_INPUT");
}

// CPU pointers remain valid while this view and the incoming message are alive.
class InputBytes {
  std::optional<shmem_buffer::ReadHandle> mapping_;
  std::vector<uint8_t> staged_;
public:
  std::span<const uint8_t> bytes;
  explicit InputBytes(const rosidl::Buffer<uint8_t> & buffer)
  {
    const auto & backend = buffer_backend();
    if (std::getenv("AGNOCAST_RMW_HOST_VIEW")) {
      if (buffer.get_backend_type() != "agnocast_host_view")
        throw std::runtime_error("host view required, received " + buffer.get_backend_type());
      bytes = dynamic_cast<const rmw_agnocast::HostCloudView &>(*buffer.get_impl()).bytes();
      return;
    }
    if (backend == "cpu") {
      // The native Agnocast CPU allocation may belong to a different process.
      bytes = static_cast<const std::vector<uint8_t> &>(buffer);
      return;
    }
    if (buffer.get_backend_type() != backend) throw std::runtime_error("buffer backend mismatch");
    if (buffer.size() == 0) return;
    if (backend == "shmem") {
      const auto & impl = dynamic_cast<const shmem_buffer::ShmemBufferImpl<uint8_t> &>(*buffer.get_impl());
      const auto & allocation = impl.get_shmem_buffer();
      if (!allocation.has_ipc_descriptor() || allocation.ipc_pid() == getpid())
        throw std::runtime_error("expected a remote shared-memory allocation");
      mapping_.emplace(shmem_buffer::from_input_buffer(buffer));
      bytes = {mapping_->get_ptr(), buffer.size()};
    } else if (backend == "cuda") {
      auto handle = cuda_buffer_backend::from_input_buffer(buffer, cudaStreamPerThread);
      staged_.resize(buffer.size());
      CUDA_CHECK(cudaMemcpyAsync(staged_.data(), handle.get_ptr(), staged_.size(),
        cudaMemcpyDeviceToHost, cudaStreamPerThread));
      CUDA_CHECK(cudaStreamSynchronize(cudaStreamPerThread));
      bytes = staged_;
    } else {
      throw std::runtime_error("unknown buffer backend");
    }
  }
};

// Promote locally produced CPU bytes to the selected transport allocation.
inline void prepare_output(rosidl::Buffer<uint8_t> & buffer)
{
  const auto & backend = buffer_backend();
  if (backend == "cpu") return;
  const std::vector<uint8_t> & bytes = buffer;
  if (backend == "shmem") {
    auto out = shmem_buffer::allocate_buffer(bytes.size());
    if (!bytes.empty()) {
      auto handle = shmem_buffer::from_output_buffer(out);
      std::memcpy(handle.get_ptr(), bytes.data(), bytes.size());
    }
    buffer = std::move(out);
  } else if (backend == "cuda") {
    auto out = cuda_buffer_backend::allocate_buffer(bytes.size());
    if (!bytes.empty()) {
      auto handle = cuda_buffer_backend::from_output_buffer(out, cudaStreamPerThread);
      CUDA_CHECK(cudaMemcpyAsync(handle.get_ptr(), bytes.data(), bytes.size(),
        cudaMemcpyHostToDevice, cudaStreamPerThread));
      CUDA_CHECK(cudaStreamSynchronize(cudaStreamPerThread));
    }
    buffer = std::move(out);
  } else {
    throw std::runtime_error("unknown buffer backend");
  }
}
}  // namespace benchmark
