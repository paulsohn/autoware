#include "standalone.hpp"

#include <poll.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>

extern char ** environ;

namespace
{
struct GatewayCommand
{
  std::string program, config, domain;
  std::vector<std::string> environment;
};

int acquire(const GatewayCommand & cmd, bool & automatic)
{
  std::vector<char *> env;
  for (const auto & entry : cmd.environment) env.push_back(const_cast<char *>(entry.c_str()));
  char capability[] = "AGNOCAST_GATEWAY_RECOVERY_LEASE=1";
  env.push_back(capability);
  env.push_back(nullptr);
  int lease[2];
  if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, lease)) {
    return -1;
  }
  const char * args[] = {cmd.program.c_str(), "--join", cmd.config.c_str(), cmd.domain.c_str(), nullptr};
  posix_spawn_file_actions_t actions;
  int error = posix_spawn_file_actions_init(&actions);
  pid_t child;
  if (!error) {
    error = posix_spawn_file_actions_adddup2(&actions, lease[1], STDIN_FILENO);
    if (!error) error = posix_spawn(&child, cmd.program.c_str(), &actions, nullptr, const_cast<char **>(args), env.data());
    posix_spawn_file_actions_destroy(&actions);
  }
  close(lease[1]);
  if (!error) {
    int status;
    pid_t result;
    do { result = waitpid(child, &status, 0); } while (result < 0 && errno == EINTR);
    if (result == child && WIFEXITED(status) && WEXITSTATUS(status) == 0) {
      char ack;
      if (recv(lease[0], &ack, 1, MSG_DONTWAIT) == 1 && (ack == '+' || ack == 'A')) {
        automatic = ack == 'A';
        return lease[0];
      }
    }
  }
  close(lease[0]);
  return -1;
}

int wait_descriptors(pollfd * fds, nfds_t n, int timeout)
{
  int rc;
  do { rc = poll(fds, n, timeout); } while (rc < 0 && errno == EINTR);
  return rc;
}

void monitor(standalone::Context & ctx, GatewayCommand cmd)
{
  pollfd stop{ctx.gateway_stop, POLLIN, 0};
  unsigned attempts = 0;
  while (true) {
    pollfd fds[] = {stop, {ctx.gateway_lease, POLLIN, 0}};
    if (wait_descriptors(fds, 2, -1) < 0 || fds[0].revents) return;
    close(ctx.gateway_lease);
    ctx.gateway_lease = -1;
    bool automatic = false;
    while (ctx.gateway_lease < 0 && attempts < 3) {
      if (wait_descriptors(&stop, 1, 100 << attempts) != 0) return;
      ++attempts;
      try {
        ctx.gateway_lease = acquire(cmd, automatic);
      } catch (const std::bad_alloc &) {
        std::fprintf(stderr, "rmw_agnocast: gateway recovery allocation failed\n");
        return;
      }
    }
    if (ctx.gateway_lease < 0) {
      std::fprintf(stderr, "rmw_agnocast: gateway recovery exhausted three attempts in this context\n");
      return;
    }
    std::fprintf(stderr, "rmw_agnocast: rejoined gateway on recovery attempt %u\n", attempts);
    if (!automatic) return;
  }
}
}  // namespace

standalone::Context::~Context()
{
  if (gateway_monitor.joinable()) {
    while (eventfd_write(gateway_stop, 1) < 0 && errno == EINTR) {}
    gateway_monitor.join();
  }
  if (gateway_stop >= 0) close(gateway_stop);
  if (gateway_lease >= 0) close(gateway_lease);
}

bool standalone::join_gateway(Context & context, size_t domain)
{
  const char * config = std::getenv("AGNOCAST_GATEWAY_CONFIG");
  if (!config || !*config) return true;
  const char * program = std::getenv("AGNOCAST_GATEWAY_EXECUTABLE");
  if (!program || program[0] != '/') {
    RMW_SET_ERROR_MSG("AGNOCAST_GATEWAY_EXECUTABLE must be the absolute installed topic_gateway path");
    return false;
  }
  try {
    GatewayCommand cmd{program, config, std::to_string(domain), {}};
    for (auto p = environ; *p; ++p)
      if (std::string_view(*p).find("LD_PRELOAD=") != 0 &&
          std::string_view(*p).find("AGNOCAST_GATEWAY_RECOVERY_LEASE=") != 0)
        cmd.environment.emplace_back(*p);
    bool automatic = false;
    context.gateway_lease = acquire(cmd, automatic);
    if (context.gateway_lease < 0) {
      RMW_SET_ERROR_MSG("could not join a ready gateway for the configured route");
      return false;
    }
    if (automatic) {
      context.gateway_stop = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
      if (context.gateway_stop < 0) {
        RMW_SET_ERROR_MSG("could not create gateway recovery stop descriptor");
        return false;
      }
      context.gateway_monitor = std::thread(monitor, std::ref(context), std::move(cmd));
    }
    return true;
  } catch (const std::exception & error) {
    RMW_SET_ERROR_MSG(error.what());
    return false;
  }
}
