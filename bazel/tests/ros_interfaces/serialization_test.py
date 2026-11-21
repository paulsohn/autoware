# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
"""Round-trip the generated public types of an explicitly selected package."""
import importlib
import inspect
import pkgutil
import sys
import unittest

from rclpy.serialization import deserialize_message, serialize_message

PACKAGE = sys.argv.pop(1)


class TestSerialization(unittest.TestCase):
    def test_generated_types(self):
        checked = 0
        for kind in ('msg', 'srv'):
            try:
                module = importlib.import_module(f'{PACKAGE}.{kind}')
            except ModuleNotFoundError as error:
                if error.name == f'{PACKAGE}.{kind}':
                    continue
                raise
            # RCR namespace packages omit their lazy exports from dir(module).
            for item in pkgutil.iter_modules(module.__path__, module.__name__ + '.'):
                if not item.name.rsplit('.', 1)[-1].startswith('_'):
                    continue
                generated = importlib.import_module(item.name)
                for name, value in inspect.getmembers(generated, inspect.isclass):
                    if value.__module__ != generated.__name__:
                        continue
                    if kind == 'msg' and hasattr(value, 'get_fields_and_field_types'):
                        types = [value]
                    elif kind == 'srv' and inspect.isclass(getattr(value, 'Request', None)):
                        types = [value.Request, value.Response, value.Event]
                    else:
                        continue
                    for message_type in types:
                        with self.subTest(type=f'{name}.{message_type.__name__}'):
                            encoded = serialize_message(message_type())
                            decoded = deserialize_message(encoded, message_type)
                            self.assertEqual(serialize_message(decoded), encoded)
                            checked += 1
        self.assertGreater(checked, 0)
        print(f'{PACKAGE}: {checked} generated wire types passed')


if __name__ == '__main__':
    unittest.main()
