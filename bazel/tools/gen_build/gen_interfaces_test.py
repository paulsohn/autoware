import unittest

from bazel.tools.gen_build.gen_interfaces import field_types


class FieldTypesTest(unittest.TestCase):
    def test_parses_all_type_forms(self):
        text = """
# comment line
std_msgs/Header header
uint8 MODE_A = 1  # constant
string<=10 short_name
geometry_msgs/Pose[] poses
geometry_msgs/Point[3] corners
Local[<=2] locals
Header legacy
---
autoware_common_msgs/ResponseStatus status
bool success
"""
        self.assertEqual(
            field_types(text),
            {
                ("std_msgs", "Header"),
                ("geometry_msgs", "Pose"),
                ("geometry_msgs", "Point"),
                (None, "Local"),
                ("autoware_common_msgs", "ResponseStatus"),
            },
        )


if __name__ == "__main__":
    unittest.main()
