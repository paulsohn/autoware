#pragma once

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <sched.h>
#include <fstream>
#include <stdexcept>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>

namespace benchmark {
// Read the callback thread's cumulative runnable delay without machine-wide tracing.
class GapProfile {
public:
  explicit GapProfile(const char * path) : path_(path) {
    if (const char * mode = std::getenv("BENCH_FIXED_WORK")) {
      if (std::strcmp(mode, "work") == 0) rounds_ = 4096;
      else if (std::strcmp(mode, "empty") == 0) rounds_ = 0;
      else throw std::invalid_argument("expected BENCH_FIXED_WORK=work or empty");
    }
  }
  ~GapProfile() { if (fd_ >= 0) close(fd_); }

  template<class Callback>
  void measure(int64_t stamp, Callback callback) {
    if (used_ == rows_.size()) throw std::runtime_error("unexpected extra crop");
    auto & row = rows_[used_++];
    row[0] = stamp;
    row[1] = clock_ns(CLOCK_REALTIME);
    if (rounds_ >= 0) {
      auto & w = work_[used_ - 1];
      w.stamp = stamp;
      w.cpu_before = sched_getcpu();
      w.first = time_work();
      w.second = time_work();
      w.cpu_after = sched_getcpu();
    }
    if (fd_ < 0) {
      tid_ = gettid();
      fd_ = open("/proc/thread-self/schedstat", O_RDONLY | O_CLOEXEC);
    }
    if (tid_ != gettid()) throw std::runtime_error("crop thread changed during profile");
    row[2] = delay();
    row[3] = clock_ns(CLOCK_THREAD_CPUTIME_ID);
    callback();
    row[4] = clock_ns(CLOCK_THREAD_CPUTIME_ID);
    row[5] = delay();
    row[6] = clock_ns(CLOCK_REALTIME);
    if (used_ == rows_.size()) {
      if (rounds_ >= 0) write_work();
      std::ofstream out(path_);
      out.exceptions(std::ios::badbit | std::ios::failbit);
      out << "stamp,entry,delay_before,cpu_before,cpu_after,delay_after,exit\n";
      for (const auto & r : rows_) {
        for (size_t i = 0; i < r.size(); ++i) out << (i ? "," : "") << r[i];
        out << '\n';
      }
    }
  }

private:
  struct Block { int64_t wall, cpu; uint64_t result; };
  struct Work { int64_t stamp; int cpu_before, cpu_after; Block first, second; };
  Block time_work() const {
    uint64_t value = 1;
    const auto start = clock_ns(CLOCK_MONOTONIC_RAW);
    const auto cpu = clock_ns(CLOCK_THREAD_CPUTIME_ID);
    for (int i = 0; i < rounds_; ++i) {
      value ^= value >> 17;
      value *= UINT64_C(0x9e3779b97f4a7c15);
      // Keep each iteration in the timed region and dependent on the previous result.
      asm volatile("" : "+r"(value));
    }
    const auto elapsed_cpu = clock_ns(CLOCK_THREAD_CPUTIME_ID) - cpu;
    return {clock_ns(CLOCK_MONOTONIC_RAW) - start, elapsed_cpu, value};
  }
  void write_work() const {
    std::ofstream out(std::string(path_) + ".work");
    out.exceptions(std::ios::badbit | std::ios::failbit);
    out << "stamp,cpu_before,cpu_after,first_wall,first_cpu,first_result,second_wall,second_cpu,second_result\n";
    for (const auto & w : work_) {
      out << w.stamp << ',' << w.cpu_before << ',' << w.cpu_after;
      for (const auto & block : {w.first, w.second})
        out << ',' << block.wall << ',' << block.cpu << ',' << block.result;
      out << '\n';
    }
  }
  int rounds_ = -1;
  std::array<Work, 60> work_{};
  static int64_t clock_ns(clockid_t id) {
    timespec t{};
    if (clock_gettime(id, &t)) throw std::runtime_error("gap clock unavailable");
    return t.tv_sec * 1000000000LL + t.tv_nsec;
  }
  int64_t delay() const {
    char buf[128]{};
    unsigned long long runtime, wait;
    if (pread(fd_, buf, sizeof(buf)-1, 0) <= 0 || std::sscanf(buf, "%llu %llu", &runtime, &wait) != 2)
      throw std::runtime_error("cannot read callback thread schedstat");
    return wait;
  }
  const char * path_;
  pid_t tid_{};
  int fd_ = -1;
  size_t used_{};
  std::array<std::array<int64_t, 7>, 60> rows_{};
};
}
