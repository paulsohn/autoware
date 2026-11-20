#include <agnocast/node/agnocast_node.hpp>
#include <agnocast/node/agnocast_only_single_threaded_executor.hpp>
#include <rclcpp/rclcpp.hpp>

#include <nav_msgs/msg/path.hpp>

#include <array>
#include <filesystem>
#include <fstream>
#include <thread>

using Path = nav_msgs::msg::Path;
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
constexpr size_t frames = 60;
constexpr size_t poses = 64;
constexpr char topic[] = "/path_latency";

// The native executor invokes both hooks on the application callback thread.
struct NativeStages
{
  Clock::time_point wake, acquired;
  uint32_t wake_id, loan_id;
  int64_t entry;
  uintptr_t address;
};
thread_local NativeStages current_stages{};
extern "C" void path_probe_wake(uint32_t id)
{
  current_stages.wake = Clock::now();
  current_stages.wake_id = id;
}
extern "C" void path_probe_loan(uint32_t id, int64_t entry, uintptr_t address)
{
  current_stages.acquired = Clock::now();
  current_stages.loan_id = id;
  current_stages.entry = entry;
  current_stages.address = address;
}

struct Record
{
  int64_t time;
  std::array<uintptr_t, 3> addresses;
};
using Records = std::array<Record, frames>;
Record record(const Path & msg, Clock::time_point now)
{
  return {
    std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count(),
    {reinterpret_cast<uintptr_t>(&msg), reinterpret_cast<uintptr_t>(msg.poses.data()),
     reinterpret_cast<uintptr_t>(msg.poses.back().header.frame_id.data())}};
}
void save(const char * file, const Records & rows)
{
  std::ofstream out(file);
  out.exceptions(std::ios::failbit | std::ios::badbit);
  for (const auto & r : rows)
    out << r.time << ' ' << r.addresses[0] << ' ' << r.addresses[1] << ' ' << r.addresses[2]
        << '\n';
  out.close();
}
void check(bool ok, const char * what)
{
  if (!ok) throw std::runtime_error(what);
}
void verify(const Path & msg, size_t seq)
{
  check(
    msg.poses.size() == poses && msg.header.stamp.sec == static_cast<int32_t>(seq),
    "missing or reordered path");
  const auto label = std::string(80, 'A' + seq % 26);
  for (size_t i = 0; i < poses; ++i) {
    check(
      msg.poses[i].header.frame_id == label && msg.poses[i].pose.position.x == seq * poses + i,
      "invalid nested pose");
  }
}
Path * loan_ptr(agnocast::ipc_shared_ptr<Path> & loan)
{
  return loan.get();
}
Path * loan_ptr(rclcpp::LoanedMessage<Path> & loan)
{
  return &loan.get();
}
template <class Pub>
void send(Pub & pub)
{
  const auto deadline = Clock::now() + 10s;
  while (pub.get_subscription_count() != 1 || !std::filesystem::exists("ready")) {
    check(Clock::now() < deadline, "unmatched reader");
    std::this_thread::sleep_for(10ms);
  }
  Records rows;
  auto start = Clock::now() + 100ms;
  for (size_t i = 0; i < frames; ++i) {
    std::this_thread::sleep_until(start + i * 100ms);
    auto loan = pub.borrow_loaned_message();
    Path & msg = *loan_ptr(loan);
    msg.header.stamp.sec = i;
    msg.poses.resize(poses);
    for (size_t j = 0; j < poses; ++j) {
      msg.poses[j].header.frame_id.assign(80, 'A' + i % 26);
      msg.poses[j].pose.position.x = i * poses + j;
    }
    rows[i] = record(msg, Clock::now());
    pub.publish(std::move(loan));
  }
  save("writer", rows);
  while (!std::filesystem::exists("released")) {
    check(Clock::now() < start + 15s, "reader did not release final sample");
    std::this_thread::sleep_for(10ms);
  }
}
void save_native_stages(const std::array<NativeStages, frames> & stages)
{
  std::ofstream out("native-stages");
  out.exceptions(std::ios::failbit | std::ios::badbit);
  for (const auto & row : stages) {
    check(row.wake_id == row.loan_id, "native wake belongs to another subscription");
    out << row.entry << ' ' << row.address << ' '
        << std::chrono::duration_cast<std::chrono::nanoseconds>(row.wake.time_since_epoch()).count()
        << ' '
        << std::chrono::duration_cast<std::chrono::nanoseconds>(row.acquired.time_since_epoch())
             .count()
        << '\n';
  }
}
template <class Ptr, class Exec>
struct Receiver
{
  Exec & exec;
  Ptr held{};
  Records rows{};
  size_t count = 0;
  std::array<NativeStages, frames> stages{};
  bool trace = false;
  void receive(Ptr msg)
  {
    const auto now = Clock::now();
    check(count < frames, "extra frame");
    verify(*msg, count);
    rows[count] = record(*msg, now);
    if (trace) stages[count] = current_stages;
    if (count) verify(*held, count - 1);
    held = std::move(msg);
    if (++count == frames) exec.cancel();
  }
  void run()
  {
    std::ofstream("ready").close();
    exec.spin();
    check(count == frames, "incomplete run");
    verify(*held, frames - 1);
    held.reset();
    save("reader", rows);
    if (trace) save_native_stages(stages);
    std::ofstream("released").close();
  }
};
void native(bool writer, bool trace)
{
  auto node = std::make_shared<agnocast::Node>(writer ? "path_writer" : "path_reader");
  if (writer) {
    agnocast::Publisher<Path> pub(
      node.get(), topic, rclcpp::QoS(4), {}, agnocast::PublisherRole::AgnocastOnly);
    send(pub);
  } else {
    agnocast::AgnocastOnlySingleThreadedExecutor exec;
    using Ptr = agnocast::ipc_shared_ptr<const Path>;
    Receiver<Ptr, decltype(exec)> receiver{exec};
    receiver.trace = trace;
    auto sub = std::make_shared<agnocast::Subscription<Path>>(
      node.get(), topic, rclcpp::QoS(4), [&](Ptr msg) { receiver.receive(std::move(msg)); },
      agnocast::SubscriptionOptions{}, agnocast::SubscriptionRole::AgnocastOnly);
    exec.add_node(node);
    receiver.run();
  }
}
// Diagnostic for one fixed subscription on one executor thread.
class PathExecutor : public rclcpp::executors::SingleThreadedExecutor
{
public:
  rclcpp::SubscriptionBase::SharedPtr only;
  void spin() override
  {
    if (!only) return SingleThreadedExecutor::spin();
    spinning.store(true);
    while (spinning.load() && rclcpp::ok()) {
      wait_for_work();
      if (wait_result_->kind() != rclcpp::WaitResultKind::Ready) continue;
      auto ready = wait_result_->next_ready_subscription();
      if (!ready) continue;
      check(ready == only, "unexpected ready subscription");
      execute_subscription(ready);
    }
  }
};
void middleware(bool writer, bool trace, bool single)
{
  check(std::string(rmw_get_implementation_identifier()) == "rmw_agnocast", "wrong RMW");
  auto node = std::make_shared<rclcpp::Node>(writer ? "path_writer" : "path_reader");
  if (writer) {
    auto pub = node->create_publisher<Path>(topic, 4);
    check(pub->can_loan_messages(), "writer cannot loan");
    send(*pub);
  } else {
    PathExecutor exec;
    Receiver<Path::ConstSharedPtr, decltype(exec)> receiver{exec};
    receiver.trace = trace;
    auto sub = node->create_subscription<Path>(
      topic, 4, [&](Path::ConstSharedPtr msg) { receiver.receive(std::move(msg)); });
    if (single) exec.only = sub;
    check(sub->can_loan_messages(), "reader cannot loan");
    exec.add_node(node);
    receiver.run();
  }
}
int main(int argc, char ** argv)
{
  try {
    check(argc == 3, "expected transport and role");
    const bool writer = std::string(argv[2]) == "writer";
    const bool trace = std::string(argv[1]) == "native-traced";
    if (trace || std::string(argv[1]) == "native") {
      agnocast::init(argc, argv);
      native(writer, trace);
      agnocast::shutdown();
    } else {
      rclcpp::init(argc, argv);
      middleware(writer, std::string(argv[1]) == "rmw-traced", std::string(argv[1]) == "single");
      rclcpp::shutdown();
    }
  } catch (const std::exception & e) {
    std::cerr << e.what() << std::endl;
    return 1;
  }
}
