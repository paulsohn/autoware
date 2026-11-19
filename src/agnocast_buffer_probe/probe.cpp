#include "probe.hpp"

#include <shmem_buffer/shmem_buffer_api.hpp>

#include <sensor_msgs/msg/point_cloud2.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

using namespace std::chrono_literals;
using Cloud = sensor_msgs::msg::PointCloud2;
namespace fs = std::filesystem;

template <class Predicate>
void wait_for(Predicate ready, const std::string & what)
{
  auto end = std::chrono::steady_clock::now() + 20s;
  while (!ready()) {
    if (std::chrono::steady_clock::now() > end) throw std::runtime_error("timeout: " + what);
    std::this_thread::sleep_for(10ms);
  }
}
void mark(const std::string & name)
{
  std::ofstream(name + ".tmp") << "ready";
  fs::rename(name + ".tmp", name);
}
void await(const std::string & name)
{
  wait_for([&] { return fs::exists(name); }, name);
}
void require(bool ok, const char * message)
{
  if (!ok) throw std::runtime_error(message);
}

constexpr size_t size = 65536;
void verify(std::span<const uint8_t> bytes, uint8_t seed = 1)
{
  require(bytes.size() == size, "size mismatch");
  for (size_t i = 0; i < size; ++i)
    require(bytes[i] == static_cast<uint8_t>(seed + i), "payload mismatch");
}
void run(const std::string & role)
{
  auto node = std::make_shared<rclcpp::Node>("probe_" + role);
  const auto qos = rclcpp::QoS(1).transient_local();
  std::shared_ptr<probe::Reader> reader;
  if (role != "cpu" && role != "shmem" && role != "decline") {
    reader = std::make_shared<probe::Reader>(
      node.get(), probe::topic(), qos, agnocast::SubscriptionOptions{},
      agnocast::SubscriptionRole::AgnocastOnly);
  }
  if (role != "publisher") probe::attach(reader);
  if (role == "publisher") {
    probe::Publisher writer(
      node.get(), probe::topic(), qos, {}, agnocast::PublisherRole::AgnocastOnly);
    auto pub = node->create_publisher<Cloud>("probe_cloud", 4);
    Cloud::ConstSharedPtr reply;
    rclcpp::SubscriptionOptions native_only;
    native_only.acceptable_buffer_backends = "agnocast_probe";
    auto reply_sub = node->create_subscription<Cloud>(
      "probe_reply", 1, [&](Cloud::ConstSharedPtr msg) { reply = std::move(msg); }, native_only);
    rclcpp::executors::SingleThreadedExecutor executor;
    executor.add_node(node);
    wait_for([&] { return pub->get_subscription_count() == 8; }, "eight ROS readers");
    for (const auto * r :
         {"fast", "delayed", "cpu", "stale", "drop", "shmem", "decline", "capacity"})
      await(r);
    auto send = [&](uint8_t seed) {
      auto loan = writer.borrow_loaned_message();
      loan->resize(size);
      for (size_t i = 0; i < size; ++i) (*loan)[i] = static_cast<uint8_t>(seed + i);
      writer.publish(std::move(loan));
    };
    auto publish_cloud = [&] {
      Cloud msg;
      msg.width = size;
      msg.height = 1;
      msg.row_step = size;
      msg.point_step = 1;
      msg.data = probe::wrap(reader->take());
      pub->publish(msg);
    };
    send(1);
    publish_cloud();
    mark("released");
    await("fast_held");
    await("delayed_held");
    await("cpu_verified");
    await("decline_verified");
    wait_for(
      [&] {
        executor.spin_some();
        return bool(reply);
      },
      "POSIX Buffer reply");
    require(reply->data.get_backend_type() == "cpu", "expected CPU fallback on POSIX reply");
    verify(static_cast<const std::vector<uint8_t> &>(reply->data));
    std::cout << "publisher: verified POSIX-to-Agnocast-only subscription CPU fallback\n";
    send(2);  // Replaces the single-entry native history.
    require(!probe::reclaimed(1), "live loan reclaimed");
    publish_cloud();
    mark("replaced");
    await("fast_verified");
    await("stale_verified");
    await("drop_verified");
    await("reader_killed");
    wait_for(
      [&] {
        send(3);
        return probe::reclaimed(1) && probe::reclaimed(2);
      },
      "reclaim after reader death");
    std::cout << "publisher: reclaimed acquired and pending loans after reader death\n";
    for (uint8_t seed = 4; seed <= 20; ++seed) {
      send(seed);
      publish_cloud();
    }
    mark("saturated");
    await("capacity_drained");
    send(21);
    publish_cloud();
    await("capacity_recovered");
  } else {
    rclcpp::SubscriptionOptions options;
    if (role == "shmem")
      options.acceptable_buffer_backends = "shmem";
    else if (role != "cpu")
      options.acceptable_buffer_backends = "agnocast_probe";
    rclcpp::Publisher<Cloud>::SharedPtr return_pub;
    if (role == "shmem") return_pub = node->create_publisher<Cloud>("probe_reply", 1);
    std::atomic_size_t arrivals{0};
    uint8_t sequence = 0;
    Cloud::ConstSharedPtr held;
    size_t depth = role == "capacity" ? 32 : 4;
    if (role == "drop") depth = 1;
    auto sub = node->create_subscription<Cloud>(
      "probe_cloud", depth,
      [&](Cloud::ConstSharedPtr msg) {
        if (role == "capacity") {
          ++sequence;
          auto seed = sequence <= 2 ? sequence : sequence + 1;
          if (seed == 20) {
            require(msg->data.get_backend_type() == "cpu", "saturated peer expected CPU fallback");
            verify(static_cast<const std::vector<uint8_t> &>(msg->data), seed);
          } else {
            verify(probe::view(msg->data), seed);
          }
          return;
        }
        if (role == "stale") {
          verify(probe::view(msg->data), ++sequence);
        }
        held = std::move(msg);
      },
      options);
    if (role == "drop") {
      sub->set_on_new_message_callback([&](size_t count) { arrivals.fetch_add(count); });
    }
    rclcpp::executors::SingleThreadedExecutor executor;
    executor.add_node(node);
    mark(role);
    await("released");
    if (role == "capacity") {
      auto receive_until = [&](uint8_t count) {
        wait_for(
          [&] {
            executor.spin_some();
            return sequence == count;
          },
          "capacity sample sequence");
      };
      receive_until(2);
      await("saturated");
      receive_until(19);
      mark("capacity_drained");
      receive_until(20);
      mark("capacity_recovered");
      std::cout << "capacity: verified 16 native reservations, CPU overflow and native recovery\n";
      probe::attach(nullptr);
      return;
    }
    if (role == "delayed") await("fast_held");
    if (role == "drop") {
      await("replaced");
      wait_for([&] { return arrivals.load() >= 2; }, "two Zenoh arrivals at depth one");
      wait_for(
        [&] {
          executor.spin_some();
          return bool(held);
        },
        "newest sample after queue drop");
      verify(probe::view(held->data), 2);
      mark("drop_verified");
      std::cout << "drop: KeepLast(1) received and verified the replacement sample\n";
      probe::attach(nullptr);
      return;
    }
    if (role == "stale") {
      await("replaced");
      wait_for(
        [&] {
          executor.spin_some();
          return sequence == 2;
        },
        "both retained generations in order");
      mark("stale_verified");
      std::cout << "stale: verified both native generations after history advanced\n";
      probe::attach(nullptr);
      return;
    }
    wait_for(
      [&] {
        executor.spin_some();
        return bool(held);
      },
      "ROS payload " + role);
    if (role == "shmem") {
      require(held->data.get_backend_type() == "cpu", "expected CPU fallback from Agnocast");
      auto promoted = shmem_buffer::from_input_buffer(held->data);
      verify({promoted.get_ptr(), held->data.size()});
      Cloud reply;
      reply.width = size;
      reply.height = 1;
      reply.point_step = 1;
      reply.row_step = size;
      reply.data = shmem_buffer::allocate_buffer(size);
      {
        auto write = shmem_buffer::from_output_buffer(reply.data);
        std::copy_n(promoted.get_ptr(), size, write.get_ptr());
      }
      wait_for([&] { return return_pub->get_subscription_count() == 1; }, "Agnocast-only reader");
      return_pub->publish(reply);
      await("replaced");
      std::cout
        << "shmem: verified Agnocast-to-POSIX-only subscription via CPU and issued POSIX reply\n";
    } else if (role == "cpu" || role == "decline") {
      require(held->data.get_backend_type() == "cpu", "missing CPU fallback");
      verify(static_cast<const std::vector<uint8_t> &>(held->data));
      mark(role + "_verified");
    } else {
      verify(probe::view(held->data));
      mark(role + "_held");
      await("replaced");
      verify(probe::view(held->data));
      if (role == "delayed") {
        mark("kill_ready");
        for (;;) std::this_thread::sleep_for(1s);
      }
      held.reset();
      mark("fast_verified");
    }
    std::cout << role << ": verified all payload bytes\n";
  }
  probe::attach(nullptr);
}
int main(int argc, char ** argv)
{
  try {
    if (argc != 2 || probe::topic().empty())
      throw std::runtime_error("expected role and native topic");
    rclcpp::init(argc, argv);
    run(argv[1]);
    rclcpp::shutdown();
  } catch (const std::exception & e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
