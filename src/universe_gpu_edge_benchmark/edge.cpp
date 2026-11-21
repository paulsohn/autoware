#include <autoware/cuda_pointcloud_preprocessor/cuda_concatenate_data/cuda_combine_cloud_handler_kernel.hpp>
#include <cuda_blackboard/cuda_blackboard_publisher.hpp>
#include <cuda_blackboard/cuda_blackboard_subscriber.hpp>
#include <cuda_blackboard/cuda_pointcloud2.hpp>
#include <cuda_buffer/cuda_buffer_api.hpp>
#include <rclcpp/rclcpp.hpp>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
#include <vector>

using namespace std::chrono_literals;
namespace aw = autoware::pointcloud_preprocessor;
namespace bb = cuda_blackboard;
using Point = aw::PointTypeStruct;
using Cloud = sensor_msgs::msg::PointCloud2;
using DeviceCloud = bb::CudaPointCloud2;
using Clock = std::chrono::steady_clock;
constexpr int points = 32768, frames = 24;
constexpr size_t bytes = points * sizeof(Point);
static_assert(sizeof(Point) == 16);

void require(bool ok, const std::string & why)
{
  if (!ok) throw std::runtime_error(why);
}
void gpu(cudaError_t error)
{
  require(error == cudaSuccess, cudaGetErrorString(error));
}
Point expected(int i, int shift)
{
  return {float(i + shift), float(i % 17), float(i % 13), uint8_t(i), uint8_t(i % 4), uint16_t(i)};
}
void transform(const void * src, void * dst, int shift, cudaStream_t & stream)
{
  aw::transform_launch(
    static_cast<const Point *>(src), points, {float(shift), 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1},
    static_cast<Point *>(dst), stream);
}
int64_t now_ns()
{
  return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch())
    .count();
}
void metadata(Cloud & msg, int seq, int64_t start)
{
  msg.header.stamp.sec = start / 1000000000;
  msg.header.stamp.nanosec = start % 1000000000;
  msg.header.frame_id = "lidar_" + std::to_string(seq);
  msg.height = 1;
  msg.width = points;
  msg.point_step = sizeof(Point);
  msg.row_step = bytes;
  using F = sensor_msgs::msg::PointField;
  for (const auto & [name, offset, type] : std::vector<std::tuple<std::string, int, int>>{
         {"x", 0, F::FLOAT32},
         {"y", 4, F::FLOAT32},
         {"z", 8, F::FLOAT32},
         {"intensity", 12, F::UINT8},
         {"return_type", 13, F::UINT8},
         {"channel", 14, F::UINT16}}) {
    F f;
    f.name = name;
    f.offset = offset;
    f.datatype = type;
    f.count = 1;
    msg.fields.push_back(f);
  }
}

int main(int argc, char ** argv)
try {
  require(argc == 3, "edge blackboard|intra|composed|pub|sub|cpu peer_count");
  const std::string mode = argv[1];
  const int peers = std::stoi(argv[2]);
  const bool board = mode == "blackboard", cpu = mode == "cpu";
  const bool both = board || mode == "composed" || mode == "intra";
  const bool send = both || mode == "pub", receive = both || cpu || mode == "sub";
  require(send || receive, "unknown mode");
  rclcpp::init(argc, argv);
  rclcpp::executors::SingleThreadedExecutor exec;
  auto opts = rclcpp::NodeOptions().use_intra_process_comms(mode == "intra");
  auto tx = rclcpp::Node::make_shared("edge_sender", opts);
  auto rx = rclcpp::Node::make_shared("edge_" + mode, opts);
  if (send) exec.add_node(tx);
  if (receive) exec.add_node(rx);
  cudaStream_t writer{}, reader{};
  Point *input{}, *output{};
  std::vector<Point> host(points);
  if (send) {
    gpu(cudaStreamCreateWithFlags(&writer, cudaStreamNonBlocking));
    gpu(cudaMalloc(&input, bytes));
    for (int i = 0; i < points; ++i) host[i] = expected(i, 0);
    gpu(cudaMemcpy(input, host.data(), bytes, cudaMemcpyHostToDevice));
  }
  if (receive && !cpu) {
    gpu(cudaStreamCreateWithFlags(&reader, cudaStreamNonBlocking));
    gpu(cudaMalloc(&output, bytes));
  }
  int received = 0, sent = 0;
  std::vector<double> samples;
  void * published_address{};
  auto consume = [&](const Cloud & m, const void * data) {
    const int seq = ++received;
    require(
      m.header.frame_id == "lidar_" + std::to_string(seq) && m.row_step == bytes &&
        m.width == points && m.fields.size() == 6,
      "sequence or layout mismatch");
    if (board || mode == "intra") require(data == published_address, "device address changed");
    if (!cpu) {
      transform(data, output, 3, reader);
      gpu(cudaStreamSynchronize(reader));
    }
    const auto elapsed =
      now_ns() - (int64_t(m.header.stamp.sec) * 1000000000 + m.header.stamp.nanosec);
    if (seq > 4) samples.push_back(elapsed / 1000.0);
    if (cpu)
      std::memcpy(host.data(), data, bytes);
    else
      gpu(cudaMemcpy(host.data(), output, bytes, cudaMemcpyDeviceToHost));
    for (int i = 0; i < points; ++i) {
      const auto want = expected(i, seq + (cpu ? 0 : 3));
      require(std::memcmp(&host[i], &want, sizeof(Point)) == 0, "point mismatch");
    }
  };
  std::unique_ptr<bb::CudaBlackboardPublisher<DeviceCloud>> bp;
  std::unique_ptr<bb::CudaBlackboardSubscriber<DeviceCloud>> bs;
  rclcpp::Publisher<Cloud>::SharedPtr pub;
  rclcpp::Subscription<Cloud>::SharedPtr sub;
  if (board) {
    bp = std::make_unique<bb::CudaBlackboardPublisher<DeviceCloud>>(*tx, "cloud");
    bs = std::make_unique<bb::CudaBlackboardSubscriber<DeviceCloud>>(
      *rx, "cloud", [&](DeviceCloud::ConstSharedPtr m) { consume(*m, m->data.get()); }, reader);
  } else {
    if (send) pub = tx->create_publisher<Cloud>("cloud", 8);
    if (receive) {
      rclcpp::SubscriptionOptions opt;
      opt.acceptable_buffer_backends = cpu ? "cpu" : "cuda";
      sub = rx->create_subscription<Cloud>(
        "cloud", 8,
        [&](Cloud::ConstSharedPtr m) {
          require(m->data.get_backend_type() == (cpu ? "cpu" : "cuda"), "unexpected backend");
          require(m->data.size() == bytes, "unexpected payload length");
          if (cpu)
            consume(*m, m->data.data());
          else {
            auto handle = cuda_buffer_backend::from_input_buffer(m->data, reader);
            consume(*m, handle.get_ptr());
          }
        },
        opt);
    }
  }
  auto begin = Clock::now();
  auto timer = tx->create_wall_timer(100ms, [&] {
    if (sent == frames || Clock::now() - begin < 2s) return;
    if (
      board ? !bp->get_intra_process_subscription_count()
            : pub->get_subscription_count() < static_cast<size_t>(peers))
      return;
    ++sent;
    auto start = now_ns();
    if (board) {
      auto m = std::make_unique<DeviceCloud>();
      metadata(*m, sent, start);
      m->data = bb::make_unique<uint8_t[]>(bytes);
      published_address = m->data.get();
      transform(input, published_address, sent, writer);
      gpu(cudaStreamSynchronize(writer));
      bp->publish(std::move(m));
    } else {
      auto m = std::make_unique<Cloud>();
      metadata(*m, sent, start);
      m->data = cuda_buffer_backend::allocate_buffer(bytes);
      {
        auto handle = cuda_buffer_backend::from_output_buffer(m->data, writer);
        published_address = handle.get_ptr();
        transform(input, handle.get_ptr(), sent, writer);
      }
      pub->publish(std::move(m));
    }
  });
  while ((send && sent < frames) || (receive && received < frames)) {
    require(Clock::now() - begin < 20s, "discovery or delivery timeout");
    exec.spin_once(100ms);
  }
  if (receive) {
    std::sort(samples.begin(), samples.end());
    std::cout << mode << " verified=" << received << " timed=" << samples.size()
              << " p50_us=" << samples.at(9) << " p95_us=" << samples.at(18) << std::endl;
  } else
    rclcpp::sleep_for(500ms);
  timer.reset();
  sub.reset();
  bs.reset();
  pub.reset();
  bp.reset();
  if (input) gpu(cudaFree(input));
  if (output) gpu(cudaFree(output));
  if (writer) gpu(cudaStreamDestroy(writer));
  if (reader) gpu(cudaStreamDestroy(reader));
  rclcpp::shutdown();
} catch (const std::exception & e) {
  std::cerr << e.what() << std::endl;
  return 1;
}
