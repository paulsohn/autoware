#include <rclcpp/rclcpp.hpp>

#include <autoware_planning_msgs/msg/trajectory.hpp>

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
struct CallbackError
{
};
int main(int argc, char ** argv)
{
  try {
    check(argc == 3, "expected role and loan/copy mode");
    bool writer = std::string(argv[1]) == "writer", use_loan = std::string(argv[2]) == "loan";
    rclcpp::init(argc, argv);
    check(
      std::string(rmw_get_implementation_identifier()) == "rmw_agnocast", "wrong implementation");
    auto node = std::make_shared<rclcpp::Node>(writer ? "trajectory_writer" : "trajectory_reader");
    if (writer) {
      auto pub = node->create_publisher<Message>("trajectory_loan_probe", 4);
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
          Message msg;
          fill(msg, seq);
          pub->publish(msg);
          msg.points.clear();
        } else {
          auto loan = pub->borrow_loaned_message();
          auto & msg = loan.get();
          fill(msg, seq);
          std::ofstream("addresses" + std::to_string(seq))
            << reinterpret_cast<uintptr_t>(&msg) << ' '
            << reinterpret_cast<uintptr_t>(msg.points.data()) << ' '
            << reinterpret_cast<uintptr_t>(msg.header.frame_id.data());
          pub->publish(std::move(loan));
        }
        wait_file("done" + std::to_string(seq));
      }
    } else {
      Message::ConstSharedPtr held;
      int calls = 0;
      auto sub = node->create_subscription<Message>(
        "trajectory_loan_probe", 4, [&](Message::ConstSharedPtr m) {
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
          std::chrono::steady_clock::now() < deadline,
          "sample arrived only after executor timeout");
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
      std::cout << "rmw_agnocast " << argv[2]
                << ": verified four trajectories, retained A, throwing C and copied publish D"
                << std::endl;
    }
    rclcpp::shutdown();
  } catch (const std::exception & error) {
    std::cerr << error.what() << std::endl;
    return 1;
  }
}
