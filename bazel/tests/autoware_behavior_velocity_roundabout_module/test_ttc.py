# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0

import os
from pathlib import Path
import signal
import subprocess
import tempfile
import time
import unittest

from ament_index_python.packages import get_package_share_directory
from autoware_internal_debug_msgs.msg import Float64MultiArrayStamped
from PIL import Image
from rcl_interfaces.srv import GetParameters
import rclpy
from std_msgs.msg import MultiArrayDimension

TOPIC = '/planning/scenario_planning/lane_driving/behavior_planning/behavior_velocity_planner/debug/roundabout/'


def command():
    share = Path(get_package_share_directory('autoware_behavior_velocity_roundabout_module'))
    return str(share.parents[1] / 'lib/autoware_behavior_velocity_roundabout_module/ttc.py')


def environment(work):
    env = dict(os.environ)
    env['LD_LIBRARY_PATH'] = os.pathsep.join(str(Path(p).resolve())
        for p in env.get('LD_LIBRARY_PATH', '').split(os.pathsep) if p)
    env['MPLCONFIGDIR'] = str(Path(work) / 'matplotlib')
    env['PYTHONFAULTHANDLER'] = '1'
    return env


class TtcTool(unittest.TestCase):
    def test_indexed_command_help(self):
        with tempfile.TemporaryDirectory() as work:
            result = subprocess.run([command(), '--help'], cwd=work, env=environment(work),
                                    text=True, capture_output=True, timeout=25)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('--lane_id', result.stdout)
        self.assertIn('--save', result.stdout)

    def test_messages_render_and_save_gif(self):
        self.exercise_tool(True)

    def test_no_data_shutdown(self):
        self.exercise_tool(False)

    def exercise_tool(self, send_frames):
        self.assertTrue(os.environ.get('DISPLAY'), 'The TTC plotting gate requires an X11 display')
        rclpy.init()
        node = rclpy.create_node('ttc_fixture_peer')
        ego_pub = node.create_publisher(Float64MultiArrayStamped, TOPIC + 'ego_ttc', 1)
        object_pub = node.create_publisher(Float64MultiArrayStamped, TOPIC + 'object_ttc', 1)
        ready = node.create_client(GetParameters, '/ttc_visualizer/get_parameters')
        try:
            with tempfile.TemporaryDirectory() as work, tempfile.TemporaryFile(mode='w+') as log:
                process = subprocess.Popen([command(), '--lane_id', '42', '--save', '--gif', 'fixture'],
                    cwd=work, env=environment(work), stdout=log, stderr=subprocess.STDOUT)

                def output():
                    log.seek(0)
                    return log.read()

                try:
                    deadline = time.monotonic() + 25
                    while not (ego_pub.get_subscription_count() and object_pub.get_subscription_count()
                               and ready.service_is_ready()):
                        self.assertIsNone(process.poll(), output())
                        self.assertLess(time.monotonic(), deadline, output())
                        rclpy.spin_once(node, timeout_sec=0.05)
                    response = ready.call_async(GetParameters.Request(names=[]))
                    while not response.done():
                        self.assertIsNone(process.poll(), output())
                        self.assertLess(time.monotonic(), deadline, output())
                        rclpy.spin_once(node, timeout_sec=0.05)
                    self.assertIsNone(response.exception())
                    ego = Float64MultiArrayStamped()
                    ego.layout.dim = [MultiArrayDimension(size=5), MultiArrayDimension(size=6)]
                    ego.data = [42.] + [0.] * 5 + [float(i) for i in range(6)] + \
                        [float(2 * i) for i in range(6)] * 2 + [0.] * 6
                    objects = Float64MultiArrayStamped()
                    objects.layout.dim = [MultiArrayDimension(size=2), MultiArrayDimension(size=57)]
                    row = [4., 2., 0., 2., 4., 1., 1., 0., 3., 1., 4., 3., 6., 4., 2., 6., 2.]
                    row += [float(i) if j == 0 else 2. for i in range(20) for j in range(2)]
                    objects.data = [42.] + [0.] * 56 + row
                    deadline = time.monotonic() + (2 if send_frames else 0.1)
                    while time.monotonic() < deadline:
                        self.assertIsNone(process.poll(), output())
                        if send_frames:
                            ego_pub.publish(ego)
                            object_pub.publish(objects)
                        rclpy.spin_once(node, timeout_sec=0.1)
                finally:
                    if process.poll() is None:
                        process.send_signal(signal.SIGINT)
                    try:
                        process.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
                        self.fail('TTC shutdown timed out: ' + output())
                self.assertEqual(process.returncode, 0, output())
                if send_frames:
                    self.assertIn('saved fig', output())
                    with Image.open(Path(work) / 'fixture.gif') as image:
                        self.assertGreaterEqual(image.n_frames, 1)
                        self.assertGreater(image.width, 100)
                        self.assertGreater(image.height, 100)
                        self.assertTrue(any(hi > lo for lo, hi in image.convert('RGB').getextrema()))
                else:
                    self.assertFalse((Path(work) / 'fixture.gif').exists(), output())
        finally:
            node.destroy_node()
            rclpy.shutdown()


if __name__ == '__main__':
    unittest.main()
