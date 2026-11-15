# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Version lookup respects runtime preload and local ament overlays."""

from unittest.mock import Mock

import pytest
from ros2agnocast.verb import agnocast_version as version


@pytest.mark.parametrize('preloaded', [False, True])
def test_heaphook_version_uses_the_selected_prefix(tmp_path, monkeypatch, preloaded):
    library_path = tmp_path / 'lib' / 'libagnocast_heaphook.so'
    library_path.parent.mkdir()
    library_path.touch()
    monkeypatch.setenv('ROS_DISTRO', '__test_distribution__')
    monkeypatch.setenv('LD_PRELOAD', str(library_path) if preloaded else '')
    prefix = Mock(return_value=str(tmp_path))
    monkeypatch.setattr(version, 'get_package_prefix', prefix)
    library = Mock()
    library.agnocast_heaphook_get_version.return_value = b'2.4.1'
    loader = Mock(return_value=library)
    monkeypatch.setattr(version.ctypes, 'CDLL', loader)

    result = version.VersionVerb()._get_heaphook_version()

    assert result.startswith('2.4.1')
    assert ('WARN: not in LD_PRELOAD' in result) is not preloaded
    loader.assert_called_once_with(
        str(library_path), mode=version.os.RTLD_LAZY | version.ctypes.RTLD_LOCAL)
    if preloaded:
        prefix.assert_not_called()
    else:
        prefix.assert_called_once_with('agnocastlib')
