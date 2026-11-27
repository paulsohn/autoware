#include <rclcpp/rclcpp.hpp>

#include <sensor_msgs/msg/point_cloud2.hpp>

#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;
using Cloud = sensor_msgs::msg::PointCloud2;
constexpr int payload = 65536;
constexpr int control = 24, stalled = 272, cycles = 2;

void mark(const std::string & name)
{
  std::ofstream(name).put('1');
}
template <class F>
void wait_for(F ok)
{
  auto end = Clock::now() + 45s;
  while (!ok()) {
    if (Clock::now() > end) throw std::runtime_error("phase timeout");
    std::this_thread::sleep_for(250us);
  }
}
void await(const std::string & name)
{
  wait_for([&] { return std::filesystem::exists(name); });
}
int64_t ns()
{
  return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch())
    .count();
}

void writer(rclcpp::Node & node, int hz)
{
  auto pub = node.create_publisher<Cloud>("/pressure", rclcpp::QoS(8));
  auto pool_size = [](int pid) {
    struct stat st{};
    if (stat(("/dev/shm/agnocast@" + std::to_string(pid)).c_str(), &st))
      throw std::runtime_error("missing endpoint pool");
    return st.st_blocks * 512;
  };
  auto memory = [&](int cycle, const char * phase) {
    std::cout << "pool," << cycle << "," << phase << "," << pool_size(getpid()) << "\n";
    for (const char * role : {"slow", "fast"}) {
      int pid;
      if (!(std::ifstream(std::string(role) + ".pid") >> pid))
        throw std::runtime_error("missing reader pid");
      std::cout << role << "_pool," << cycle << "," << phase << "," << pool_size(pid) << "\n";
    }
  };
  wait_for([&] { return pub->get_subscription_count() == 2; });
  Cloud cloud;
  cloud.header.frame_id = "lidar";
  cloud.width = payload / 4;
  cloud.height = 1;
  cloud.point_step = 4;
  cloud.row_step = payload;
  cloud.fields.resize(1);
  cloud.fields[0].name = "intensity";
  cloud.fields[0].datatype = sensor_msgs::msg::PointField::UINT32;
  cloud.fields[0].count = 1;
  cloud.data.resize(payload);
  auto & bytes = static_cast<std::vector<uint8_t> &>(cloud.data);
  for (int cycle = 0; cycle < cycles; ++cycle) {
    auto id = std::to_string(cycle);
    auto send = [&](int offset, int count, const char * phase) {
      auto start = Clock::now();
      for (int i = 0; i < count; ++i) {
        std::this_thread::sleep_until(start + std::chrono::nanoseconds(1000000000LL * i / hz));
        int seq = cycle * (control + stalled + 1) + offset + i;
        std::fill(bytes.begin(), bytes.end(), static_cast<uint8_t>(seq));
        std::memcpy(bytes.data(), &seq, sizeof(seq));
        auto t = ns();
        cloud.header.stamp = rclcpp::Time(t);
        const auto begin_publish = ns();
        pub->publish(cloud);
        const auto elapsed = ns() - begin_publish;
        std::cout << "publish," << seq << "," << phase << "," << elapsed << "," << t << "\n";
      }
    };
    send(0, control, "control");
    await("paused" + id);
    memory(cycle, "before");
    send(control, stalled, "stalled");
    std::this_thread::sleep_for(150ms);
    memory(cycle, "full");
    mark("resume" + id);
    await("drained" + id);
    memory(cycle, "drained");
    send(control + stalled, 1, "recovered");
    await("slow" + id);
    await("fast" + id);
  }
}

void reader(rclcpp::Node & node, bool slow, bool all)
{
  auto qos = rclcpp::QoS(8);
  if (slow && all) qos.keep_all();
  auto sub = node.create_subscription<Cloud>("/pressure", qos, [](Cloud::ConstSharedPtr) {});
  auto handle = rcl_subscription_get_rmw_handle(sub->get_subscription_handle().get());
  rmw_event_t event = rmw_get_zero_initialized_event();
  if (rmw_subscription_event_init(&event, handle, RMW_EVENT_MESSAGE_LOST) != RMW_RET_OK)
    throw std::runtime_error("loss event unavailable");
  Cloud msg;
  rclcpp::MessageInfo info;
  int seq = -1;
  auto take = [&] {
    if (!sub->take(msg, info)) return false;
    auto received = ns();
    auto & bytes = static_cast<const std::vector<uint8_t> &>(msg.data);
    if (bytes.size() != payload) throw std::runtime_error("payload size");
    std::memcpy(&seq, bytes.data(), sizeof(seq));
    if (!std::all_of(bytes.begin() + sizeof(seq), bytes.end(), [&](auto b) {
          return b == static_cast<uint8_t>(seq);
        }))
      throw std::runtime_error("payload contents");
    std::cout << "take," << seq << "," << received - rclcpp::Time(msg.header.stamp).nanoseconds()
              << "\n";
    return true;
  };
  for (int cycle = 0; cycle < cycles; ++cycle) {
    auto id = std::to_string(cycle);
    int start = cycle * (control + stalled + 1);
    // Break on the terminal sequence so gaps remain measurable in the output.
    if (slow) {
      wait_for([&] { return take() && seq == start + control - 1; });
      mark("paused" + id);
      await("resume" + id);
      while (take()) {
      }
      mark("drained" + id);
    }
    wait_for([&] { return take() && seq == start + control + stalled; });
    rmw_message_lost_status_t loss{};
    bool taken = false;
    if (rmw_take_event(&event, &loss, &taken) != RMW_RET_OK || !taken)
      throw std::runtime_error("status take");
    std::cout << "loss," << cycle << "," << loss.total_count_change << "\n";
    mark(std::string(slow ? "slow" : "fast") + id);
  }
  if (rmw_event_fini(&event) != RMW_RET_OK) throw std::runtime_error("event cleanup");
}

int main(int argc, char ** argv)
{
  if (argc != 4) return 2;
  try {
    rclcpp::init(argc, argv);
    std::string role = argv[1];
    auto node = std::make_shared<rclcpp::Node>(
      "pressure_" + role, rclcpp::NodeOptions().start_parameter_services(false));
    if (role == "writer")
      writer(*node, std::stoi(argv[2]));
    else
      reader(*node, role == "slow", std::string(argv[3]) == "all");
    node.reset();
    rclcpp::shutdown();
  } catch (const std::exception & e) {
    std::cerr << e.what() << "\n";
    return 1;
  }
}
