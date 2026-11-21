#include <rclcpp/rclcpp.hpp>
#ifdef SYNC_NATIVE
#include "native_cloud.hpp"
#include "native_executor.hpp"

#endif

#include <autoware_sensing_msgs/msg/concatenated_point_cloud_info.hpp>
#include <sensor_msgs/msg/point_cloud2.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <thread>
#include <tuple>
#include <vector>

using Cloud = sensor_msgs::msg::PointCloud2;
using Info = autoware_sensing_msgs::msg::ConcatenatedPointCloudInfo;
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
const bool extended = std::getenv("PIPELINE_DIR") && std::getenv("SYNC_EXTENDED");
const int sets = extended ? 104 : 24;
const int points = std::getenv("SYNC_LARGE") ? 32768 : 1024;
const std::array<std::string, 3> topics{"/sync/left", "/sync/right", "/sync/top"};
struct Point
{
  float x, y, z;
  uint8_t intensity, type;
  uint16_t channel;
};
static_assert(sizeof(Point) == 16);

void check(bool ok, const char * message)
{
  if (!ok) throw std::runtime_error(message);
}
Point point(int seq, int sensor, int index)
{
  return {float(index), float(seq % 24), float(sensor),
          uint8_t(seq), uint8_t(sensor), uint16_t(index)};
}
Cloud cloud(int seq, int sensor)
{
  Cloud c;
  c.header.frame_id = sensor ? "sync_lidar" : "base_link";
  c.width = points;
  c.height = 1;
  c.is_dense = true;
  c.point_step = sizeof(Point);
  if (sensor == 2 && std::getenv("SYNC_STRIDED_INPUT")) {
    c.height = 2;
    c.width = points / c.height;
    c.point_step += 4;
  }
  c.row_step = c.width * c.point_step + (c.height == 2 ? 8 : 0);
  using Field = sensor_msgs::msg::PointField;
  for (auto [name, offset, type] :
       {std::tuple{"x", 0, Field::FLOAT32},
        {"y", 4, Field::FLOAT32},
        {"z", 8, Field::FLOAT32},
        {"intensity", 12, Field::UINT8},
        {"return_type", 13, Field::UINT8},
        {"channel", 14, Field::UINT16}}) {
    Field f;
    f.name = name;
    f.offset = offset;
    f.datatype = type;
    f.count = 1;
    c.fields.push_back(f);
  }
  c.data.resize(size_t(c.height) * c.row_step);
  for (int i = 0; i < points; ++i) {
    auto p = point(seq, sensor, i);
    const auto offset = (i / c.width) * c.row_step + (i % c.width) * c.point_step;
    std::memcpy(c.data.data() + offset, &p, sizeof(p));
  }
  return c;
}
// Separate source process for the launch-driven crop and synchronizer graph.
int pipeline_source(rclcpp::Node::SharedPtr node, const std::filesystem::path & directory)
{
  auto qos = rclcpp::SensorDataQoS().keep_last(32);
#ifdef SYNC_NATIVE
  std::vector<agnocast::Publisher<Cloud>::SharedPtr> pubs;
#else
  std::vector<rclcpp::Publisher<Cloud>::SharedPtr> pubs;
#endif
  std::vector<std::vector<Cloud>> inputs(sets);
  for (const auto & topic : topics)
#ifdef SYNC_NATIVE
    pubs.push_back(agnocast::create_publisher<Cloud>(node.get(), "/raw" + topic, qos));
#else
    pubs.push_back(node->create_publisher<Cloud>("/raw" + topic, qos));
#endif
  for (int seq = 0; !extended && seq < sets; ++seq) {
    for (int sensor = 0; sensor < (seq % 2 ? 2 : 3); ++sensor)
      inputs[seq].push_back(cloud(seq, sensor));
  }
  const auto deadline = Clock::now() + 20s;
  auto matched = [&] {
    return std::filesystem::exists(directory / "sink-ready") &&
           std::all_of(
             pubs.begin(), pubs.end(),
             [](const auto & p) { return p->get_subscription_count() > 0; }) &&
           std::all_of(topics.begin(), topics.end(), [&](const auto & t) {
#ifdef SYNC_NATIVE
             return agnocast::get_publisher_count_core(t) &&
                    agnocast::get_subscription_count_core(t);
#else
             return node->count_publishers(t) && node->count_subscribers(t);
#endif
           });
  };
  while (!matched()) {
    check(Clock::now() < deadline, "pipeline discovery timeout");
    std::this_thread::sleep_for(20ms);
  }
#ifdef SYNC_NATIVE
  for (const auto & t :
       {"/raw/sync/left", "/raw/sync/right", "/raw/sync/top", "/sync/left", "/sync/right",
        "/sync/top", "/output"}) {
    const auto writers = agnocast::get_publisher_count_core(t);
    const auto readers = agnocast::get_subscription_count_core(t);
    check(writers && readers, "native cloud endpoints missing");
    std::cout << "native_route " << t << " writers=" << writers << " readers=" << readers
              << std::endl;
  }
#endif
  std::this_thread::sleep_for(500ms);
  const bool jitter = std::getenv("SYNC_TOP_JITTER") != nullptr;
  const auto start = Clock::now();
  std::vector<int64_t> top_release_ns(sets);
  std::vector<int64_t> stamps(sets);
  for (int seq = 0; seq < sets; ++seq) {
    if (extended) {
      for (int sensor = 0; sensor < (seq % 2 ? 2 : 3); ++sensor)
        inputs[seq].push_back(cloud(seq, sensor));
    }
    std::this_thread::sleep_until(start + seq * 100ms);
    const auto group_start = Clock::now();
    stamps[seq] = node->now().nanoseconds();
    for (size_t sensor = 0; sensor < inputs[seq].size(); ++sensor) {
      inputs[seq][sensor].header.stamp = rclcpp::Time(stamps[seq] + int64_t(sensor) * 1000000);
      if (sensor == 2) {
        if (jitter) std::this_thread::sleep_until(group_start + (seq % 4 == 0 ? 15ms : 50ms));
        top_release_ns[seq] = node->now().nanoseconds() - stamps[seq];
      }
#ifdef SYNC_NATIVE
      native_sync::publish(*pubs[sensor], inputs[seq][sensor]);
#else
      pubs[sensor]->publish(inputs[seq][sensor]);
#endif
    }
    if (extended) inputs[seq].clear();
  }
  for (int seq = 0; seq < sets; seq += 2)
    std::cout << "top_release_ns," << seq << "," << top_release_ns[seq] << std::endl;
  std::ofstream out(directory / "stamps.tmp");
  out.exceptions(std::ios::badbit | std::ios::failbit);
  for (auto stamp : stamps) out << stamp << '\n';
  out.close();
  std::filesystem::rename(directory / "stamps.tmp", directory / "stamps");
  rclcpp::spin(node);
  return 0;
}
int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  auto node = std::make_shared<rclcpp::Node>("sync_fixture");
  const char * directory = std::getenv("PIPELINE_DIR");
  if (directory && std::getenv("PIPELINE_SOURCE")) return pipeline_source(node, directory);
  const bool jitter = directory && std::getenv("SYNC_TOP_JITTER");
  const int expected_outputs = jitter ? sets + sets / 4 : sets;
  const int segment_points = directory ? points / 2 : points;
  auto qos = rclcpp::SensorDataQoS().keep_last(32);
  std::map<int64_t, Cloud::ConstSharedPtr> clouds;
  std::map<int64_t, int64_t> arrival;
  std::map<int64_t, Info::ConstSharedPtr> infos;
  const bool slow = directory && std::getenv("SYNC_SLOW_SINK");
  std::atomic<size_t> cloud_count{0}, info_count{0};
  int64_t resume_ns = 0;
  int delayed = 0;
  auto receive = [&](Cloud::ConstSharedPtr c) {
    const auto stamp = rclcpp::Time(c->header.stamp).nanoseconds();
    arrival[stamp] = node->now().nanoseconds() - stamp;
    if (slow && (!extended || delayed < 68)) {
      std::this_thread::sleep_for(125ms);
      if (++delayed == 68 && extended) resume_ns = node->now().nanoseconds();
    }
    if (clouds.empty()) {
      if (const char * path = std::getenv("SYNC_READY_FILE")) std::ofstream(path) << "ready";
    }
    check(clouds.emplace(rclcpp::Time(c->header.stamp).nanoseconds(), c).second, "duplicate cloud");
    ++cloud_count;
  };
#ifdef SYNC_NATIVE
  auto sub =
    agnocast::create_subscription<Cloud>(node.get(), "/output", qos, [&](native_sync::Loan loan) {
      if (directory)
        receive(native_sync::cpu_copy(std::move(loan)));
      else
        receive(native_sync::retain(std::move(loan)));
    });
#else
  auto sub = node->create_subscription<Cloud>("/output", qos, receive);
#endif
  auto status = node->create_subscription<Info>("/output_info", qos, [&](Info::ConstSharedPtr m) {
    check(infos.emplace(rclcpp::Time(m->header.stamp).nanoseconds(), m).second, "duplicate info");
    ++info_count;
  });
#ifdef SYNC_NATIVE
  std::vector<agnocast::Publisher<Cloud>::SharedPtr> pubs;
  if (!directory)
    for (const auto & topic : topics)
      pubs.push_back(agnocast::create_publisher<Cloud>(node.get(), topic, qos));
  native_sync::Executor executor;
  executor.add_node(node);
  std::thread worker([&] { executor.spin(); });
  auto spin_until = [](Clock::time_point end) { std::this_thread::sleep_until(end); };
#else
  std::vector<rclcpp::Publisher<Cloud>::SharedPtr> pubs;
  if (!directory) {
    for (const auto & topic : topics) pubs.push_back(node->create_publisher<Cloud>(topic, qos));
  }
  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(node);
  auto spin_until = [&](Clock::time_point end) {
    while (true) {
      auto remaining = end - Clock::now();
      if (remaining <= Clock::duration::zero()) break;
      executor.spin_once(std::chrono::duration_cast<std::chrono::nanoseconds>(remaining));
    }
  };
#endif
  auto ready = [&] {
    return sub->get_publisher_count() && status->get_publisher_count() &&
           std::all_of(
             pubs.begin(), pubs.end(), [](auto & p) { return p->get_subscription_count(); });
  };
  auto deadline = Clock::now() + 10s;
  while (!ready() && Clock::now() < deadline) spin_until(Clock::now() + 20ms);
  check(ready(), "endpoints failed to match");
  spin_until(Clock::now() + 500ms);
  std::vector<int64_t> stamps;
  if (directory) {
    const auto path = std::filesystem::path(directory);
    std::ofstream(path / "sink-ready") << "ready";
    const auto end = Clock::now() + 25s;
    while (Clock::now() < end && ((!extended && (cloud_count.load() < size_t(expected_outputs) ||
                                                 info_count.load() < size_t(expected_outputs))) ||
                                  !std::filesystem::exists(path / "stamps")))
      spin_until(Clock::now() + 20ms);
    std::ifstream in(path / "stamps");
    for (int64_t stamp; in >> stamp;) stamps.push_back(stamp);
    check(stamps.size() == sets, "missing source timestamps");
    spin_until(Clock::now() + (extended ? 500ms : 100ms));
  } else {
    std::vector<std::vector<Cloud>> payloads(sets);
    for (int seq = 0; seq < sets; ++seq) {
      for (int sensor = 0; sensor < (seq % 2 ? 2 : 3); ++sensor)
        payloads[seq].push_back(cloud(seq, sensor));
    }
    auto start = Clock::now();
    for (int seq = 0; seq < sets; ++seq) {
      auto & inputs = payloads[seq];
      spin_until(start + seq * 100ms);
      stamps.push_back(node->now().nanoseconds());
      // Two 10 Hz streams and one 5 Hz stream, with a 40 ms collector timeout.
      for (size_t sensor = 0; sensor < inputs.size(); ++sensor) {
        inputs[sensor].header.stamp = rclcpp::Time(stamps.back() + int64_t(sensor) * 1000000);
#ifdef SYNC_NATIVE
        native_sync::publish(*pubs[sensor], inputs[sensor]);
#else
        pubs[sensor]->publish(inputs[sensor]);
#endif
      }
    }
    spin_until(Clock::now() + 500ms);
  }
#ifdef SYNC_NATIVE
  executor.cancel();
  worker.join();
#endif
  if (!extended || !slow)
    check(
      clouds.size() == size_t(expected_outputs) && infos.size() == size_t(expected_outputs),
      "unexpected number of clouds or metadata records");
  size_t counted_clouds = 0, counted_infos = 0, paired = 0;
  auto verify_group = [&](int seq, int first, int count, const char * kind) {
    const auto stamp = stamps[seq] + int64_t(first) * 1000000;
    const bool has_cloud = clouds.count(stamp), has_info = infos.count(stamp);
    counted_clouds += has_cloud;
    counted_infos += has_info;
    paired += has_cloud && has_info;
    if (extended)
      std::cout << "delivery," << seq << "," << kind << "," << has_cloud << "," << has_info << ","
                << stamp << "," << (has_cloud ? arrival.at(stamp) : -1) << "\n";
    check((has_cloud && has_info) || (extended && slow && seq < sets - 12), "missing group");
    std::map<int, unsigned> offsets;
    if (has_cloud) {
      const auto & c = *clouds.at(stamp);
      const auto & bytes = static_cast<const std::vector<uint8_t> &>(c.data);
      if (c.width != unsigned(count * segment_points))
        std::cerr << "width mismatch group=" << seq << " kind=" << kind
                  << " expected=" << count * segment_points << " received=" << c.width << std::endl;
      check(
        c.header.frame_id == "base_link" && c.width == count * segment_points && c.height == 1 &&
          c.point_step == sizeof(Point) && c.row_step == bytes.size() &&
          bytes.size() == c.width * sizeof(Point),
        "invalid cloud frame or shape");
      for (int part = 0; part < count; ++part) {
        unsigned offset = part * segment_points;
        Point prefix;
        std::memcpy(&prefix, bytes.data() + offset * sizeof(Point), sizeof(Point));
        int sensor = prefix.type;
        check(
          sensor >= first && sensor < first + count && offsets.emplace(sensor, offset).second,
          "wrong cloud member");
        for (int i = 0; i < segment_points; ++i) {
          auto expected = point(seq, sensor, i + (directory ? sensor * points / 4 : 0));
          if (sensor) expected.x += 4;
          check(
            !std::memcmp(bytes.data() + (offset + i) * sizeof(Point), &expected, sizeof(Point)),
            "source payload or TF mismatch");
        }
      }
    }
    if (!has_info) return;
    const auto & m = *infos.at(stamp);
    check(
      m.concatenation_success == (count == 3) && m.source_info.size() == 3,
      "invalid collection status");
    std::set<std::string> seen;
    std::set<unsigned> segments;
    for (const auto & source : m.source_info) {
      auto it = std::find(topics.begin(), topics.end(), source.topic);
      check(it != topics.end() && seen.insert(source.topic).second, "unknown or duplicate sensor");
      int sensor = it - topics.begin();
      if (sensor < first || sensor >= first + count) {
        check(
          source.status == source.STATUS_TIMEOUT && source.length == 0, "expected sensor timeout");
        continue;
      }
      check(
        source.status == source.STATUS_OK && source.length == segment_points &&
          source.idx_begin % segment_points == 0 &&
          source.idx_begin / segment_points < unsigned(count) &&
          segments.insert(source.idx_begin).second,
        "invalid source segment");
      check(
        rclcpp::Time(source.header.stamp).nanoseconds() == stamps[seq] + sensor * 1000000,
        "wrong source stamp");
      if (has_cloud) check(offsets.at(sensor) == source.idx_begin, "metadata offset mismatch");
    }
    if (has_cloud) std::cout << "sample," << seq << "," << kind << "," << arrival.at(stamp) << "\n";
  };
  for (int seq = 0; seq < sets; ++seq) {
    if (jitter && seq % 4 == 2) {
      verify_group(seq, 0, 2, "late_pair");
      verify_group(seq, 2, 1, "late_top");
    } else {
      verify_group(seq, 0, seq % 2 ? 2 : 3, seq % 2 ? "partial" : "full");
    }
  }
  std::cout << "fast_source_hz=" << 1e9 * (sets - 1) / (stamps.back() - stamps.front()) << "\n";
  check(counted_clouds == clouds.size() && counted_infos == infos.size(), "unknown stamp");
  if (extended)
    std::cout << "extended expected=" << expected_outputs << " clouds=" << counted_clouds
              << " metadata=" << counted_infos << " paired=" << paired << " resume_ns=" << resume_ns
              << "\n";
  else
    std::cout << "verified " << expected_outputs << " synchronized clouds and metadata, "
              << (jitter ? 6 : 12) << " complete and " << (jitter ? 24 : 12) << " timed out, "
              << 60 * points << " input points\n";
  clouds.clear();
  rclcpp::shutdown();
}
