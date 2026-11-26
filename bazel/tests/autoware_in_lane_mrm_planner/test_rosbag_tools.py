# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Record actual DDS samples and analyze the resulting bag with installed tools."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest

from autoware_planning_msgs.msg import Trajectory, TrajectoryPoint
from nav_msgs.msg import Odometry
import rclpy

RECORD, ANALYZE, ROS2 = [str(Path(sys.argv.pop(1)).resolve()) for _ in range(3)]


class RosbagTools(unittest.TestCase):
    def test_record_and_analyze(self):
        rclpy.init()
        peer = rclpy.create_node('in_lane_rosbag_peer')
        pub = peer.create_publisher(Trajectory, '/mrm/planning/in_lane_mrm_planner/output/trajectory', 10)
        odom_pub = peer.create_publisher(Odometry, '/mrm/localization/kinematic_state', 10)
        trajectory = Trajectory()
        trajectory.header.frame_id = 'map'
        for i in range(60):
            point = TrajectoryPoint()
            point.pose.position.x = i * 0.5
            point.pose.orientation.w = 1.0
            point.longitudinal_velocity_mps = 2.0 if i < 40 else 0.0
            trajectory.points.append(point)
        odom = Odometry()
        odom.header.frame_id = 'map'
        odom.pose.pose.orientation.w = 1.0
        odom.twist.twist.linear.x = 2.0
        try:
            with tempfile.TemporaryDirectory() as work:
                work = Path(work)
                env = os.environ.copy()
                env['PATH'] = str(Path(ROS2).parent) + os.pathsep + env.get('PATH', '/usr/bin:/bin')
                env['MPLCONFIGDIR'] = str(work / 'matplotlib')
                env['LD_LIBRARY_PATH'] = os.pathsep.join(str(Path(p).resolve()) for p in env['LD_LIBRARY_PATH'].split(os.pathsep) if p)
                for command in [RECORD, ANALYZE]:
                    help_result = subprocess.run([command, '--help'], env=env, text=True, capture_output=True, timeout=30)
                    self.assertEqual(help_result.returncode, 0, help_result.stdout + help_result.stderr)
                    self.assertIn('usage:', help_result.stdout)
                with tempfile.TemporaryFile(mode='w+') as log:
                    process = subprocess.Popen([RECORD, '--output-dir', str(work), '--bag-name', 'recorded', '--duration', '3', '--countdown', '0'], env=env, stdout=log, stderr=subprocess.STDOUT)
                    try:
                        deadline = time.monotonic() + 35
                        while process.poll() is None and time.monotonic() < deadline:
                            stamp = peer.get_clock().now().to_msg()
                            trajectory.header.stamp = stamp
                            odom.header.stamp = stamp
                            pub.publish(trajectory)
                            odom_pub.publish(odom)
                            rclpy.spin_once(peer, timeout_sec=0.05)
                        log.seek(0)
                        self.assertEqual(process.poll(), 0, log.read())
                    finally:
                        if process.poll() is None:
                            process.kill()
                            process.wait()
                bag = work / 'recorded'
                self.assertTrue((bag / 'metadata.yaml').is_file())
                report = work / 'report'
                result = subprocess.run([ANALYZE, str(bag), '--output-dir', str(report), '--no-show'], env=env, text=True, capture_output=True, timeout=45)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertGreater(len((report / 'timeline.csv').read_text().splitlines()), 2)
                self.assertGreater((report / 'in_lane_mrm_planner_analysis.png').stat().st_size, 1000)
        finally:
            peer.destroy_node()
            rclpy.shutdown()


if __name__ == '__main__':
    unittest.main()
