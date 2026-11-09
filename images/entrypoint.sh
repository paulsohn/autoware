#!/bin/bash
# Runtime image entrypoint: the Autoware install tree's environment, then the command (default: a shell).
set -e
# shellcheck disable=SC1091
. /opt/ros/lyrical/setup.sh
exec "$@"
