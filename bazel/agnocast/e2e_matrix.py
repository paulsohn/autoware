# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Run the upstream communication matrices with isolated, immutable fixtures."""

import itertools
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import time
import xml.etree.ElementTree as ET

from rcr_launch_guard import launch_process_guard


def configurations(kind):
    if kind == '1to1':
        keys = ('use_agnocast_pub', 'launch_pub_before_sub', 'pub_qos_depth',
                'pub_transient_local', 'sub_qos_depth', 'sub_transient_local',
                'use_take_sub')
        values = ([True, False], [True, False], [1, 10], [True, False],
                  [1, 10], [True, False], [True, False])
        return [dict(zip(keys, row)) for row in itertools.product(*values)]
    if kind == '2to2':
        layouts = ('PPSS', 'PP|SS', 'P|PSS', 'PPS|S', 'P|P|SS', 'P|PS|S',
                   'PP|S|S', 'P|P|S|S')
        result = []
        for mode, layout in itertools.product(('agno2agno', 'ros2agno', 'agno2ros'), layouts):
            config = {'test_mode': mode}
            groups = layout.lower().split('|')
            config.update({f'container{i}': list(groups[i]) if i < len(groups) else []
                           for i in range(4)})
            result.append(config)
        return result
    raise ValueError(f'Unknown matrix: {kind}')


def main():
    kind, source, runner, adapter = sys.argv[1:]
    source, runner, adapter = map(lambda p: Path(p).resolve(), (source, runner, adapter))
    configs = configurations(kind)
    assert len(configs) == (128 if kind == '1to1' else 24)
    base = Path(os.environ['TEST_UNDECLARED_OUTPUTS_DIR']) / kind
    base.mkdir(parents=True, exist_ok=True)
    report = ET.Element('testsuite', name=f'agnocast_{kind}')
    environment = dict(os.environ)
    environment.pop('LD_PRELOAD', None)
    # Carry Bazel's import closure into the directly invoked pinned interpreter.
    environment['PYTHONPATH'] = os.pathsep.join(sys.path)
    environment['ROS_DOMAIN_ID'] = '72'
    failures = 0
    for index, config in enumerate(configs):
        case_id = f'{kind}_{index:03d}'
        directory = base / case_id
        directory.mkdir()
        case_source = directory / source.name
        shutil.copyfile(source, case_source)
        (directory / f'config_test_{kind}.yaml').write_text(json.dumps(config))
        case_xml = directory / 'result.xml'
        environment['AGNOCAST_LAUNCH_SOURCE'] = str(case_source)
        environment['E2E_TOPIC_NAME'] = f'/bazel_{case_id}'
        print(f'[{index + 1}/{len(configs)}] {case_id}: {json.dumps(config)}', flush=True)
        started = time.monotonic()
        with (directory / 'launch.log').open('w') as log:
            with subprocess.Popen(
                    [sys.executable, str(runner), str(adapter), '--junit-xml', str(case_xml)],
                    env=environment, stdout=log, stderr=subprocess.STDOUT,
                    start_new_session=True) as process:
                try:
                    error = '' if process.wait(timeout=180) == 0 else f'exit code {process.returncode}'
                except subprocess.TimeoutExpired:
                    error = 'launch scenario timed out after 180 seconds'
                    os.killpg(process.pid, signal.SIGTERM)
                    try:
                        process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        os.killpg(process.pid, signal.SIGKILL)
                        process.wait()
        if case_xml.exists():
            for case in ET.parse(case_xml).getroot().iter('testcase'):
                case.set('classname', case_id + '.' + case.get('classname', ''))
                report.append(case)
        if error:
            tail = '\n'.join((directory / 'launch.log').read_text().splitlines()[-100:])
            print(tail, flush=True)
            case = ET.SubElement(report, 'testcase', name=case_id,
                                 time=str(time.monotonic() - started))
            ET.SubElement(case, 'failure', message=error).text = tail
            failures += 1
            break
    cases = list(report)
    report.set('tests', str(len(cases)))
    report.set('failures', str(sum(c.find('failure') is not None for c in cases)))
    report.set('errors', str(sum(c.find('error') is not None for c in cases)))
    report.set('skipped', str(sum(c.find('skipped') is not None for c in cases)))
    ET.ElementTree(report).write(os.environ['XML_OUTPUT_FILE'], encoding='utf-8', xml_declaration=True)
    return int(bool(failures))


if __name__ == '__main__':
    with launch_process_guard(target=os.environ.get('TEST_TARGET')):
        sys.exit(main())
