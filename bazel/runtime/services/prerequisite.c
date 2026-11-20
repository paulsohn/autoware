// Copyright 2026 Taeseung Sohn
// SPDX-License-Identifier: Apache-2.0
// Demonstrate readiness and lifetime dependency without an external runtime.
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static volatile sig_atomic_t running = 1;

static void stop(int signo)
{
  (void)signo;
  running = 0;
}

static void record(const char * name, const char * value)
{
  char path[512];
  snprintf(path, sizeof(path), "/run/autoware/%s", name);
  FILE * file = fopen(path, strcmp(name, "events") == 0 ? "a" : "w");
  if (!file || fprintf(file, "%s\n", value) < 0 || fclose(file) != 0) exit(1);
}

int main(void)
{
  signal(SIGTERM, stop);
  signal(SIGINT, stop);
  char pid[32];
  snprintf(pid, sizeof(pid), "%ld", (long)getpid());
  record("prerequisite.pid", pid);
  record("events", "prerequisite:starting");
  const char * gate = getenv("AUTOWARE_READY_GATE");
  while (running && gate && access(gate, F_OK) != 0) usleep(100000);
  if (!running) return 0;
  record("ready", "ready");
  record("events", "prerequisite:ready");
  const char * fd = getenv("AUTOWARE_NOTIFY_FD");
  if (fd && write(atoi(fd), "\n", 1) != 1) return 1;
  const char * notify = getenv("NOTIFY_SOCKET");
  if (notify) {
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    if (strlen(notify) >= sizeof(address.sun_path)) return 1;
    strcpy(address.sun_path, notify);
    if (*notify == '@') address.sun_path[0] = '\0';
    int sock = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    socklen_t length = offsetof(struct sockaddr_un, sun_path) + strlen(notify) + (*notify != '@');
    if (sock < 0 || sendto(sock, "READY=1", 7, 0, (struct sockaddr *)&address, length) != 7)
      return 1;
    close(sock);
  }
  while (running) usleep(100000);
  unlink("/run/autoware/ready");
  record("events", "prerequisite:stopped");
  return 0;
}
