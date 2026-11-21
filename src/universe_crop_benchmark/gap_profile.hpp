#pragma once

#include <array>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>

namespace benchmark {
// Read the callback thread's cumulative runnable delay without machine-wide tracing.
class GapProfile {
public:
  explicit GapProfile(const char * path) : path_(path) {}
  ~GapProfile() { if (fd_ >= 0) close(fd_); }

  template<class Callback>
  void measure(int64_t stamp, Callback callback) {
    if (used_ == rows_.size()) throw std::runtime_error("unexpected extra crop");
    auto & row = rows_[used_++];
    row[0] = stamp;
    row[1] = clock_ns(CLOCK_REALTIME);
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
