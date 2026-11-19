#include <autoware/agnocast_wrapper/node.hpp>
#include <rclcpp_components/register_node_macro.hpp>
#include <sensor_msgs/msg/point_cloud2.hpp>

#ifdef UNIVERSE_BUFFER_MATRIX
#include "buffer_io.hpp"
#endif

#include <array>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>

namespace benchmark {
using Cloud = sensor_msgs::msg::PointCloud2;
constexpr unsigned points = 32768;
constexpr unsigned frames = 60;
struct Point {
  float x, y, z;
  uint8_t intensity, return_type;
  uint16_t channel;
};
static_assert(sizeof(Point) == 16);

Point sample(unsigned index, unsigned frame) {
  return {index % 2 ? 2.0f : 0.5f, static_cast<float>(index % 4) / 8,
    static_cast<float>(index % 8) / 16, static_cast<uint8_t>(frame), 1,
    static_cast<uint16_t>(index)};
}

class Endpoint : public autoware::agnocast_wrapper::Node {
public:
  explicit Endpoint(const rclcpp::NodeOptions &options)
  : Node(std::string("bench_") + std::getenv("BENCH_ROLE"), options) {
#ifndef UNIVERSE_BUFFER_MATRIX
    const char * backend = std::getenv("BENCH_BUFFER");
    if (backend && std::string(backend) != "cpu")
      throw std::runtime_error("explicit buffer case requires UNIVERSE_BUFFER_MATRIX");
#endif
    if (std::string(std::getenv("BENCH_ROLE")) == "pub") {
      pub_ = create_publisher<Cloud>("input", rclcpp::SensorDataQoS());
      timer_ = create_wall_timer(std::chrono::milliseconds(100), [this] { send(); });
    } else {
      csv_.open(std::getenv("BENCH_CSV"));
      if (!csv_) throw std::runtime_error("could not open sample log");
#ifdef UNIVERSE_BUFFER_MATRIX
      if (buffer_backend() != "cpu") {
        rclcpp::SubscriptionOptions options;
        options.acceptable_buffer_backends = buffer_backend();
        buffer_sub_ = get_rclcpp_node()->create_subscription<Cloud>("output", rclcpp::SensorDataQoS(),
          [this](const Cloud &msg) { receive(msg); }, options);
        return;
      }
#endif
      sub_ = create_subscription<Cloud>("output", rclcpp::SensorDataQoS(),
        [this](const Cloud &msg) { receive(msg); });
    }
  }

private:
  void send() {
    if (frame_ >= frames || !pub_->get_subscription_count()) return;
    auto msg = pub_->allocate_output_message_unique();
    msg->header.frame_id = "base_link";
    msg->width = points;
    msg->height = 1;
    msg->point_step = sizeof(Point);
    msg->row_step = points * sizeof(Point);
    msg->is_dense = true;
    constexpr std::array<const char *, 6> names{"x", "y", "z", "intensity", "return_type", "channel"};
    constexpr std::array<unsigned, 6> offsets{0, 4, 8, 12, 13, 14};
    for (unsigned i = 0; i < names.size(); ++i) {
      sensor_msgs::msg::PointField f;
      f.name = names[i];
      f.offset = offsets[i];
      f.datatype = i < 3 ? f.FLOAT32 : (i == 5 ? f.UINT16 : f.UINT8);
      f.count = 1;
      msg->fields.push_back(f);
    }
    msg->data.resize(msg->row_step);
    for (unsigned i = 0; i < points; ++i) {
      const auto point = sample(i, frame_);
      std::memcpy(msg->data.data() + i * sizeof(Point), &point, sizeof(Point));
    }
#ifdef UNIVERSE_BUFFER_MATRIX
    prepare_output(msg->data);
#endif
    msg->header.stamp = now();
    pub_->publish(std::move(msg));
    ++frame_;
  }

  void receive(const Cloud &msg) {
    const auto latency = (now() - rclcpp::Time(msg.header.stamp)).nanoseconds();
#ifdef UNIVERSE_BUFFER_MATRIX
    InputBytes view(msg.data);
    const auto data = view.bytes;
#else
    const std::vector<uint8_t> & data = msg.data;
#endif
    if (latency <= 0 || msg.header.frame_id != "base_link" || msg.width != points / 2 ||
      msg.point_step != sizeof(Point) || msg.height != 1 || msg.fields.size() != 6 ||
      msg.row_step != msg.width * sizeof(Point) || data.size() != msg.row_step)
      throw std::runtime_error("invalid crop output");
    for (unsigned i = 0; i < msg.width; ++i) {
      const auto point = sample(i * 2, frame_);
      if (std::memcmp(data.data() + i * sizeof(Point), &point, sizeof(Point)))
        throw std::runtime_error("point, order, or frame mismatch");
    }
    csv_ << frame_++ << ',' << latency << std::endl;
  }

  unsigned frame_ = 0;
  AUTOWARE_PUBLISHER_PTR(Cloud) pub_;
  AUTOWARE_SUBSCRIPTION_PTR(Cloud) sub_;
  AUTOWARE_TIMER_PTR timer_;
#ifdef UNIVERSE_BUFFER_MATRIX
  rclcpp::Subscription<Cloud>::SharedPtr buffer_sub_;
#endif
  std::ofstream csv_;
};
}  // namespace benchmark
RCLCPP_COMPONENTS_REGISTER_NODE(benchmark::Endpoint)
