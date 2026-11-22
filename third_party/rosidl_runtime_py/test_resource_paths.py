# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Resource indexes may place the owning package at the start of the path."""
import importlib
import unittest
from unittest.mock import patch

interfaces = importlib.import_module('rosidl_runtime_py.get_interfaces')


class ResourcePathsTest(unittest.TestCase):
    def test_package_path_positions(self):
        for prefix in ('', 'fixture_interfaces/', 'generated/fixture_interfaces/'):
            with self.subTest(prefix=prefix):
                content = '\n'.join([
                    'msg/Time.msg', 'msg/builtin_interfaces/msg/Time.idl',
                    prefix + 'msg/Example.idl', prefix + 'srv/Query.idl', prefix + 'action/Move.idl',
                ])
                with patch.object(interfaces, 'has_resource', return_value=True), \
                        patch.object(interfaces, 'get_resource', return_value=(content, '/unused')):
                    actual = interfaces.get_interfaces(['fixture_interfaces'])['fixture_interfaces']
                    expected = {'msg/Example', 'srv/Query', 'action/Move'}
                    # Without package-qualified entries, the SDK cannot identify foreign sources.
                    if not prefix:
                        expected.update(['msg/Time'])
                    self.assertEqual(set(actual), expected)


if __name__ == '__main__':
    unittest.main()
