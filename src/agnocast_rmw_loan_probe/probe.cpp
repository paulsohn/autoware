#include "loan_hooks.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

using namespace std::chrono_literals;
using probe::Message;
namespace fs = std::filesystem;
void check(bool ok, const char * why)
{
  if (!ok) throw std::runtime_error(why);
}
template <class Predicate>
void until(Predicate p, const char * why)
{
  auto end = std::chrono::steady_clock::now() + 20s;
  while (!p()) {
    check(std::chrono::steady_clock::now() < end, why);
    std::this_thread::sleep_for(10ms);
  }
}
std::string addresses(unsigned seq)
{
  return "addresses_" + std::to_string(seq);
}
void verify(const Message & msg, unsigned seq)
{
  uintptr_t object{}, points{}, string{};
  check(
    bool(std::ifstream(addresses(seq)) >> object >> points >> string), "missing sender addresses");
  check(
    object == reinterpret_cast<uintptr_t>(&msg) &&
      points == reinterpret_cast<uintptr_t>(msg.points.data()) &&
      string == reinterpret_cast<uintptr_t>(msg.header.frame_id.data()),
    "sender allocation mismatch");
  check(
    msg.header.frame_id == std::string(96, 'A' + seq) && msg.points.size() == 129,
    "bad trajectory");
  for (unsigned i = 0; i < msg.points.size(); ++i)
    check(msg.points[i].pose.position.x == seq * 1000 + i, "corrupt trajectory points");
}
struct CallbackError
{
};
void run(bool writer, bool stock)
{
  auto node = std::make_shared<rclcpp::Node>(writer ? "native_writer" : "native_reader");
  probe::attach(*node, writer);
  if (writer) {
    auto pub = node->create_publisher<Message>("trajectory_loan_probe", 4);
    check(pub->can_loan_messages(), "publisher cannot loan");
    until([&] { return pub->get_subscription_count() == 1; }, "no subscriber");
    for (unsigned seq = 1; seq <= (stock ? 1u : 3u); ++seq) {
      auto loan = pub->borrow_loaned_message();
      auto & msg = loan.get();
      msg.header.frame_id.assign(96, 'A' + seq);
      msg.points.resize(1);
      auto first = reinterpret_cast<uintptr_t>(msg.points.data());
      msg.points.reserve(msg.points.capacity() + 128);
      check(first != reinterpret_cast<uintptr_t>(msg.points.data()), "vector did not reallocate");
      msg.points.resize(129);
      for (unsigned i = 0; i < msg.points.size(); ++i)
        msg.points[i].pose.position.x = seq * 1000 + i;
      std::ofstream(addresses(seq)) << reinterpret_cast<uintptr_t>(&msg) << ' '
                                    << reinterpret_cast<uintptr_t>(msg.points.data()) << ' '
                                    << reinterpret_cast<uintptr_t>(msg.header.frame_id.data());
      pub->publish(std::move(loan));
      until([&] { return fs::exists("done_" + std::to_string(seq)); }, "reader acknowledgement");
    }
    until([] { return fs::exists("finished"); }, "reader final release");
  } else {
    Message::ConstSharedPtr held;
    unsigned calls{};
    auto sub = node->create_subscription<Message>(
      "trajectory_loan_probe", 4, [&](Message::ConstSharedPtr m) {
        verify(*m, ++calls);
        if (calls == 1) held = std::move(m);
        if (calls == 3) throw CallbackError{};
      });
    check(sub->can_loan_messages(), "subscriber cannot loan");
    rclcpp::executors::SingleThreadedExecutor exec;
    exec.add_node(node);
    for (unsigned seq = 1; seq <= (stock ? 1u : 3u); ++seq) {
      bool threw = false;
      until(
        [&] {
          try {
            exec.spin_some();
          } catch (const CallbackError &) {
            threw = true;
          }
          return calls == seq;
        },
        "trajectory callback");
      check(threw == (seq == 3), "exception path mismatch");
      check(probe::returns() == (stock ? 1u : seq - 1), "loan returned at wrong time");
      if (!stock) verify(*held, 1);
      std::ofstream("done_" + std::to_string(seq)) << "ready";
    }
    exec.remove_node(node);
    auto handle = std::weak_ptr<rcl_subscription_t>(sub->get_subscription_handle());
    sub.reset();
    node.reset();
    if (!stock) {
      check(!handle.expired(), "return context destroyed while A retained");
      std::thread release([&] { held.reset(); });
      release.join();
      check(probe::returns() == 3 && handle.expired(), "final loan return/teardown mismatch");
    } else {
      held.reset();  // The stock executor already returned A. Never dereference it here.
    }
    std::cout << (stock ? "stock: A returned at callback exit while still retained"
                        : "patched: A survived B and throwing C; final release returned A once "
                          "after subscription teardown")
              << std::endl;
    std::ofstream("finished") << "done";
  }
  probe::detach();
}
int main(int argc, char ** argv)
{
  try {
    rclcpp::init(argc, argv);
    check(argc == 3, "expected writer/reader and stock/patched");
    run(std::string(argv[1]) == "writer", std::string(argv[2]) == "stock");
    rclcpp::shutdown();
  } catch (const std::exception & e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
