"""Control the order of a publisher count read and executor dispatch."""
from pathlib import Path
import sys
import time

import rclpy
from rclpy.event_handler import PublisherEventCallbacks
from std_msgs.msg import Int32

out = Path(sys.argv[1])
rclpy.init()
node = rclpy.create_node("match_status_probe")
events = []

def matched(info):
    events.append(info.current_count)
    print("callback", info.current_count, flush=True)

pub = node.create_publisher(Int32, "/match_probe", 4,
                            event_callbacks=PublisherEventCallbacks(matched=matched))
def spin(seconds):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        rclpy.spin_once(node, timeout_sec=.02)

deadline = time.monotonic() + 12
while not events and time.monotonic() < deadline:
    spin(.05)
assert events == [1]
pub.publish(Int32(data=1))
(out / "ready").touch()
# DDS receive threads continue while application dispatch is paused.
while not (out / "dispatch").exists() and time.monotonic() < deadline:
    time.sleep(.01)
assert (out / "dispatch").exists()
if sys.argv[2] == "query":
    print("before_dispatch", pub.get_subscription_count(), flush=True)
spin(.45)
print("after_dispatch", pub.get_subscription_count(), "events", events, flush=True)
(out / "observed").touch()
spin(3)
print("restored_count", pub.get_subscription_count(), "events", events, flush=True)
pub.publish(Int32(data=2))
spin(.2)
node.destroy_node()
rclpy.shutdown()
