#include <rclcpp/rclcpp.hpp>

#include <autoware_planning_msgs/msg/trajectory.hpp>
#include <geometry_msgs/msg/point.hpp>
#include <nav_msgs/msg/path.hpp>

#include <rmw/rmw.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

using Message = autoware_planning_msgs::msg::Trajectory;
using namespace std::chrono_literals;
void check(bool ok, const char * what)
{
  if (!ok) throw std::runtime_error(what);
}
void wait_file(std::string file)
{
  auto deadline = std::chrono::steady_clock::now() + 12s;
  while (!std::filesystem::exists(file)) {
    check(std::chrono::steady_clock::now() < deadline, "peer acknowledgement timed out");
    std::this_thread::sleep_for(10ms);
  }
}
void mark(std::string file)
{
  std::ofstream(file) << "ready";
}
void fill(Message & msg, int seq)
{
  msg.header.frame_id.assign(96, 'A' + seq);
  msg.points.resize(1);
  auto old = reinterpret_cast<uintptr_t>(msg.points.data());
  msg.points.reserve(msg.points.capacity() + 128);
  check(old != reinterpret_cast<uintptr_t>(msg.points.data()), "vector failed to relocate");
  msg.points.resize(129);
  for (size_t i = 0; i < msg.points.size(); ++i) msg.points[i].pose.position.x = seq * 1000 + i;
}
void verify(const Message & msg, int seq, bool same_address)
{
  check(
    msg.points.size() == 129 && msg.header.frame_id == std::string(96, 'A' + seq),
    "trajectory shape");
  for (size_t i = 0; i < msg.points.size(); ++i)
    check(msg.points[i].pose.position.x == seq * 1000 + i, "invalid point");
  if (same_address) {
    uintptr_t object{}, points{}, string{};
    check(
      bool(std::ifstream("addresses" + std::to_string(seq)) >> object >> points >> string),
      "addresses missing");
    check(
      object == reinterpret_cast<uintptr_t>(&msg) &&
        points == reinterpret_cast<uintptr_t>(msg.points.data()) &&
        string == reinterpret_cast<uintptr_t>(msg.header.frame_id.data()),
      "copied loan");
  }
}
using Path = nav_msgs::msg::Path;
using Point = geometry_msgs::msg::Point;
void fill(Point & p, int seq)
{
  p.x = seq;
  p.y = 2 * seq;
  p.z = -seq;
}
void fill(Path & path, int seq)
{
  path.poses.resize(5);
  for (size_t i = 0; i < path.poses.size(); ++i) {
    path.poses[i].header.frame_id.assign(80 + i, 'a' + seq);
    fill(path.poses[i].pose.position, seq + i);
  }
}
template <class T>
std::array<uintptr_t, 3> addresses(const T & msg)
{
  std::array<uintptr_t, 3> a{reinterpret_cast<uintptr_t>(&msg), 0, 0};
  if constexpr (std::is_same_v<T, Path>) {
    a[1] = reinterpret_cast<uintptr_t>(msg.poses.data());
    a[2] = reinterpret_cast<uintptr_t>(msg.poses.back().header.frame_id.data());
  } else if constexpr (std::is_same_v<T, Message>) {
    a[1] = reinterpret_cast<uintptr_t>(msg.points.data());
    a[2] = reinterpret_cast<uintptr_t>(msg.header.frame_id.data());
  }
  return a;
}
template <class T>
void verify(const T & msg, int seq, bool loan)
{
  T expected;
  fill(expected, seq);
  check(msg == expected, "incorrect payload");
  if (loan) {
    auto a = addresses(msg);
    std::array<uintptr_t, 3> source{};
    check(
      bool(
        std::ifstream("addresses" + std::to_string(seq)) >> source[0] >> source[1] >> source[2]) &&
        a == source,
      "incorrect loan addresses");
  }
}
template <class T>
void publish_trajectory(rclcpp::Publisher<T> & pub, int seq)
{
  auto loan = pub.borrow_loaned_message();
  fill(loan.get(), seq);
  auto a = addresses(loan.get());
  std::ofstream("addresses" + std::to_string(seq)) << a[0] << ' ' << a[1] << ' ' << a[2];
  pub.publish(std::move(loan));
}
void pressure_writer(rclcpp::Node & node)
{
  auto pub = node.create_publisher<Message>("trajectory_loan_probe", 2);
  auto dds = node.create_publisher<Message>("trajectory_dds_control", 2);
  auto wait_readers = [&](size_t count) {
    auto deadline = std::chrono::steady_clock::now() + 10s;
    while (pub->get_subscription_count() != count) {
      check(std::chrono::steady_clock::now() < deadline, "reader count mismatch");
      std::this_thread::sleep_for(10ms);
    }
  };
  wait_readers(2);
  for (int seq = 1; seq <= 11; ++seq) {
    if (seq == 10) {
      wait_file("slow_released");
      wait_readers(1);
    }
    if (seq == 11) wait_readers(2);
    publish_trajectory(*pub, seq);
    wait_file("fast" + std::to_string(seq));
    if (seq == 1) wait_file("slow_holding");
    if (seq == 9) mark("burst_done");
    if (seq == 10) {
      Message control;
      fill(control, 10);
      dds->publish(control);
      wait_file("dds_received");
      mark("restart_now");
    }
  }
  wait_file("restarted_done");
}

void pressure_reader(rclcpp::Node::SharedPtr node, const std::string & role)
{
  const bool slow = role == "slow";
  const bool restart = role == "restart";
  int expected = restart ? 11 : 1;
  Message::ConstSharedPtr held;
  auto sub = node->create_subscription<Message>(
    "trajectory_loan_probe", 2, [&](Message::ConstSharedPtr msg) {
      verify(*msg, expected, true);
      if (slow && expected == 1) held = msg;
      if (!slow && !restart) mark("fast" + std::to_string(expected));
      ++expected;
    });
  rclcpp::Subscription<Message>::SharedPtr dds;
  if (!slow && !restart) {
    dds = node->create_subscription<Message>(
      "trajectory_dds_control", 2, [&](Message::ConstSharedPtr msg) {
        verify(*msg, 10, false);
        mark("dds_received");
      });
  }
  rclcpp::executors::SingleThreadedExecutor exec;
  exec.add_node(node);
  auto receive_through = [&](int last) {
    auto deadline = std::chrono::steady_clock::now() + 12s;
    while (expected <= last) {
      check(std::chrono::steady_clock::now() < deadline, "missing trajectory");
      exec.spin_once(100ms);
    }
  };
  if (!slow) {
    receive_through(11);
    if (restart)
      mark("restarted_done");
    else
      wait_file("restarted_done");
    return;
  }
  receive_through(1);
  mark("slow_holding");
  wait_file("burst_done");
  // Allow the independent native receiver to enqueue the final publication.
  std::this_thread::sleep_for(100ms);
  expected = 8;
  receive_through(9);
  auto handle = std::weak_ptr<rcl_subscription_t>(sub->get_subscription_handle());
  exec.remove_node(node);
  sub.reset();
  node.reset();
  check(!handle.expired(), "held loan lost its subscription");
  rclcpp::shutdown();
  verify(*held, 1, true);
  std::thread release([&] {
    mark("final_release_begin");
    held.reset();
    mark("final_release_end");
  });
  release.join();
  check(handle.expired(), "subscription survived final release");
  mark("slow_released");
}

void contract(rclcpp::Node & node, const std::string & role)
{
  constexpr auto topic = "trajectory_loan_probe";
  if (role == "writer") {
    auto pub = node.create_publisher<Point>(topic, rclcpp::QoS(2).best_effort());
    wait_file("attached");
    auto deadline = std::chrono::steady_clock::now() + 8s;
    while (pub->get_subscription_count() != 2) {
      check(std::chrono::steady_clock::now() < deadline, "expected two compatible DDS endpoints");
      std::this_thread::sleep_for(10ms);
    }
    auto loan = pub->borrow_loaned_message();
    fill(loan.get(), 1);
    auto first = reinterpret_cast<uintptr_t>(&loan.get());
    pub->publish(std::move(loan));
    wait_file("kill_confirmed");
    bool reused = false;
    for (int i = 0; i < 512; ++i) {
      auto next = pub->borrow_loaned_message();
      reused |= reinterpret_cast<uintptr_t>(&next.get()) == first;
      fill(next.get(), i);
      pub->publish(std::move(next));
    }
    check(reused, "publisher did not reuse killed reader's sample address");
    std::cout << "writer reused held sample address after SIGKILL" << std::endl;
    return;
  }
  Point::ConstSharedPtr held;
  bool serialized = false, mismatch = false;
  auto sub = node.create_subscription<Point>(
    topic, rclcpp::QoS(2).best_effort(),
    [&](Point::ConstSharedPtr msg, const rclcpp::MessageInfo & mi) {
      held = msg;
      auto & info = mi.get_rmw_message_info();
      std::cout << "typed x=" << msg->x << " source_ns=" << info.source_timestamp
                << " receive_ns=" << info.received_timestamp << " gid_identifier="
                << (info.publisher_gid.implementation_identifier
                      ? info.publisher_gid.implementation_identifier
                      : "(null)")
                << std::endl;
    });
  auto incompatible = node.create_subscription<Point>(
    topic, rclcpp::QoS(2).reliable(), [&](Point::ConstSharedPtr) { mismatch = true; });
  auto generic = node.create_generic_subscription(
    topic, "geometry_msgs/msg/Point", rclcpp::QoS(2).best_effort(),
    [&](std::shared_ptr<rclcpp::SerializedMessage> bytes) {
      Point p;
      rclcpp::Serialization<Point>().deserialize_message(bytes.get(), &p);
      verify(p, 1, false);
      serialized = true;
    });
  rclcpp::executors::SingleThreadedExecutor exec;
  exec.add_node(node.get_node_base_interface());
  mark("attached");
  auto deadline = std::chrono::steady_clock::now() + 8s;
  while (std::chrono::steady_clock::now() < deadline && (!held || !serialized))
    exec.spin_once(100ms);
  check(held && serialized, "missing typed or serialized sample");
  verify(*held, 1, false);
  for (int i = 0; i < 10; ++i) exec.spin_once(25ms);
  std::cout << "serialized callback verified; incompatible reliable callback=" << mismatch
            << std::endl;
  mark("kill_ready");
  std::raise(SIGKILL);
}

struct CallbackError
{
};
template <class T>
void simple(bool writer, bool use_loan)
{
  auto node = std::make_shared<rclcpp::Node>(writer ? "trajectory_writer" : "trajectory_reader");
  if (writer) {
    auto pub = node->create_publisher<T>("trajectory_loan_probe", 4);
    check(pub->can_loan_messages(), "missing publisher loan");
    auto deadline = std::chrono::steady_clock::now() + 10s;
    while (pub->get_subscription_count() != 1) {
      check(std::chrono::steady_clock::now() < deadline, "no matched subscription");
      std::this_thread::sleep_for(10ms);
    }
    for (int seq = 1; seq <= 4; ++seq) {
      wait_file("waiting" + std::to_string(seq));
      std::this_thread::sleep_for(100ms);
      if (seq == 4) {
        T msg;
        fill(msg, seq);
        pub->publish(msg);
        msg = T{};
      } else {
        publish_trajectory(*pub, seq);
      }
      wait_file("done" + std::to_string(seq));
    }
  } else {
    typename T::ConstSharedPtr held;
    int calls = 0;
    auto sub =
      node->create_subscription<T>("trajectory_loan_probe", 4, [&](typename T::ConstSharedPtr m) {
        ++calls;
        verify(*m, calls, use_loan && calls < 4);
        if (calls == 1) held = m;
        if (calls == 3) throw CallbackError{};
      });
    check(sub->can_loan_messages() == use_loan, "unexpected subscriber loan mode");
    rclcpp::executors::SingleThreadedExecutor exec;
    exec.add_node(node);
    for (int seq = 1; seq <= 4; ++seq) {
      mark("waiting" + std::to_string(seq));
      bool threw = false;
      auto deadline = std::chrono::steady_clock::now() + 2s;
      while (calls < seq) {
        check(std::chrono::steady_clock::now() < deadline, "blocking executor timed out");
        try {
          exec.spin_once(3s);
        } catch (const CallbackError &) {
          threw = true;
        }
      }
      check(
        std::chrono::steady_clock::now() < deadline, "sample arrived only after executor timeout");
      check(threw == (seq == 3), "exception path mismatch");
      verify(*held, 1, use_loan);
      mark("done" + std::to_string(seq));
    }
    auto handle = std::weak_ptr<rcl_subscription_t>(sub->get_subscription_handle());
    exec.remove_node(node);
    sub.reset();
    node.reset();
    check(!use_loan || !handle.expired(), "premature subscription destruction");
    std::thread release([&] { held.reset(); });
    release.join();
    check(handle.expired(), "late subscription destruction");
    std::cout << "rmw_agnocast " << rosidl_generator_traits::name<T>()
              << (use_loan ? " loan" : " copy")
              << ": verified four samples, retained A, throwing C and copied publish D"
              << std::endl;
  }
}
int main(int argc, char ** argv)
{
  try {
    check((argc == 3 || argc == 4), "expected role and loan/copy mode");
    bool writer = std::string(argv[1]) == "writer", use_loan = std::string(argv[2]) == "loan";
    rclcpp::init(argc, argv);
    check(
      std::string(rmw_get_implementation_identifier()) == "rmw_agnocast", "wrong implementation");
    if (std::string(argv[2]) == "contract") {
      auto n = std::make_shared<rclcpp::Node>(writer ? "contract_writer" : "contract_reader");
      contract(*n, argv[1]);
      rclcpp::shutdown();
      return 0;
    }
    if (std::string(argv[2]) == "pressure") {
      const std::string role = argv[1];
      auto pressure_node =
        std::make_shared<rclcpp::Node>("trajectory_" + (role == "restart" ? "slow" : role));
      if (writer)
        pressure_writer(*pressure_node);
      else
        pressure_reader(std::move(pressure_node), role);
      pressure_node.reset();
      if (rclcpp::ok()) rclcpp::shutdown();
      return 0;
    }
    const std::string type = argc == 4 ? argv[3] : "trajectory";
    if (type == "point")
      simple<Point>(writer, use_loan);
    else if (type == "path")
      simple<Path>(writer, use_loan);
    else
      simple<Message>(writer, use_loan);
    rclcpp::shutdown();
  } catch (const std::exception & error) {
    std::cerr << error.what() << std::endl;
    return 1;
  }
}
