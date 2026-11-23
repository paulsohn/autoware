#include <geometry_msgs/msg/pose_array.hpp>
#include <nav_msgs/msg/path.hpp>
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <sensor_msgs/msg/point_cloud2.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

using namespace std::chrono_literals;
using PoseArray = geometry_msgs::msg::PoseArray;
using Path = nav_msgs::msg::Path;
using Cloud = sensor_msgs::msg::PointCloud2;
using Image = sensor_msgs::msg::Image;
constexpr unsigned samples = 48;

template<typename Msg>
Msg payload(unsigned seq)
{
  Msg msg;
  msg.header.frame_id = "lidar_" + std::to_string(seq);
  const size_t large = std::is_same_v<Msg, Cloud> && std::getenv("FALLBACK_LARGE_CLOUD") ? 32768 : 1024;
  const size_t count = seq % 8 == 0 ? 0 : seq % 3 == 0 ? 1 : large;
  if constexpr (std::is_same_v<Msg, Path> || std::is_same_v<Msg, PoseArray>) {
    msg.poses.resize(count);
    for (size_t i = 0; i < count; ++i) {
      auto & pose = [&]() -> geometry_msgs::msg::Pose & {
        if constexpr (std::is_same_v<Msg, Path>) {
          msg.poses[i].header.frame_id = "point_" + std::to_string(i);
          return msg.poses[i].pose;
        } else return msg.poses[i];
      }();
      pose.position.x = seq + i;
      pose.orientation.w = 1;
    }
  } else {
    msg.width = count;
    msg.height = 1;
    if constexpr (std::is_same_v<Msg, Cloud>) {
      msg.point_step = 16;
      msg.row_step = count * 16;
      sensor_msgs::msg::PointField field;
      field.name = "x";
      field.count = 1;
      field.datatype = field.FLOAT32;
      msg.fields.push_back(field);
    } else {
      msg.encoding = "rgba8";
      msg.step = count * 4;
    }
    size_t bytes = count * (std::is_same_v<Msg, Cloud> ? 16 : 4);
    std::vector<uint8_t> data(bytes);
    for (size_t i = 0; i < bytes; ++i) data[i] = static_cast<uint8_t>(seq + i);
    msg.data = std::move(data);
  }
  return msg;
}

template<typename Msg>
void run(const std::string & role, const char * topic)
{
  auto node = std::make_shared<rclcpp::Node>("fallback_" + role);
  rclcpp::QoS qos(4);
  if (std::is_same_v<Msg, Cloud> || std::getenv("FALLBACK_SENSOR_QOS")) qos = rclcpp::SensorDataQoS();
  if (role == "writer") {
    auto pub = node->create_publisher<Msg>(topic, qos);
    std::ofstream("writer.ready");
    auto deadline = std::chrono::steady_clock::now() + 15s;
    while (!std::filesystem::exists("go")) {
      if (std::chrono::steady_clock::now() > deadline) throw std::runtime_error("start timeout");
      std::this_thread::sleep_for(5ms);
    }
    for (unsigned i = 0; i < samples; ++i) {
      auto msg = payload<Msg>(i);
      msg.header.stamp = node->now();
      pub->publish(msg);
      std::this_thread::sleep_for(20ms);
    }
    std::this_thread::sleep_for(200ms);
    std::ofstream("finished");
    return;
  }
  unsigned seen = 0;
  std::ofstream timing(role + ".csv");
  auto sub = node->create_subscription<Msg>(topic, qos, [&](const Msg & msg) {
    auto latency = node->now().nanoseconds() - rclcpp::Time(msg.header.stamp).nanoseconds();
    auto expected = payload<Msg>(seen);
    expected.header.stamp = msg.header.stamp;
    if (msg != expected) throw std::runtime_error("payload or sequence mismatch");
    timing << seen++ << ',' << latency << '\n';
  });
  std::ofstream(role + ".ready");
  rclcpp::executors::SingleThreadedExecutor exec;
  exec.add_node(node);
  auto deadline = std::chrono::steady_clock::now() + 18s;
  while (!std::filesystem::exists("finished")) {
    if (std::chrono::steady_clock::now() > deadline) throw std::runtime_error("finish timeout");
    exec.spin_once(10ms);
  }
  exec.spin_some();
  if (seen != (role == "reader" ? samples : 0U)) throw std::runtime_error("delivery count mismatch");
}

int main(int argc, char ** argv)
{
  if (argc != 4) return 2;
  rclcpp::init(argc, argv);
  try {
    std::string type = argv[2];
    if (type == "path") run<Path>(argv[1], argv[3]);
    else if (type == "poses") run<PoseArray>(argv[1], argv[3]);
    else if (type == "cloud") run<Cloud>(argv[1], argv[3]);
    else if (type == "image") run<Image>(argv[1], argv[3]);
    else return 2;
  } catch (const std::exception & e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
  rclcpp::shutdown();
}
