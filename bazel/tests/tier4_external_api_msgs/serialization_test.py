# Copyright 2026 Taeseung Sohn
# SPDX-License-Identifier: Apache-2.0
import unittest

from rclpy.serialization import deserialize_message, serialize_message
from tier4_external_api_msgs.srv import SetPreferredLane


class TestPreferredLane(unittest.TestCase):
    def test_request_response_event(self):
        self.assertEqual((SetPreferredLane.Request.LEFT, SetPreferredLane.Request.RIGHT, SetPreferredLane.Request.AUTO), (0, 1, 2))
        request = SetPreferredLane.Request(lane_change_direction=SetPreferredLane.Request.RIGHT)
        response = SetPreferredLane.Response()
        response.status.success, response.status.code, response.status.message = True, 7, 'accepted'
        event = SetPreferredLane.Event(request=[request], response=[response])
        for value in (request, response, event):
            self.assertEqual(deserialize_message(serialize_message(value), type(value)), value)


if __name__ == '__main__':
    unittest.main()
