# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Exercise installed calibration reports, API data and ROS helper publishers."""
import csv
import os
from pathlib import Path
import queue
import signal
import sys
import subprocess
import tempfile
import threading
import time
from types import SimpleNamespace
import unittest
from unittest.mock import patch

# Initialize the plotting runtime before loading ROS native libraries.
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np

from rcl_interfaces.srv import GetParameters
from ament_index_python.packages import get_package_prefix, get_package_share_directory
from autoware_vehicle_msgs.msg import GearCommand, VelocityReport
from tier4_external_api_msgs.srv import GetAccelBrakeMapCalibrationData
from tier4_vehicle_msgs.msg import ActuationCommandStamped, ActuationStatusStamped
import rclpy
from rclpy.executors import SingleThreadedExecutor

from accel_tester import AccelTester
from actuation_cmd_publisher import ActuationCmdPublisher
from brake_tester import BrakeTester
from calc_utils import CalcUtils
import config as CF
from csv_reader import CSVReader
from delay_estimator import DelayEstimator
from sim_actuation_status_publisher import ActuationStatusPublisher
from view_plot import ViewPlot
from view_statistics import ViewPlot as StatisticsView

LAUNCH = str(Path(sys.argv.pop(1)).resolve())
PACKAGE = 'autoware_accel_brake_map_calibrator'
ROOT = Path.cwd().resolve()


class Tools(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.logs = self.directory / 'logs'
        self.logs.mkdir()
        self.log_file = self.logs / 'log.csv'
        keys = [CF.TS, CF.VEL, CF.RAW_ACC, CF.PITCH_ACC, CF.ACC, CF.A_PED, CF.B_PED,
                CF.A_PED_SPD, CF.B_PED_SPD, CF.PITCH, CF.JERK, CF.STEER]
        with self.log_file.open('w') as stream:
            writer = csv.DictWriter(stream, fieldnames=keys)
            writer.writeheader()
            i = 0
            for velocity in CF.VEL_LIST / 3.6:
                for pedal in CF.PEDAL_LIST:
                    for sample in range(8):
                        row = dict.fromkeys(keys, 0.0)
                        row.update({CF.TS: i * 0.1, CF.VEL: velocity, CF.ACC: pedal * 2 + sample * 0.01,
                                    CF.RAW_ACC: pedal * 2 + sample * 0.01,
                                    CF.A_PED: max(0, pedal), CF.B_PED: max(0, -pedal),
                                    CF.A_PED_SPD: sample * 0.01})
                        writer.writerow(row)
                        i += 1
        self.maps = (Path(get_package_share_directory('autoware_raw_vehicle_cmd_converter')) / 'data/default').resolve()
        self.bin = (Path(get_package_prefix(PACKAGE)) / 'lib' / PACKAGE).resolve()
        self.environment = dict(os.environ)
        self.environment.update(AMENT_PREFIX_PATH=str(ROOT), LD_LIBRARY_PATH=str(ROOT / 'lib'),
                                MPLBACKEND='Agg', MPLCONFIGDIR=str(self.directory / 'matplotlib'))

    def run_program(self, name, *args):
        result = subprocess.run([str(self.bin / name), *args], env=self.environment,
                                cwd=self.directory, capture_output=True, text=True, timeout=45)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result

    def test_statistics_and_reports(self):
        reader = CSVReader(str(self.log_file), csv_type='file')
        reader.removeUnusedData(0.1, 0.2, 0.02, 0.15, 0.7)
        data, _ = CalcUtils.create_2d_map(reader.getVelData(), reader.getPedalData(), reader.getAccData(),
                                        reader.getPitchData(), CF.VEL_LIST / 3.6, 0.556, CF.PEDAL_LIST, 0.03)
        counts, means, deviations = CalcUtils.create_stat_map(data)
        pedal = int(np.flatnonzero(np.isclose(CF.PEDAL_LIST, 0.1))[0])
        self.assertEqual(counts[pedal, 2], 8)
        self.assertAlmostEqual(means[pedal, 2], 0.235)
        self.assertGreater(deviations[pedal, 2], 0)
        self.run_program('log_analyzer.py', '--csv-file-dir', str(self.logs),
                         '--output-stat-csv', str(self.directory / 'statistics.csv'),
                         '--output-plot-file', str(self.directory / 'report.png'))
        self.assertTrue((self.directory / 'statistics.csv').is_file())
        self.assertEqual((self.directory / 'report.png').read_bytes()[:8], b'\x89PNG\r\n\x1a\n')
        rclpy.init(args=['--ros-args', '-p', 'log_file:=' + str(self.log_file)])
        try:
            constructors = [StatisticsView, DelayEstimator]
            for method in ['each_cell', 'four_cell']:
                args = SimpleNamespace(default_map_dir=str(self.maps) + '/', calibrated_map_dir=str(self.maps) + '/',
                                       method=method, scatter_only=False, log_file=str(self.log_file), min_vel_thr=0.1,
                                       vel_diff_thr=0.556, pedal_diff_thr=0.03, max_steer_thr=0.2, max_pitch_thr=0.02,
                                       max_jerk_thr=0.7, max_pedal_vel_thr=0.15)
                constructors.append(lambda args=args: ViewPlot(args))
            for index, constructor in enumerate(constructors):
                plt.close('all')
                node = constructor()
                try:
                    self.assertTrue(plt.get_fignums())
                    path = self.directory / f'view-{index}.png'
                    plt.gcf().savefig(path)
                    self.assertEqual(path.read_bytes()[:8], b'\x89PNG\r\n\x1a\n')
                finally:
                    node.destroy_node()
        finally:
            plt.close('all')
            rclpy.shutdown()

    def test_map_data_service_and_client(self):
        endpoint = '/api/external/get/accel_brake_map_calibrator/data'
        for method in ['each_cell', 'four_cell']:
            with self.subTest(method=method):
                rclpy.init()
                peer = rclpy.create_node('map_data_peer')
                client = peer.create_client(GetAccelBrakeMapCalibrationData, endpoint)
                args = [str(self.bin / 'new_accel_brake_map_server.py'), '--ros-args',
                        '-r', '/accel_brake_map_calibrator/get_data_service:=' + endpoint,
                        '-p', 'log_file:=' + str(self.log_file), '-p', 'calibration_method:=' + method,
                        '-p', '/accel_brake_map_calibrator/csv_default_map_dir:=' + str(self.maps) + '/',
                        '-p', '/accel_brake_map_calibrator/csv_calibrated_map_dir:=' + str(self.maps) + '/']
                try:
                    with tempfile.TemporaryFile(mode='w+') as log:
                        process = subprocess.Popen(args, env=self.environment, cwd=self.directory,
                                                   stdout=log, stderr=subprocess.STDOUT)

                        def output():
                            log.seek(0)
                            return log.read()

                        def wait(predicate):
                            deadline = time.monotonic() + 30
                            while time.monotonic() < deadline:
                                self.assertIsNone(process.poll(), output())
                                rclpy.spin_once(peer, timeout_sec=0.05)
                                if predicate():
                                    return
                            self.fail('Map data service unavailable: ' + output())

                        try:
                            wait(client.service_is_ready)
                            future = client.call_async(GetAccelBrakeMapCalibrationData.Request())
                            wait(future.done)
                            response = future.result()
                            self.assertIn(b'<svg', bytes(response.graph_image), output())
                            self.assertEqual(response.accel_map, (self.maps / 'accel_map.csv').read_text())
                            self.assertEqual(response.brake_map, (self.maps / 'brake_map.csv').read_text())
                            # The shipped client uses this API endpoint and saves the same artifacts.
                            self.run_program('plot_csv_client_node.py')
                            saved = self.directory / 'test_data_save'
                            self.assertIn('<svg', (saved / 'graph.svg').read_text())
                            self.assertEqual((saved / 'accel_map.csv').read_text(), response.accel_map)
                            self.assertEqual((saved / 'brake_map.csv').read_text(), response.brake_map)
                        finally:
                            if process.poll() is None:
                                process.send_signal(signal.SIGINT)
                            try:
                                process.wait(timeout=10)
                            except subprocess.TimeoutExpired:
                                process.kill()
                                process.wait()
                                self.fail('Map server shutdown timed out: ' + output())
                            self.assertIn(process.returncode, [0, -signal.SIGINT], output())
                finally:
                    peer.destroy_node()
                    rclpy.shutdown()

    def test_launcher_paths(self):
        rclpy.init()
        peer = rclpy.create_node('calibration_launch_peer')
        log_file = self.directory / 'native-log.csv'
        args = [LAUNCH, str((Path(get_package_share_directory(PACKAGE)) / 'launch/accel_brake_map_calibrator.launch.xml').resolve()),
                'rviz:=false', 'pedal_accel_graph_output:=false',
                'csv_default_map_dir:=' + str(self.maps) + '/',
                'csv_calibrated_map_dir:=' + str(self.directory) + '/',
                'output_log_file:=' + str(log_file)]
        try:
            with tempfile.TemporaryFile(mode='w+') as log:
                process = subprocess.Popen(args, env=self.environment, cwd=self.directory,
                                           stdout=log, stderr=subprocess.STDOUT)

                def output():
                    log.seek(0)
                    return log.read()

                def wait(predicate):
                    deadline = time.monotonic() + 25
                    while time.monotonic() < deadline:
                        self.assertIsNone(process.poll(), output())
                        rclpy.spin_once(peer, timeout_sec=0.05)
                        if predicate():
                            return
                    self.fail('Launch path contract absent: ' + output())

                try:
                    for node, names, expected in [
                        ('accel_brake_map_calibrator', ['output_log_file', 'csv_calibrated_map_dir'],
                         [str(log_file), str(self.directory) + '/']),
                        ('new_accel_brake_map_server', ['log_file', '/accel_brake_map_calibrator/csv_calibrated_map_dir', '/accel_brake_map_calibrator/csv_default_map_dir'],
                         [str(log_file), str(self.directory) + '/', str(self.maps) + '/']),
                    ]:
                        client = peer.create_client(GetParameters, '/' + node + '/get_parameters')
                        wait(client.service_is_ready)
                        future = client.call_async(GetParameters.Request(names=names))
                        wait(future.done)
                        self.assertEqual([v.string_value for v in future.result().values], expected)
                        peer.destroy_client(client)
                    self.assertTrue(log_file.is_file())
                finally:
                    if process.poll() is None:
                        process.send_signal(signal.SIGINT)
                    try:
                        process.wait(timeout=15)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
                        self.fail('Launch shutdown timed out: ' + output())
                    self.assertEqual(process.returncode, 0, output())
        finally:
            peer.destroy_node()
            rclpy.shutdown()

    def test_original_ros_helpers(self):
        rclpy.init()
        nodes = [rclpy.create_node('calibration_tools_peer'), ActuationCmdPublisher(), ActuationStatusPublisher()]
        peer = nodes[0]
        executor = SingleThreadedExecutor()
        for node in nodes:
            executor.add_node(node)
        commands, gears, statuses = [], [], []
        subscriptions = [peer.create_subscription(ActuationCommandStamped, '/control/command/actuation_cmd', commands.append, 10),
                         peer.create_subscription(GearCommand, '/control/command/gear_cmd', gears.append, 10),
                         peer.create_subscription(ActuationStatusStamped, '/vehicle/status/actuation_status', statuses.append, 10)]
        velocity_pub = peer.create_publisher(VelocityReport, '/vehicle/status/velocity_status', 1)

        def wait(predicate):
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline:
                velocity = VelocityReport(longitudinal_velocity=5.0)
                velocity.header.stamp = peer.get_clock().now().to_msg()
                velocity_pub.publish(velocity)
                executor.spin_once(timeout_sec=0.02)
                if predicate():
                    return
            self.fail('Original helper output absent')

        class EndInput(Exception):
            pass

        try:
            wait(lambda: bool(statuses) and bool(gears) and bool(commands))
            self.assertAlmostEqual(statuses[-1].status.accel_status, 0.5)
            self.assertEqual(gears[-1].command, GearCommand.DRIVE)
            for typ, field in [(AccelTester, 'accel_cmd'), (BrakeTester, 'brake_cmd')]:
                tester = typ()
                values = queue.Queue()
                errors = []
                stop = object()

                def controlled_input(prompt):
                    value = values.get(timeout=10)
                    if value is stop:
                        raise EndInput()
                    return str(value)

                def run():
                    try:
                        tester.run()
                    except EndInput:
                        pass
                    except Exception as error:
                        errors.append(error)

                try:
                    with patch('builtins.input', controlled_input):
                        worker = threading.Thread(target=run)
                        worker.start()
                        try:
                            for value, expected in [(1.2, 1.0), (-0.2, 0.0), (0.3, 0.3)]:
                                commands.clear()
                                values.put(value)
                                wait(lambda: any(abs(getattr(m.actuation, field) - expected) < 1e-5 for m in commands))
                        finally:
                            values.put(stop)
                            worker.join(timeout=12)
                            self.assertFalse(worker.is_alive())
                            self.assertFalse(errors, errors)
                finally:
                    tester.destroy_node()
        finally:
            executor.shutdown()
            for node in reversed(nodes):
                node.destroy_node()
            rclpy.shutdown()


if __name__ == '__main__':
    unittest.main()
