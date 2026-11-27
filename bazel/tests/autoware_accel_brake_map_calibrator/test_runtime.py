# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Replay actuator feedback into the real calibrator and export changed maps."""
import csv
import math
from pathlib import Path
import signal
import subprocess
import tempfile
import time
import unittest

from ament_index_python.packages import get_package_prefix, get_package_share_directory
from autoware_vehicle_msgs.msg import SteeringReport, VelocityReport
from geometry_msgs.msg import TransformStamped
from nav_msgs.msg import OccupancyGrid
from std_msgs.msg import Float32MultiArray
from tf2_msgs.msg import TFMessage
from tier4_external_api_msgs.msg import CalibrationStatus
from tier4_vehicle_msgs.msg import ActuationCommandStamped, ActuationStatusStamped
from tier4_vehicle_msgs.srv import UpdateAccelBrakeMap
import rclpy
from rclpy.qos import QoSProfile, DurabilityPolicy

PACKAGE = 'autoware_accel_brake_map_calibrator'


def numeric_map(path):
    with path.open() as stream:
        rows = list(csv.reader(stream))
    return [[float(value) for value in row[1:]] for row in rows[1:]]


class Calibrator(unittest.TestCase):
    def test_feedback_sources_and_export(self):
        for source, pitch in [('status', 'tf'), ('command', 'none')]:
            with self.subTest(source=source, pitch=pitch):
                self.run_source(source, pitch)

    def run_source(self, source, pitch):
        rclpy.init()
        peer = rclpy.create_node('calibrator_peer')
        namespace = '/bazel_calibration_' + source
        prefix = namespace + '/accel_brake_map_calibrator'
        originals, updates, statuses, original_grids, updated_grids = [], [], [], [], []
        subscriptions = [peer.create_subscription(Float32MultiArray, prefix + '/debug/original_raw_map', originals.append, 10),
                         peer.create_subscription(Float32MultiArray, prefix + '/output/update_raw_map', updates.append, 10),
                         peer.create_subscription(CalibrationStatus, '/accel_brake_map_calibrator/output/calibration_status', statuses.append, 10),
                         peer.create_subscription(OccupancyGrid, prefix + '/debug/original_occ_map', original_grids.append, 10),
                         peer.create_subscription(OccupancyGrid, prefix + '/debug/update_occ_map', updated_grids.append, 10)]
        vel_pub = peer.create_publisher(VelocityReport, prefix + '/input/velocity', 1)
        steer_pub = peer.create_publisher(SteeringReport, prefix + '/input/steer', 1)
        typ = ActuationStatusStamped if source == 'status' else ActuationCommandStamped
        actuation_pub = peer.create_publisher(typ, prefix + '/input/actuation_' + ('status' if source == 'status' else 'cmd'), 1)
        tf_pub = peer.create_publisher(TFMessage, '/tf_static', QoSProfile(depth=1, durability=DurabilityPolicy.TRANSIENT_LOCAL))
        client = peer.create_client(UpdateAccelBrakeMap, prefix + '/input/update_map_dir')
        maps = (Path(get_package_share_directory('autoware_raw_vehicle_cmd_converter')) / 'data/default').resolve()
        config = (Path(get_package_share_directory(PACKAGE)) / 'config/accel_brake_map_calibrator.param.yaml').resolve()
        executable = (Path(get_package_prefix(PACKAGE)) / 'lib' / PACKAGE / 'accel_brake_map_calibrator').resolve()

        def publish():
            stamp = peer.get_clock().now().to_msg()
            velocity = VelocityReport(longitudinal_velocity=20.0 / 3.6)
            velocity.header.stamp = stamp
            vel_pub.publish(velocity)
            steer_pub.publish(SteeringReport(stamp=stamp))
            msg = typ()
            msg.header.stamp = stamp
            if source == 'status':
                msg.status.accel_status = 0.1
            else:
                msg.actuation.accel_cmd = 0.1
            actuation_pub.publish(msg)
            transform = TransformStamped(child_frame_id='base_link')
            transform.header.frame_id = 'map'
            transform.header.stamp = stamp
            transform.transform.rotation.w = 1.0
            tf_pub.publish(TFMessage(transforms=[transform]))

        try:
            with tempfile.TemporaryDirectory() as directory, tempfile.TemporaryFile(mode='w+') as log:
                destination = Path(directory)
                log_file = destination / 'log.csv'
                args = [str(executable), '--ros-args', '-r', '__ns:=' + namespace,
                        '--params-file', str(config)]
                for key, value in {'csv_default_map_dir': str(maps), 'csv_calibrated_map_dir': directory,
                                   'output_log_file': str(log_file), 'pedal_accel_graph_output': 'true',
                                   'accel_brake_value_source': source, 'get_pitch_method': pitch}.items():
                    args += ['-p', key + ':=' + value]
                process = subprocess.Popen(args, stdout=log, stderr=subprocess.STDOUT)

                def output():
                    log.seek(0)
                    return log.read()

                def wait(predicate):
                    deadline = time.monotonic() + 15
                    while time.monotonic() < deadline:
                        self.assertIsNone(process.poll(), output())
                        publish()
                        rclpy.spin_once(peer, timeout_sec=0.03)
                        if predicate():
                            return
                    self.fail('Calibration condition absent: ' + output())

                def export(path):
                    wait(client.service_is_ready)
                    future = client.call_async(UpdateAccelBrakeMap.Request(path=str(path)))
                    wait(future.done)
                    return future.result()

                try:
                    wait(lambda: bool(originals) and bool(updates) and len(originals[-1].data) == len(updates[-1].data)
                         and any(abs(a - b) > 1e-5 for a, b in zip(originals[-1].data, updates[-1].data))
                         and bool(original_grids) and bool(updated_grids) and original_grids[-1].data != updated_grids[-1].data)
                    self.assertFalse(export(destination / 'missing/subdir').success)
                    self.assertTrue(export(destination).success)
                    for kind in ['accel', 'brake']:
                        path = destination / (kind + '_map.csv')
                        self.assertTrue(path.is_file())
                        values = numeric_map(path)
                        self.assertTrue(values)
                        self.assertTrue(all(math.isfinite(v) for row in values for v in row))
                    self.assertNotEqual(numeric_map(destination / 'accel_map.csv'), numeric_map(maps / 'accel_map.csv'))
                    wait(lambda: any(m.target == CalibrationStatus.ACCEL_BRAKE_MAP and m.status != CalibrationStatus.UNAVAILABLE for m in statuses))
                finally:
                    if process.poll() is None:
                        process.send_signal(signal.SIGINT)
                    try:
                        process.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
                        self.fail('Calibrator shutdown timed out: ' + output())
                    self.assertEqual(process.returncode, 0, output())
                with log_file.open() as stream:
                    rows = list(csv.DictReader(stream))
                self.assertTrue(rows)
                self.assertIn('final_accel', rows[0])
        finally:
            peer.destroy_node()
            rclpy.shutdown()


if __name__ == '__main__':
    unittest.main()
