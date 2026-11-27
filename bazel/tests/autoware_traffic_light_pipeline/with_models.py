# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Prepare the real product profile, verify warm reuse, then run native suites."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

from python.runfiles import runfiles

program = runfiles.Create().Rlocation('_main/bazel/runtime/traffic_light_recognition')
if not program:
    raise RuntimeError('Declared traffic-light product entrypoint is missing')
with tempfile.TemporaryDirectory(dir=os.environ['TEST_TMPDIR']) as temporary:
    args = [program, '--cache-dir', temporary, '--prepare-only']
    first = subprocess.run(args, capture_output=True, text=True)
    if first.returncode:
        raise RuntimeError(first.stdout + first.stderr)
    report = json.loads(first.stdout.splitlines()[-1])
    engines = [Path(p) for p in report['engines']]
    before = [(p.stat().st_mtime_ns, hashlib.sha256(p.read_bytes()).hexdigest()) for p in engines]
    second = subprocess.run(args, capture_output=True, text=True)
    if second.returncode:
        raise RuntimeError(second.stdout + second.stderr)
    warm = json.loads(second.stdout.splitlines()[-1])
    if warm['model_root'] != report['model_root'] or before != [(p.stat().st_mtime_ns, hashlib.sha256(p.read_bytes()).hexdigest()) for p in engines]:
        raise RuntimeError('Warm preparation replaced or changed the accepted engines')
    # Exercise the active entrypoint too; the native suite below checks message flow.
    peer = runfiles.Create().Rlocation('_main/bazel/tests/autoware_traffic_light_pipeline/check_product')
    if not peer:
        raise RuntimeError('Declared ROS product probe is missing')
    ros_environment = dict(os.environ)
    ros_environment.pop('LD_PRELOAD', None)
    print('Cold preparation and unchanged warm engines verified.', flush=True)
    subprocess.run([peer, program, temporary, report['model_root']], env=ros_environment, check=True)
    env = dict(os.environ, TL_PIPELINE_TEST_DATA_DIR=report['model_root'])
    result = subprocess.run(sys.argv[1:], env=env)
    if result.returncode:
        sys.exit(result.returncode)
    xml = ET.parse(os.environ['XML_OUTPUT_FILE'])
    if xml.findall('.//skipped') or not xml.findall('.//testcase'):
        raise RuntimeError('Pipeline gate requires executed original and composed cases with no skips')
    print('Three FP16 engines prepared and reused; original and composed pipeline cases executed.')
