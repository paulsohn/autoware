# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Run the original launch suite with the declared heap hook and backend."""

import os
from pathlib import Path

os.environ["ENABLE_AGNOCAST"] = os.environ["AUTOWARE_TEST_AGNOCAST"]
__file__ = os.environ["AUTOWARE_LAUNCH_SOURCE"]
exec(compile(Path(__file__).read_text(), __file__, "exec"))

_original_description = generate_test_description


@pytest.mark.launch_test
def generate_test_description():
    description = _original_description()
    description.entities.insert(
        0,
        launch.actions.SetLaunchConfiguration(
            "agnocast_heaphook_path", os.environ["AGNOCAST_TEST_HEAPHOOK"]
        ),
    )
    return description
