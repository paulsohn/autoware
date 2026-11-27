# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Verify the active product in a ROS process outside the vendor ABI preload."""
import signal
import subprocess
import sys
import tempfile
import time

import rclpy
from rcl_interfaces.srv import GetParameters

program, temporary, model_root = sys.argv[1:]
rclpy.init()
peer = rclpy.create_node('pipeline_product_peer')
try:
    with tempfile.TemporaryFile(mode='w+') as log:
        process = subprocess.Popen([program, '--cache-dir', temporary], stdout=log, stderr=subprocess.STDOUT)
        try:
            client = peer.create_client(GetParameters, '/traffic_light_recognition/get_parameters')
            deadline = time.monotonic() + 30
            while not client.wait_for_service(timeout_sec=.2):
                if process.poll() is not None or time.monotonic() > deadline:
                    log.seek(0)
                    raise RuntimeError('Product node did not become ready: ' + log.read())
            names = ['whole_image_detector.precision', 'car_classifier.precision',
                     'pedestrian_classifier.precision', 'ml_model_path']
            future = client.call_async(GetParameters.Request(names=names))
            rclpy.spin_until_future_complete(peer, future, timeout_sec=10)
            if not future.done() or [p.string_value for p in future.result().values] != ['fp16'] * 3 + [model_root]:
                raise RuntimeError('Active product did not apply the selected precision/cache profile')
        finally:
            if process.poll() is None:
                process.send_signal(signal.SIGINT)
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
                raise RuntimeError('Product shutdown timed out')
            if process.returncode:
                log.seek(0)
                raise RuntimeError('Product node failed: ' + log.read())
finally:
    peer.destroy_node()
    rclpy.shutdown()

print('Active FP16 product parameters and clean shutdown verified.', flush=True)
