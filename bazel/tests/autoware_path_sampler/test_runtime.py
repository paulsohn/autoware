# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Run the indexed sampler with real DDS inputs and inspect its trajectory."""

import math
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time
import unittest

from ament_index_python.packages import get_package_share_directory
from autoware_perception_msgs.msg import PredictedObjects
from autoware_planning_msgs.msg import Path as RosPath, PathPoint, Trajectory
from geometry_msgs.msg import Point
from nav_msgs.msg import Odometry
from rcl_interfaces.msg import Parameter, ParameterType, ParameterValue
from rcl_interfaces.srv import SetParameters
import rclpy

EXECUTABLE = str(Path(sys.argv.pop(1)).resolve())


class PathSamplerRuntime(unittest.TestCase):
    def test_sampling_and_stop_fallback(self):
        os.environ['ROS_DOMAIN_ID'] = '75'
        rclpy.init()
        node = rclpy.create_node('path_sampler_peer')
        prefix = f'/sampler_{os.getpid()}/path_sampler'
        args = [EXECUTABLE, '--ros-args', '-r', '__ns:=' + prefix.rsplit('/', 1)[0]]
        for package, configs in [
            ('autoware_path_sampler', ['path_sampler.param.yaml']),
            ('autoware_test_utils', ['test_vehicle_info.param.yaml', 'test_nearest_search.param.yaml']),
        ]:
            share = Path(get_package_share_directory(package))
            for config in configs:
                args += ['--params-file', str(share / 'config' / config)]
        # A small candidate family exercises both algorithms without a large search.
        args += ['-p', 'sampling.target_lengths:=[10.0]', '-p', 'sampling.target_lateral_positions:=[0.0]',
                 '-p', 'sampling.frenet.target_lateral_velocities:=[0.0]',
                 '-p', 'sampling.bezier.nb_k:=2', '-p', 'sampling.bezier.nb_t:=2',
                 '-p', 'path_reuse.maximum_lateral_deviation:=0.001']
        path_pub = node.create_publisher(RosPath, prefix + '/input/path', 1)
        odom_pub = node.create_publisher(Odometry, prefix + '/input/odometry', 1)
        objects_pub = node.create_publisher(PredictedObjects, prefix + '/input/objects', 1)
        outputs = []
        subscription = node.create_subscription(Trajectory, prefix + '/output/path', outputs.append, 1)
        update = node.create_client(SetParameters, prefix + '/set_parameters')
        odom = Odometry()
        odom.header.frame_id = 'map'
        odom.pose.pose.orientation.w = 1.0
        odom.twist.twist.linear.x = 2.0
        objects = PredictedObjects()
        objects.header.frame_id = 'map'
        path = RosPath()
        path.header.frame_id = 'map'
        for i in range(41):
            point = PathPoint()
            point.pose.position.x = float(i)
            point.pose.position.z = 0.25
            point.pose.orientation.w = 1.0
            point.longitudinal_velocity_mps = 2.0
            path.points.append(point)
            path.left_bound.append(Point(x=float(i), y=5.0))
            path.right_bound.append(Point(x=float(i), y=-5.0))
        try:
            with tempfile.TemporaryFile(mode='w+') as log:
                process = subprocess.Popen(args, stdout=log, stderr=subprocess.STDOUT)

                def output():
                    log.seek(0)
                    return log.read()

                def wait(condition, publish=None, timeout=25):
                    deadline = time.monotonic() + timeout
                    while time.monotonic() < deadline:
                        self.assertIsNone(process.poll(), output())
                        if condition():
                            return
                        if publish:
                            publish()
                        rclpy.spin_once(node, timeout_sec=0.05)
                    self.fail('Sampler timed out: ' + output())

                def publish():
                    odom_pub.publish(odom)
                    objects_pub.publish(objects)
                    path_pub.publish(path)

                def configure(frenet, bezier):
                    future = update.call_async(SetParameters.Request(parameters=[
                        Parameter(name='sampling.enable_' + name, value=ParameterValue(
                            type=ParameterType.PARAMETER_BOOL, bool_value=value))
                        for name, value in [('frenet', frenet), ('bezier', bezier)]]))
                    wait(future.done)
                    self.assertTrue(all(r.successful for r in future.result().results))

                try:
                    wait(lambda: update.service_is_ready() and path_pub.get_subscription_count() > 0
                         and odom_pub.get_subscription_count() > 0 and objects_pub.get_subscription_count() > 0
                         and subscription.get_publisher_count() > 0)
                    # Missing odometry/objects must not produce a trajectory.
                    deadline = time.monotonic() + 0.5
                    while time.monotonic() < deadline:
                        path_pub.publish(path)
                        rclpy.spin_once(node, timeout_sec=0.05)
                    self.assertFalse(outputs)
                    for profile, (frenet, bezier) in enumerate([(True, True), (True, False), (False, True)], 1):
                        configure(frenet, bezier)
                        # Change lateral position to invalidate the previous path for each profile.
                        odom.pose.pose.position.y = 0.2 if profile == 2 else 0.0
                        path.header.stamp.sec = profile
                        outputs.clear()
                        wait(lambda: any(t.header.stamp.sec == profile for t in outputs), publish)
                        trajectory = next(t for t in reversed(outputs) if t.header.stamp.sec == profile)
                        self.assertEqual(trajectory.header.frame_id, 'map')
                        self.assertGreater(len(trajectory.points), 2)
                        self.assertGreater(trajectory.points[-1].pose.position.x, 8.0)
                        for point in trajectory.points:
                            self.assertTrue(all(math.isfinite(v) for v in [point.pose.position.x,
                                point.pose.position.y, point.pose.position.z, point.longitudinal_velocity_mps]))
                            self.assertAlmostEqual(point.longitudinal_velocity_mps, 2.0, places=5)
                            self.assertAlmostEqual(point.pose.position.z, 0.25, places=5)
                    # Move away from the previous path so disabling sampling cannot reuse it.
                    configure(False, False)
                    odom.pose.pose.position.y = 2.0
                    path.header.stamp.sec = 4
                    outputs.clear()
                    wait(lambda: any(t.header.stamp.sec == 4 and t.points and all(p.longitudinal_velocity_mps == 0.0 for p in t.points)
                                     for t in outputs), publish)
                    self.assertIn('libautoware_path_sampler.so', Path('/proc', str(process.pid), 'maps').read_text())
                finally:
                    if process.poll() is None:
                        process.send_signal(signal.SIGINT)
                    try:
                        process.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
                        self.fail('Sampler shutdown timed out: ' + output())
                    self.assertEqual(process.returncode, 0, output())
        finally:
            node.destroy_node()
            rclpy.shutdown()


if __name__ == '__main__':
    unittest.main()
