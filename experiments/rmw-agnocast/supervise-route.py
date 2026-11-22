"""Launch one explicit pilot route with ROS launch process supervision."""
import sys
import time
from pathlib import Path

from ament_index_python.packages import get_package_prefix
from launch import LaunchDescription, LaunchService
from launch.actions import ExecuteProcess, RegisterEventHandler
from launch.event_handlers import OnProcessExit, OnProcessStart

direction, source, target = sys.argv[1:]
assert direction in ("a2r", "r2a")
relay = ExecuteProcess(
    cmd=[str(Path(get_package_prefix("agnocast_rmw_loan_probe")) /
             "lib/agnocast_rmw_loan_probe/route_bridge"), direction, "cloud", source, target],
    additional_env={"RMW_IMPLEMENTATION": "rmw_agnocast", "AGNOCAST_RMW_HOST_CLOUD": "1",
                    "AGNOCAST_BRIDGE_MODE": "OFF", "AGNOCAST_NO_DISCOVERY_AGENT": "1",
                    "LD_PRELOAD": str(Path(get_package_prefix("agnocastlib")) / "lib/libagnocast_heaphook.so")},
    output="screen", respawn=True, respawn_delay=.25, respawn_max_retries=3)


def record(event, context):
    print("event", time.time_ns(), event.pid, getattr(event, "returncode", "started"), flush=True)


service = LaunchService(noninteractive=True)
service.include_launch_description(LaunchDescription([
    RegisterEventHandler(OnProcessStart(target_action=relay, on_start=record)),
    RegisterEventHandler(OnProcessExit(target_action=relay, on_exit=record)), relay]))
sys.exit(service.run())
