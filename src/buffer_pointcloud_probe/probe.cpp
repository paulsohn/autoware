#include <cuda_buffer/cuda_buffer_api.hpp>
#include <shmem_buffer/shmem_buffer_api.hpp>
#include <sensor_msgs/msg/point_cloud2.hpp>
#include <rclcpp/rclcpp.hpp>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <thread>
#include <unistd.h>

using Cloud = sensor_msgs::msg::PointCloud2;
using namespace std::chrono_literals;
constexpr unsigned frames = 12;
constexpr unsigned bytes = 65536;

void check(bool ok, const std::string &message) {
  if (!ok) throw std::runtime_error(message);
}

Cloud make_cloud(const std::string &backend, unsigned seq, cudaStream_t stream) {
  Cloud m;
  m.header.frame_id = "lidar";
  m.header.stamp.sec = seq;
  m.width = bytes;
  m.height = m.point_step = 1;
  m.row_step = bytes;
  m.fields.resize(1);
  auto &field = m.fields.front();
  field.name = "intensity";
  field.count = 1;
  field.datatype = sensor_msgs::msg::PointField::UINT8;
  if (backend == "cuda") {
    m.data = cuda_buffer_backend::allocate_buffer(bytes);
    auto h = cuda_buffer_backend::from_output_buffer(m.data, stream);
    check(cudaMemsetAsync(h.get_ptr(), seq, bytes, stream) == cudaSuccess, "device write");
  } else {
    m.data = shmem_buffer::allocate_buffer(bytes);
    auto h = shmem_buffer::from_output_buffer(m.data);
    std::fill_n(h.get_ptr(), bytes, seq);
  }
  return m;
}

void verify(const Cloud &m, const std::string &backend, unsigned seq, cudaStream_t stream) {
  check(m.header.frame_id == "lidar" && m.header.stamp.sec == static_cast<int>(seq) &&
    m.width == bytes && m.height == 1 && m.point_step == 1 && m.row_step == bytes &&
    m.fields.size() == 1 && m.fields.front().name == "intensity" &&
    m.data.size() == bytes, "metadata or sequence mismatch");
  check(m.data.get_backend_type() == backend, "received backend " + m.data.get_backend_type());
  const auto matches = [seq](uint8_t byte) { return byte == seq; };
  bool valid;
  if (backend == "cuda") {
    auto h = cuda_buffer_backend::from_input_buffer(m.data, stream);
    std::vector<uint8_t> verification(bytes);
    check(cudaMemcpyAsync(verification.data(), h.get_ptr(), bytes, cudaMemcpyDeviceToHost, stream)
      == cudaSuccess && cudaStreamSynchronize(stream) == cudaSuccess, "verification readback");
    valid = std::all_of(verification.begin(), verification.end(), matches);
  } else if (backend == "shmem") {
    const auto &impl = dynamic_cast<const shmem_buffer::ShmemBufferImpl<uint8_t>&>(*m.data.get_impl());
    check(impl.get_shmem_buffer().has_ipc_descriptor() &&
      impl.get_shmem_buffer().ipc_pid() != getpid(), "missing remote allocation");
    auto h = shmem_buffer::from_input_buffer(m.data);
    valid = std::all_of(h.get_ptr(), h.get_ptr() + bytes, matches);
  } else {
    valid = std::all_of(m.data.begin(), m.data.end(), matches);
  }
  check(valid, "payload mismatch at sequence " + std::to_string(seq));
}

int main(int argc, char **argv) try {
  check(argc == 4, "usage: probe pub|sub cuda|shmem|cpu subscriber_count");
  std::string role = argv[1], backend = argv[2];
  int peers = std::stoi(argv[3]);
  check((role == "sub" || role == "pub") && (backend == "cuda" || backend == "shmem" ||
    (backend == "cpu" && role == "sub")) && peers > 0, "invalid arguments");
  cudaStream_t stream = nullptr;
  if (backend == "cuda") check(cudaStreamCreateWithFlags(&stream, cudaStreamNonBlocking) == cudaSuccess, "create CUDA stream");
  rclcpp::init(argc, argv);
  auto node = rclcpp::Node::make_shared("probe_" + role + "_" + backend);
  auto deadline = std::chrono::steady_clock::now() + 25s;
  if (role == "pub") {
    auto pub = node->create_publisher<Cloud>("buffer_probe", 16);
    while (pub->get_subscription_count() < static_cast<size_t>(peers)) {
      check(std::chrono::steady_clock::now() < deadline, "discovery timeout");
      std::this_thread::sleep_for(50ms);
    }
    for (unsigned seq = 1; seq <= frames; ++seq) {
      auto msg = make_cloud(backend, seq, stream);
      pub->publish(msg);
      std::this_thread::sleep_for(100ms);
    }
    std::this_thread::sleep_for(1s);
  } else {
    rclcpp::SubscriptionOptions options;
    options.acceptable_buffer_backends = backend;
    unsigned received = 0;
    Cloud::ConstSharedPtr held;
    auto sub = node->create_subscription<Cloud>("buffer_probe", 16,
      [&](Cloud::ConstSharedPtr m) {
        verify(*m, backend, ++received, stream);
        if (!held) held = m;
      }, options);
    rclcpp::executors::SingleThreadedExecutor executor;
    executor.add_node(node);
    while (received < frames) {
      check(std::chrono::steady_clock::now() < deadline, "receive timeout");
      executor.spin_some();
      std::this_thread::sleep_for(5ms);
    }
    verify(*held, backend, 1, stream);
    std::cout << backend << " pid=" << getpid() << " verified 12 frames and retained frame 1\n";
  }
  node.reset();
  rclcpp::shutdown();
  if (stream) cudaStreamDestroy(stream);
  return 0;
} catch (const std::exception &e) {
  if (rclcpp::ok()) rclcpp::shutdown();
  std::cerr << e.what() << '\n';
  return 1;
}
