# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Prepare or run the selected traffic-light FP16 product with writable caches."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

from ament_index_python.packages import get_package_prefix, get_package_share_directory
from python.runfiles import runfiles


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cache-dir', type=Path, required=True,
                        help='Writable cache root dedicated to this GPU and SDK')
    parser.add_argument('--prepare-only', action='store_true')
    options = parser.parse_args()
    package = 'autoware_traffic_light_pipeline'
    share = Path(get_package_share_directory(package))
    configuration = share / 'config/traffic_light_recognition.param.yaml'
    resource = runfiles.Create().Rlocation('_main/bazel/runtime/traffic_light_fp16.json')
    if not resource:
        raise RuntimeError('Declared product profile is missing')
    profile = json.loads(Path(resource).read_text())
    providers = {
        'tensorrt_yolox': ('autoware_tensorrt_yolox_models',
                         ['yolox_s_car_ped_tl_detector_960_960_batch_1.onnx', 'car_ped_tl_detector_labels.txt']),
        'traffic_light_classifier': ('autoware_traffic_light_classifier_models',
                                    ['traffic_light_classifier_mobilenetv2_batch_6.onnx',
                                     'ped_traffic_light_classifier_mobilenetv2_batch_6.onnx',
                                     'lamp_labels.txt', 'lamp_labels_ped.txt']),
    }
    inputs = {}
    for subdir, (provider, names) in providers.items():
        source = Path(get_package_share_directory(provider))
        for name in names:
            inputs[subdir + '/' + name] = source / name
    remap = Path(get_package_share_directory('autoware_tensorrt_yolox')) / 'config/traffic_light_roi_label_remap.csv'
    manifest = {'profile': profile, 'configuration_sha256': digest(configuration),
                'roi_remap_sha256': digest(remap),
                'inputs': {name: digest(path) for name, path in inputs.items()}}
    identity = hashlib.sha256(json.dumps(manifest, sort_keys=True).encode()).hexdigest()
    cache = options.cache_dir.expanduser().resolve() / profile['profile'] / identity
    cache.mkdir(parents=True, exist_ok=True)
    for name, source in inputs.items():
        destination = cache / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        if destination.is_file() and digest(destination) == manifest['inputs'][name]:
            continue
        with tempfile.NamedTemporaryFile(dir=destination.parent, delete=False) as temporary:
            staged = Path(temporary.name)
        try:
            shutil.copyfile(source, staged)
            os.replace(staged, destination)
        finally:
            staged.unlink(missing_ok=True)
    (cache / 'profile.json').write_text(json.dumps(manifest, indent=2) + '\n')
    prefix = Path(get_package_prefix(package)).resolve()
    program = prefix / 'lib' / package / 'traffic_light_recognition_node'
    arguments = [str(program), '--ros-args', '--params-file', str(configuration.resolve()),
                 '-p', 'ml_model_path:=' + str(cache), '-p', 'whole_image_detector.roi_remap_path:=' + str(remap.resolve()),
                 '-p', 'build_only:=' + str(options.prepare_only).lower()]
    for backend in ['whole_image_detector', 'car_classifier', 'pedestrian_classifier']:
        arguments += ['-p', backend + '.precision:=' + profile['precision']]
    environment = dict(os.environ, AMENT_PREFIX_PATH=str(prefix),
                       LD_LIBRARY_PATH=str(prefix / 'lib') + ':' + os.environ.get('LD_LIBRARY_PATH', ''))
    if not options.prepare_only:
        os.execve(program, arguments, environment)
    prepared = subprocess.run(arguments, env=environment, stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT, text=True, check=True)
    sys.stderr.write(prepared.stdout)
    # The component logs caught preparation exceptions but still exits zero.
    if 'failed to build TensorRT engines:' in prepared.stdout:
        raise RuntimeError('Component reported an engine preparation failure')
    engines = sorted(cache.rglob('*.engine'))
    if len(engines) != 3 or any(path.stat().st_size == 0 for path in engines):
        raise RuntimeError('Preparation must produce three nonempty TensorRT engines')
    print(json.dumps({'model_root': str(cache), 'engines': [str(p) for p in engines], 'profile': profile}))


if __name__ == '__main__':
    main()
