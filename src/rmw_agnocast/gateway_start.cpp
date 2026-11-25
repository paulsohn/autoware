#include "standalone.hpp"

#include <poll.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <string>
#include <string_view>

extern char ** environ;

bool standalone::join_gateway(Context & context, size_t domain)
{
  const char * config = std::getenv("AGNOCAST_GATEWAY_CONFIG");
  if (!config || !*config) return true;
  const char * program = std::getenv("AGNOCAST_GATEWAY_EXECUTABLE");
  if (!program || program[0] != '/') {
    RMW_SET_ERROR_MSG("AGNOCAST_GATEWAY_EXECUTABLE must be the absolute installed topic_gateway path");
    return false;
  }
  int lease[2];
  if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, lease)) {
    RMW_SET_ERROR_MSG("could not create gateway context lease");
    return false;
  }
  const auto domain_arg = std::to_string(domain);
  const char * args[] = {program, "--join", config, domain_arg.c_str(), nullptr};
  std::vector<char *> env;
  for (auto p = environ; *p; ++p)
    if (std::string_view(*p).find("LD_PRELOAD=") != 0) env.push_back(*p);
  env.push_back(nullptr);
  posix_spawn_file_actions_t actions;
  int error = posix_spawn_file_actions_init(&actions);
  pid_t child;
  if (!error) {
    error = posix_spawn_file_actions_adddup2(&actions, lease[1], STDIN_FILENO);
    if (!error) error = posix_spawn(&child, program, &actions, nullptr, const_cast<char **>(args), env.data());
    posix_spawn_file_actions_destroy(&actions);
  }
  close(lease[1]);
  if (!error) {
    int status;
    pid_t result;
    do { result = waitpid(child, &status, 0); } while (result < 0 && errno == EINTR);
    if (result == child && WIFEXITED(status) && WEXITSTATUS(status) == 0) {
      char ack;
      if (recv(lease[0], &ack, 1, MSG_DONTWAIT) == 1 && ack == '+') {
        context.gateway_lease = lease[0];
        return true;
      }
    }
  }
  close(lease[0]);
  RMW_SET_ERROR_MSG("could not join a ready gateway for the configured route");
  return false;
}
