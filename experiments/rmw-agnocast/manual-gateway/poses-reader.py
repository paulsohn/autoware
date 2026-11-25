import sys
import time
import rclpy
from geometry_msgs.msg import PoseArray
from rclpy.qos import qos_profile_sensor_data

rclpy.init()
node = rclpy.create_node("manual_ingress_witness")
count = int(sys.argv[1]) if len(sys.argv)>1 else 48
seen = []
def receive(msg):
    seq = len(seen)
    assert msg.header.frame_id == f"lidar_{seq}"
    assert len(msg.poses) == (0 if seq % 8 == 0 else 1 if seq % 3 == 0 else 1024)
    assert all(p.position.x == seq+i and p.orientation.w == 1 for i, p in enumerate(msg.poses))
    seen.append(seq)
    if count != 48:
        print(f"pose {seq} {time.time_ns()}", flush=True)

node.create_subscription(PoseArray, "/manual/local_poses", receive, qos_profile_sensor_data)
print("reader ready", flush=True)
end = time.monotonic()+25
while len(seen) < count and time.monotonic() < end:
    rclpy.spin_once(node, timeout_sec=.1)
assert seen == list(range(count)), seen
print(f"{count}/{count} nested PoseArrays verified")
node.destroy_node()
rclpy.shutdown()
