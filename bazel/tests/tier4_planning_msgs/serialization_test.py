# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
import unittest

from rclpy.serialization import deserialize_message, serialize_message
from tier4_planning_msgs.msg import RerouteAvailability


class TestAvailability(unittest.TestCase):
    def test_wire_round_trip(self):
        value = RerouteAvailability(availability=True)
        value.stamp.sec, value.stamp.nanosec = 123, 456
        self.assertEqual(deserialize_message(serialize_message(value), RerouteAvailability), value)


if __name__ == '__main__':
    unittest.main()
