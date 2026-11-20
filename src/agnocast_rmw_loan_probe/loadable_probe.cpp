#include "../rmw_agnocast/host_cloud_view.hpp"
#include <sensor_msgs/msg/point_cloud2.hpp>
#include "fixed_scope.hpp"
#include <rclcpp/rclcpp.hpp>

#include <autoware_planning_msgs/msg/trajectory.hpp>
#include <geometry_msgs/msg/point.hpp>
#include <nav_msgs/msg/path.hpp>

#include <rmw/rmw.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
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
    if (seq == 11 && std::getenv("AGNOCAST_FIXED_PROBE")) wait_file("dual_waiting");
    publish_trajectory(*pub, seq);
    if (seq == 11 && std::getenv("AGNOCAST_FIXED_PROBE")) {
      Message control;
      fill(control, 11);
      dds->publish(control);
      mark("dual_sent");
    }
    wait_file("fast" + std::to_string(seq));
    if (seq == 1) wait_file("slow_holding");
    if (seq == 9) mark("burst_done");
    if (seq == 10) {
      wait_file("dds_waiting");
      std::this_thread::sleep_for(100ms);
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
  int controls = 0;
  auto make_dds = [&] {
    return node->create_subscription<Message>(
      "trajectory_dds_control", 2, [&](Message::ConstSharedPtr msg) {
        verify(*msg, 10 + controls, false);
        ++controls;
        mark("dds_received");
      });
  };
  if (!slow && !restart) dds = make_dds();
  rclcpp::executors::SingleThreadedExecutor exec;
  exec.add_node(node);
  FixedScope fixed;
  auto receive_through = [&](int last) {
    auto deadline = std::chrono::steady_clock::now() + 12s;
    while (expected <= last) {
      check(std::chrono::steady_clock::now() < deadline, "missing trajectory");
      exec.spin_once(100ms);
    }
  };
  if (!slow) {
    if (!restart) {
      receive_through(10);
      auto deadline = std::chrono::steady_clock::now() + 2s;
      mark("dds_waiting");
      while (!controls && std::chrono::steady_clock::now() < deadline) exec.spin_once(3s);
      check(controls && std::chrono::steady_clock::now() < deadline, "DDS wake timed out");
    }
    if (!restart && fixed.scope) {
      if (std::getenv("AGNOCAST_PROBE_REBIND")) {
        fixed.stop();
        exec.remove_node(node);
        dds.reset();
        dds = make_dds();
        exec.add_node(node);
        exec.spin_once(0ns);
        fixed.start();
        std::cout << "replaced DDS subscription between fixed scopes" << std::endl;
      }
      exec.spin_once(10ms);  // Rearm DDS before both publications queue.
      mark("dual_waiting");
      wait_file("dual_sent");
      std::this_thread::sleep_for(100ms);
    }
    receive_through(11);
    if (!restart && fixed.scope) {
      if (controls == 1) exec.spin_once(1s);
      check(controls == 2, "missing simultaneous DDS sample");
      auto start = std::chrono::steady_clock::now();
      std::thread cancel([&] {
        std::this_thread::sleep_for(100ms);
        if (std::getenv("AGNOCAST_PROBE_CONTEXT_SHUTDOWN")) rclcpp::shutdown();
        else exec.cancel();
      });
      exec.spin();
      cancel.join();
      check(std::chrono::steady_clock::now() - start < 2s, "idle cancel timed out");
      std::cout << "dual delivery and idle "
                << (std::getenv("AGNOCAST_PROBE_CONTEXT_SHUTDOWN") ? "context shutdown" : "executor cancel")
                << " passed" << std::endl;
    }
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
  fixed.stop();
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

void bridge_peer(bool writer, bool active, bool dds)
{
  auto node = std::make_shared<rclcpp::Node>(writer ? "boundary_writer" : "boundary_reader");
  constexpr auto topic = "trajectory_loan_probe";
  auto deadline = std::chrono::steady_clock::now() + 8s;
  auto wait_matches = [&](auto count) {
    const size_t target = active && dds ? 2 : 1;
    while (count() < target) {
      check(std::chrono::steady_clock::now() < deadline, "missing graph match at boundary");
      std::this_thread::sleep_for(10ms);
    }
  };
  if (writer) {
    auto pub = node->create_publisher<Path>(topic, 4);
    std::cout << "writer " << rmw_get_implementation_identifier() << " GID provider "
              << pub->get_gid().implementation_identifier << std::endl;
    wait_matches([&] { return pub->get_subscription_count(); });
    wait_file("discovered");
    Path msg;
    fill(msg, 3);
    pub->publish(msg);
    mark("sent");
    wait_file("checked");
    return;
  }
  size_t calls = 0;
  auto sub = node->create_subscription<Path>(
    topic, 4, [&](Path::ConstSharedPtr msg, const rclcpp::MessageInfo & mi) {
      auto & info = mi.get_rmw_message_info();
      std::cout << "reader " << rmw_get_implementation_identifier() << " sample GID provider "
                << (info.publisher_gid.implementation_identifier
                      ? info.publisher_gid.implementation_identifier
                      : "(null)")
                << " source_ns " << info.source_timestamp << " received_ns "
                << info.received_timestamp << std::endl;
      verify(*msg, 3, false);
      ++calls;
    });
  wait_matches([&] { return sub->get_publisher_count(); });
  mark("discovered");
  wait_file("sent");
  rclcpp::executors::SingleThreadedExecutor exec;
  exec.add_node(node);
  deadline = std::chrono::steady_clock::now() + (active ? 2s : 1s);
  while (std::chrono::steady_clock::now() < deadline) exec.spin_once(20ms);
  check(calls == (active ? 1U : 0U), "unexpected boundary delivery count");
  std::cout << "Path boundary callbacks=" << calls << std::endl;
  mark("checked");
}

std::string gid_text(const rmw_gid_t & gid)
{
  std::string result = gid.implementation_identifier ? gid.implementation_identifier : "(null)";
  result += '/';
  constexpr char digits[] = "0123456789abcdef";
  for (uint8_t byte : gid.data) {
    result += digits[byte >> 4];
    result += digits[byte & 15];
  }
  return result;
}

bool native_matching(bool writer)
{
  auto node = std::make_shared<rclcpp::Node>(writer ? "matching_writers" : "matching_reader");
  constexpr auto topic = "/trajectory_loan_probe";
  if (writer) {
    auto reliable = node->create_publisher<Path>(topic, rclcpp::QoS(8).reliable());
    auto best = node->create_publisher<Path>(topic, rclcpp::QoS(8).best_effort());
    auto reliable_gid = gid_text(reliable->get_gid());
    auto best_gid = gid_text(best->get_gid());
    check(reliable_gid != best_gid, "writers share a GID");
    std::ofstream("writers") << reliable_gid << ' ' << best_gid;
    wait_file("reader_ready");
    auto deadline = std::chrono::steady_clock::now() + 5s;
    while (!reliable->get_subscription_count()) {
      check(std::chrono::steady_clock::now() < deadline, "reliable writer never matched");
      std::this_thread::sleep_for(10ms);
    }
    auto rel_matches = reliable->get_subscription_count(), be_matches = best->get_subscription_count();
    std::cout << "matched readers reliable=" << rel_matches << " best_effort=" << be_matches << std::endl;
    auto send = [&] {
      for (int seq = 1; seq <= 6; ++seq) {
        Path msg;
        fill(msg, seq);
        (seq % 2 ? best : reliable)->publish(msg);
      }
    };
    send();
    mark("sent");
    wait_file("calibrate");
    deadline = std::chrono::steady_clock::now() + 5s;
    while (!best->get_subscription_count() || reliable->get_subscription_count() != 2) {
      check(std::chrono::steady_clock::now() < deadline, "calibration listener never matched");
      std::this_thread::sleep_for(10ms);
    }
    send();
    mark("calibration_sent");
    wait_file("observed");
    return rel_matches == 1 && be_matches == 0;
  }

  std::string rel_gid, be_gid;
  std::vector<int> compatible, incompatible;
  std::vector<std::pair<int, std::string>> delivered;
  bool calibrating = false;
  auto sub = node->create_subscription<Path>(topic, rclcpp::QoS(8).reliable(),
    [&](Path::ConstSharedPtr msg, const rclcpp::MessageInfo & mi) {
      if (calibrating) return;
      check(!msg->poses.empty(), "empty Path");
      int seq = static_cast<int>(msg->poses.front().pose.position.x);
      check(seq >= 1 && seq <= 6, "unexpected sequence");
      verify(*msg, seq, false);
      (seq % 2 ? incompatible : compatible).push_back(seq);
      auto actual = gid_text(mi.get_rmw_message_info().publisher_gid);
      delivered.emplace_back(seq, actual);
      std::cout << "received seq=" << seq << " gid=" << actual << std::endl;
    });
  wait_file("writers");
  check(bool(std::ifstream("writers") >> rel_gid >> be_gid), "missing writer GIDs");
  auto deadline = std::chrono::steady_clock::now() + 5s;
  while (node->count_publishers(topic) != 2 || sub->get_publisher_count() == 0) {
    check(std::chrono::steady_clock::now() < deadline, "writer discovery incomplete");
    std::this_thread::sleep_for(10ms);
  }
  auto matches = sub->get_publisher_count();
  std::cout << "discovered=2 matched writers=" << matches << " reliable_gid=" << rel_gid
            << " best_effort_gid=" << be_gid << std::endl;
  mark("reader_ready");
  wait_file("sent");
  rclcpp::executors::SingleThreadedExecutor exec;
  exec.add_node(node);
  deadline = std::chrono::steady_clock::now() + 1s;
  while (std::chrono::steady_clock::now() < deadline) exec.spin_once(10ms);
  calibrating = true;
  std::set<std::string> ids[2];
  std::set<int> calibration_sequences;
  auto calibration = node->create_subscription<Path>(topic, rclcpp::QoS(8).best_effort(),
    [&](Path::ConstSharedPtr msg, const rclcpp::MessageInfo & mi) {
      check(!msg->poses.empty(), "empty calibration Path");
      int seq = static_cast<int>(msg->poses.front().pose.position.x);
      check(seq >= 1 && seq <= 6 && calibration_sequences.insert(seq).second, "invalid calibration sequence");
      verify(*msg, seq, false);
      ids[seq % 2].insert(gid_text(mi.get_rmw_message_info().publisher_gid));
    });
  mark("calibrate");
  wait_file("calibration_sent");
  deadline = std::chrono::steady_clock::now() + 2s;
  while (calibration_sequences.size() < 6 && std::chrono::steady_clock::now() < deadline)
    exec.spin_once(10ms);
  check(calibration_sequences.size() == 6, "calibration samples missing");
  auto usable = [](const auto & group) {
    if (group.size() != 1) return false;
    const auto & id = *group.begin();
    return id.rfind("(null)/", 0) != 0 && id.find_first_not_of('0', id.find('/') + 1) != std::string::npos;
  };
  bool identity = usable(ids[0]) && usable(ids[1]) && ids[0] != ids[1];
  for (const auto & [seq, id] : delivered) identity &= ids[seq % 2].count(id) == 1;
  std::cout << "compatible callbacks=" << compatible.size() << " incompatible callbacks="
            << incompatible.size() << " calibrated writer identity=" << identity
            << " receiver GIDs reliable=" << *ids[0].begin() << " best_effort=" << *ids[1].begin() << std::endl;
  mark("observed");
  return matches == 1 && compatible == std::vector<int>{2, 4, 6} && incompatible.empty() && identity;
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
    [&](std::shared_ptr<const rclcpp::SerializedMessage> bytes) {
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

using Cloud = sensor_msgs::msg::PointCloud2;
Cloud cloud_fixture(int seq)
{
  Cloud msg;
  msg.header.stamp.sec = seq;
  msg.header.stamp.nanosec = 123;
  msg.header.frame_id = std::string(80, 'a' + seq);
  msg.height = seq ? 1 : 0;
  msg.width = seq ? 2 : 0;
  msg.point_step = 4;
  msg.row_step = seq ? 12 : 0;
  msg.is_dense = !seq;
  msg.is_bigendian = seq;
  if (seq) {
    msg.fields.resize(1);
    msg.fields[0].name.assign(64, 'x');
    msg.fields[0].datatype = sensor_msgs::msg::PointField::UINT16;
    msg.fields[0].offset = 2;
    msg.fields[0].count = 1;
    msg.data.resize(msg.row_step);
    for (size_t i = 0; i < msg.data.size(); ++i) msg.data[i] = seq + 3 * i;
  }
  return msg;
}
void host_cloud_contract(rclcpp::Node & node, bool writer)
{
  const char * topic = "trajectory_loan_probe";
  const bool view = std::getenv("AGNOCAST_RMW_HOST_VIEW");
  const int frames = view ? 8 : 2;
  if (writer) {
    auto pub = node.create_publisher<Cloud>(topic, 4);
    wait_file("cloud_attached");
    auto deadline = std::chrono::steady_clock::now() + 10s;
    while (pub->get_subscription_count() != (view ? 3u : 2u)) {
      check(std::chrono::steady_clock::now() < deadline, "missing cloud readers");
      std::this_thread::sleep_for(10ms);
    }
    const bool loaned = std::getenv("AGNOCAST_RMW_HOST_LOAN");
    if (loaned) {
      check(pub->can_loan_messages(), "writer lacks native loan support");
      auto abandoned = pub->borrow_loaned_message();
      abandoned.get() = cloud_fixture(37);
    }
    for (int seq = 0; seq < frames; ++seq) {
      if (loaned) {
        Cloud prebuilt;
        if (seq == 2) prebuilt = cloud_fixture(seq);
        auto loan = pub->borrow_loaned_message();
        loan.get() = seq == 2 ? std::move(prebuilt) : cloud_fixture(seq);
        std::ofstream("view-address") << reinterpret_cast<uintptr_t>(loan.get().data.data());
        pub->publish(std::move(loan));
      } else {
        pub->publish(cloud_fixture(seq));
      }
      wait_file("cloud_received" + std::to_string(seq));
    }
    if (view) wait_file("view_released");
    return;
  }
  Cloud::ConstSharedPtr retained;
  std::weak_ptr<const std::vector<uint8_t>> owner;
  int typed = 0, serialized = 0, cpu = 0;
  rclcpp::SubscriptionOptions options;
  if (view) options.acceptable_buffer_backends = "any";
  rclcpp::Subscription<Cloud>::SharedPtr cpu_sub;
  if (view) {
    cpu_sub = node.create_subscription<Cloud>(topic, 4, [&](const Cloud & msg) {
      check(msg.data.get_backend_type() == "cpu" && msg == cloud_fixture(cpu++), "CPU fallback mismatch");
    });
  }
  auto sub = node.create_subscription<Cloud>(topic, 4, [&](Cloud::SharedPtr msg) {
    check(*msg == cloud_fixture(typed), "typed cloud mismatch");
    if (view) {
      const auto & impl = dynamic_cast<const rmw_agnocast::HostCloudView &>(*msg->data.get_impl());
      uintptr_t address;
      check(bool(std::ifstream("view-address") >> address) &&
            ((address == reinterpret_cast<uintptr_t>(impl.bytes().data())) ==
             !(std::getenv("AGNOCAST_RMW_HOST_LOAN") && typed == 2)), "payload address mismatch");
      if (typed == 1) {
        retained = msg;
        owner = impl.owner();
        bool rejected = false;
        try {
          msg->data[0] = 99;
        } catch (const std::runtime_error &) {
          rejected = true;
        }
        check(rejected, "view allowed in-place mutation");
        auto clone = *msg;
        clone.data[0] = 99;
        check(impl.bytes()[0] == typed && clone.data.get_backend_type() == "cpu", "copy mutation leaked");
      }
    }
    ++typed;
  }, options);
  auto generic = node.create_generic_subscription(topic, "sensor_msgs/msg/PointCloud2", rclcpp::QoS(4),
    [&](std::shared_ptr<const rclcpp::SerializedMessage> bytes) {
      Cloud msg;
      rclcpp::Serialization<Cloud>().deserialize_message(bytes.get(), &msg);
      check(msg == cloud_fixture(serialized++), "serialized cloud mismatch");
    });
  rclcpp::executors::SingleThreadedExecutor exec;
  exec.add_node(node.get_node_base_interface());
  mark("cloud_attached");
  for (int seq = 0; seq < frames; ++seq) {
    auto deadline = std::chrono::steady_clock::now() + 10s;
    while (typed <= seq || serialized <= seq || (view && cpu <= seq)) {
      check(std::chrono::steady_clock::now() < deadline, "cloud callback timeout");
      exec.spin_once(100ms);
    }
    mark("cloud_received" + std::to_string(seq));
  }
  if (view) {
    exec.remove_node(node.get_node_base_interface());
    cpu_sub.reset();
    sub.reset();
    generic.reset();
    rclcpp::shutdown();
    check(!owner.expired() && *retained == cloud_fixture(1), "lost retained view after teardown");
    std::thread release([&] { retained.reset(); });
    release.join();
    check(owner.expired(), "view still owns the loan");
    mark("view_released");
    std::cout << "view addresses, independent mutation, retention and final release passed" << std::endl;
  }
  std::cout << "empty and padded clouds verified through typed and serialized take" << std::endl;
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
    if (std::string(argv[2]) == "matching") {
      bool ok = native_matching(writer);
      rclcpp::shutdown();
      return ok ? 0 : 3;
    }
    if (std::string(argv[2]) == "boundary" && argc == 4) {
      bool dds = std::string(rmw_get_implementation_identifier()) == "rmw_cyclonedds_cpp";
      bridge_peer(writer, std::string(argv[3]) == "on", dds);
      rclcpp::shutdown();
      return 0;
    }
    check(
      std::string(rmw_get_implementation_identifier()) == "rmw_agnocast", "wrong implementation");
    if (std::string(argv[2]) == "host-cloud") {
      auto n = std::make_shared<rclcpp::Node>(writer ? "cloud_writer" : "cloud_reader");
      host_cloud_contract(*n, writer);
      if (rclcpp::ok()) rclcpp::shutdown();
      return 0;
    }
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
