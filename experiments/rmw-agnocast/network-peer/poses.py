"""Ordinary ROS publisher used by the isolated peer for the native ingress check."""
import time
from pathlib import Path
import rclpy
from geometry_msgs.msg import PoseArray, Pose

rclpy.init()
node = rclpy.create_node("peer_poses")
pub = node.create_publisher(PoseArray, "/peer/poses", 4)
end = time.monotonic()+20
while not Path("/tmp/poses-go").exists() or not pub.get_subscription_count():
    if time.monotonic()>end: raise TimeoutError("native gateway did not subscribe")
    time.sleep(.02)
for seq in range(48):
    m = PoseArray()
    m.header.frame_id = f"lidar_{seq}"
    for i in range(0 if seq%8==0 else 1 if seq%3==0 else 1024):
        p = Pose()
        p.position.x = float(seq+i)
        p.orientation.w = 1.
        m.poses.append(p)
    m.header.stamp = node.get_clock().now().to_msg()
    pub.publish(m)
    time.sleep(.03)
time.sleep(.4)
node.destroy_node()
rclpy.shutdown()
