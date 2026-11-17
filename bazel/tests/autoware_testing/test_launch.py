# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Run the original generic smoke suite against the real example executable."""
from autoware_testing import smoke_test
from autoware_testing.smoke_test import DummyTest, TestProcessOutput
import launch
from launch.actions import SetLaunchConfiguration
import launch_testing.actions
import pytest


@pytest.mark.launch_test
def generate_test_description():
    return launch.LaunchDescription([
        SetLaunchConfiguration('arg_package', 'autoware_test_node'),
        SetLaunchConfiguration('arg_package_exe', 'autoware_test_node_node'),
        SetLaunchConfiguration('arg_param_filenames', ''),
        SetLaunchConfiguration('arg_test_param_filenames', ''),
        *smoke_test.generate_test_description().entities,
    ])
