# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Exercise the real loader-to-visualizer graph and retained marker delivery."""

from contextlib import ExitStack
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time
import unittest

from autoware_map_msgs.msg import MapProjectorInfo
import rclpy
from rclpy.qos import DurabilityPolicy, QoSProfile
from visualization_msgs.msg import Marker, MarkerArray

VISUALIZER, LOADER, MAP = [str(Path(p).resolve()) for p in sys.argv[1:4]]
del sys.argv[1:4]


class VisualizerRuntime(unittest.TestCase):
    def test_markers_and_late_subscriber(self):
        os.environ['ROS_DOMAIN_ID'] = '95'
        environment = dict(os.environ)
        environment['ENABLE_AGNOCAST'] = environment['AUTOWARE_TEST_AGNOCAST']
        if environment['ENABLE_AGNOCAST'] == '1':
            environment['LD_PRELOAD'] = environment['AGNOCAST_TEST_HEAPHOOK']
        else:
            environment.pop('LD_PRELOAD', None)
        rclpy.init()
        node = rclpy.create_node('map_visualizer_runtime_peer')
        qos = QoSProfile(depth=1, durability=DurabilityPolicy.TRANSIENT_LOCAL)
        received, late = [], []
        node.create_subscription(MarkerArray, 'output/lanelet2_map_marker', received.append, qos)
        publisher = node.create_publisher(MapProjectorInfo, '/map/map_projector_info', qos)
        projector = MapProjectorInfo()
        projector.projector_type = MapProjectorInfo.MGRS
        projector.mgrs_grid = '54SUE'
        projector.vertical_datum = MapProjectorInfo.WGS84
        processes = []
        try:
            with ExitStack() as stack:
                logs = [stack.enter_context(tempfile.TemporaryFile(mode='w+')) for _ in range(2)]
                commands = [
                    [LOADER, '--ros-args', '-p', 'lanelet2_map_path:=' + MAP,
                     '-p', 'lanelet2_map_metadata_path:=""', '-p', 'center_line_resolution:=5.0',
                     '-p', 'use_waypoints:=true', '-p', 'allow_unsupported_version:=true',
                     '-p', 'enable_selected_map_loading:=false'],
                    [VISUALIZER, '--ros-args', '-r', 'input/lanelet2_map:=/map/vector_map'],
                ]
                def output():
                    for log in logs:
                        log.seek(0)
                    return '\n'.join(log.read() for log in logs)

                try:
                    for command, log in zip(commands, logs):
                        processes.append(subprocess.Popen(command, env=environment, stdout=log,
                                                          stderr=subprocess.STDOUT))
                    deadline = time.monotonic() + 30
                    while not received and time.monotonic() < deadline:
                        self.assertTrue(all(p.poll() is None for p in processes), output())
                        publisher.publish(projector)
                        rclpy.spin_once(node, timeout_sec=0.1)
                    self.assertTrue(received, output())
                    markers = received[-1].markers
                    self.assertTrue(markers)
                    self.assertTrue(all(m.header.frame_id == 'map' for m in markers))
                    self.assertTrue(any(m.ns == 'road_lanelets' and m.type == Marker.TRIANGLE_LIST
                                        and len(m.points) > 0 for m in markers))
                    self.assertTrue(any(m.ns == 'left_lane_bound' for m in markers))
                    # Stop the input source before subscribing: receipt must use retained output.
                    processes[0].send_signal(signal.SIGINT)
                    processes[0].wait(timeout=10)
                    self.assertEqual(processes[0].returncode, 0, output())
                    node.create_subscription(MarkerArray, 'output/lanelet2_map_marker', late.append, qos)
                    deadline = time.monotonic() + 15
                    while not late and time.monotonic() < deadline:
                        self.assertIsNone(processes[1].poll(), output())
                        rclpy.spin_once(node, timeout_sec=0.1)
                    self.assertTrue(late, output())
                    self.assertEqual(late[-1], received[-1])
                finally:
                    for process in processes:
                        if process.poll() is None:
                            process.send_signal(signal.SIGINT)
                    for process in processes:
                        try:
                            process.wait(timeout=10)
                        except subprocess.TimeoutExpired:
                            process.kill()
                            process.wait()
                            self.fail('Node failed to stop on SIGINT: ' + output())
                    for process in processes:
                        self.assertEqual(process.returncode, 0, output())
        finally:
            node.destroy_node()
            rclpy.shutdown()


if __name__ == '__main__':
    unittest.main()
