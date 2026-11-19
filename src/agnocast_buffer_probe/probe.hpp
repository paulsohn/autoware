#pragma once
#include <agnocast/agnocast_publisher.hpp>
#include <agnocast/agnocast_subscription.hpp>
#include <rosidl_buffer/buffer.hpp>

#include <span>

namespace probe
{
struct Bytes : std::vector<uint8_t>
{
  ~Bytes();
};
bool reclaimed(unsigned seed);
using Buffer = rosidl::Buffer<uint8_t>;
using Publisher = agnocast::Publisher<Bytes>;
using Reader = agnocast::TakeSubscription<Bytes>;
std::string topic();
void attach(std::shared_ptr<Reader> reader);
Buffer wrap(agnocast::ipc_shared_ptr<const Bytes> loan);
std::span<const uint8_t> view(const Buffer & buffer);
}  // namespace probe
