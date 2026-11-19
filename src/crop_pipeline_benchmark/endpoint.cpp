#include <cuda_buffer/cuda_buffer_api.hpp>
#include <shmem_buffer/shmem_buffer_api.hpp>
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/point_cloud2.hpp>

#include <array>
#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>
#include <unistd.h>

using Cloud = sensor_msgs::msg::PointCloud2;
using namespace std::chrono_literals;
constexpr unsigned points = 32768;
constexpr unsigned frames = 60;
struct Point {
  float x, y, z;
  uint32_t id;
};
static_assert(sizeof(Point) == 16);

Point sample(unsigned i, unsigned frame) {
  return {i % 2 ? 2.0f : 0.5f, static_cast<float>(i % 4) / 8,
    static_cast<float>(i % 8) / 16, frame * points + i};
}

void require(bool ok, const char *what) {
  if (!ok) throw std::runtime_error(what);
}

void publish(rclcpp::Node &node, const std::string &backend) {
  auto pub = node.create_publisher<Cloud>("input", rclcpp::SensorDataQoS());
  auto deadline = std::chrono::steady_clock::now() + 20s;
  while (!pub->get_subscription_count() || !node.count_subscribers("output")) {
    require(std::chrono::steady_clock::now() < deadline, "pipeline discovery timeout");
    std::this_thread::sleep_for(50ms);
  }
  std::this_thread::sleep_for(1s);
  cudaStream_t stream = nullptr;
  if (backend == "cuda") require(cudaStreamCreate(&stream) == cudaSuccess, "CUDA stream");
  std::vector<Point> fixture(points);
  const size_t bytes = points * sizeof(Point);
  auto next = std::chrono::steady_clock::now();
  for (unsigned frame = 0; frame < frames; ++frame) {
    std::this_thread::sleep_until(next);
    next += 100ms;
    Cloud cloud;
    cloud.header.frame_id = "base_link";
    cloud.width = points;
    cloud.height = 1;
    cloud.is_dense = true;
    cloud.point_step = sizeof(Point);
    cloud.row_step = bytes;
    const std::array<std::string, 4> names{"x", "y", "z", "id"};
    for (size_t i = 0; i < names.size(); ++i) {
      sensor_msgs::msg::PointField field;
      field.name = names[i];
      field.offset = i * 4;
      field.count = 1;
      field.datatype = i == 3 ? field.UINT32 : field.FLOAT32;
      cloud.fields.push_back(field);
    }
    for (unsigned i = 0; i < points; ++i) fixture[i] = sample(i, frame);
    if (backend == "cuda") {
      cloud.data = cuda_buffer_backend::allocate_buffer(bytes);
      auto h = cuda_buffer_backend::from_output_buffer(cloud.data, stream);
      require(cudaMemcpyAsync(h.get_ptr(), fixture.data(), bytes, cudaMemcpyHostToDevice,
        stream) == cudaSuccess && cudaStreamSynchronize(stream) == cudaSuccess, "upload");
    } else if (backend == "shmem") {
      cloud.data = shmem_buffer::allocate_buffer(bytes);
      auto h = shmem_buffer::from_output_buffer(cloud.data);
      std::memcpy(h.get_ptr(), fixture.data(), bytes);
    } else {
      cloud.data.resize(bytes);
      std::memcpy(cloud.data.data(), fixture.data(), bytes);
    }
    cloud.header.stamp = node.now();
    pub->publish(cloud);
  }
  std::this_thread::sleep_for(300ms);
  if (stream) cudaStreamDestroy(stream);
}

void verify_output(const Cloud &cloud, const std::string &backend, unsigned frame) {
  require(cloud.data.get_backend_type() == backend, "unexpected output backend");
  require(cloud.width == points / 2 && cloud.height == 1 &&
        cloud.header.frame_id == "base_link" && cloud.point_step == sizeof(Point) &&
        cloud.row_step == points / 2 * sizeof(Point) && cloud.data.size() == cloud.row_step &&
        cloud.fields.size() == 4 && cloud.fields.back().name == "id", "invalid crop output");
  auto verify_points = [&](const uint8_t *bytes) {
    for (unsigned i = 0; i < cloud.width; ++i) {
      Point expected = sample(2 * i, frame);
      require(std::memcmp(&expected, bytes + sizeof(Point) * i, sizeof(Point)) == 0,
        "output value, order, or frame count mismatch");
    }
  };
  if (backend == "cuda") {
    auto read = cuda_buffer_backend::from_input_buffer(cloud.data, cudaStreamPerThread);
    std::vector<uint8_t> check_bytes(cloud.row_step);
    CUDA_CHECK(cudaMemcpyAsync(check_bytes.data(), read.get_ptr(), check_bytes.size(),
      cudaMemcpyDeviceToHost, cudaStreamPerThread));
    CUDA_CHECK(cudaStreamSynchronize(cudaStreamPerThread));
    verify_points(check_bytes.data());
  } else if (backend == "shmem") {
    const auto &impl = dynamic_cast<const shmem_buffer::ShmemBufferImpl<uint8_t>&>(*cloud.data.get_impl());
    require(impl.get_shmem_buffer().has_ipc_descriptor() &&
      impl.get_shmem_buffer().ipc_pid() != getpid(), "expected a remote output allocation");
    auto read = shmem_buffer::from_input_buffer(cloud.data);
    verify_points(read.get_ptr());
  } else {
    verify_points(cloud.data.data());
  }
}

void receive(rclcpp::Node::SharedPtr node, const std::string &backend) {
  unsigned received = 0;
  Cloud::ConstSharedPtr held;
  rclcpp::SubscriptionOptions options;
  options.acceptable_buffer_backends = backend;
  auto sub = node->create_subscription<Cloud>("output", rclcpp::SensorDataQoS(),
    [&](Cloud::ConstSharedPtr cloud) {
      auto latency = (node->now() - rclcpp::Time(cloud->header.stamp)).nanoseconds();
      require(latency > 0, "nonpositive latency");
      verify_output(*cloud, backend, received);
      if (!held) held = cloud;
      std::cout << received++ << ',' << latency << std::endl;
    }, options);
  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(node);
  auto deadline = std::chrono::steady_clock::now() + 40s;
  while (received < frames) {
    require(std::chrono::steady_clock::now() < deadline, "output timeout");
    executor.spin_once(100ms);
  }
  verify_output(*held, backend, 0);
}

int main(int argc, char **argv) try {
  require(argc == 3, "usage: endpoint pub|sub cpu|shmem|cuda");
  rclcpp::init(argc, argv);
  std::string role = argv[1], backend = argv[2];
  auto node = rclcpp::Node::make_shared("bench_" + role);
  if (role == "sub") receive(node, backend);
  else {
    require(role == "pub" && (backend == "cuda" || backend == "cpu" || backend == "shmem"),
      "invalid endpoint arguments");
    publish(*node, backend);
  }
  node.reset();
  rclcpp::shutdown();
  return 0;
} catch (const std::exception &error) {
  std::cerr << error.what() << std::endl;
  if (rclcpp::ok()) rclcpp::shutdown();
  return 1;
}
