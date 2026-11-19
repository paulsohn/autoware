#include "probe.hpp"

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
  if (role != "cpu") {
    reader = std::make_shared<probe::Reader>(
      node.get(), probe::topic(), qos, agnocast::SubscriptionOptions{},
      agnocast::SubscriptionRole::AgnocastOnly);
  }
  probe::attach(reader.get());
  if (role == "publisher") {
    probe::Publisher writer(
      node.get(), probe::topic(), qos, {}, agnocast::PublisherRole::AgnocastOnly);
    auto pub = node->create_publisher<Cloud>("probe_cloud", 4);
    wait_for([&] { return pub->get_subscription_count() == 5; }, "five ROS readers");
    for (const auto * r : {"fast", "delayed", "cpu", "stale", "drop"}) await(r);
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
    send(2);  // Replaces the single-entry native history.
    require(!probe::first_reclaimed(), "live loan reclaimed");
    publish_cloud();
    mark("replaced");
    await("fast_verified");
    await("stale_rejected");
    await("drop_verified");
    await("reader_killed");
    wait_for(
      [&] {
        send(3);
        return probe::first_reclaimed();
      },
      "reclaim after reader death");
    std::cout << "publisher: original allocation reclaimed after both readers released or exited\n";
  } else {
    rclcpp::SubscriptionOptions options;
    if (role != "cpu") options.acceptable_buffer_backends = "agnocast_probe";
    std::atomic_size_t arrivals{0};
    Cloud::ConstSharedPtr held;
    auto sub = node->create_subscription<Cloud>(
      "probe_cloud", role == "drop" ? 1 : 4,
      [&](Cloud::ConstSharedPtr msg) { held = std::move(msg); }, options);
    if (role == "drop") {
      sub->set_on_new_message_callback([&](size_t count) { arrivals.fetch_add(count); });
    }
    rclcpp::executors::SingleThreadedExecutor executor;
    executor.add_node(node);
    mark(role);
    await("released");
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
          try {
            executor.spin_some();
          } catch (const std::exception &) {
            if (!probe::stale_rejected()) throw;
          }
          require(!held, "unexpected ROS callback for stale descriptor");
          return probe::stale_rejected();
        },
        "stale descriptor rejection");
      mark("stale_rejected");
      std::cout << "stale: delayed ROS import rejected after native history advanced\n";
      probe::attach(nullptr);
      return;
    }
    wait_for(
      [&] {
        executor.spin_some();
        return bool(held);
      },
      "ROS payload " + role);
    if (role == "cpu") {
      require(held->data.get_backend_type() == "cpu", "missing CPU fallback");
      verify(static_cast<const std::vector<uint8_t> &>(held->data));
      mark("cpu_verified");
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
