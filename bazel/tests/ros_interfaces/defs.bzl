"""Product-owned wire checks for registered interface providers."""

load("@rules_python//python:defs.bzl", "py_test")

def interface_serialization_test(package, name = "generated_serialization_test"):
    py_test(
        name = name,
        srcs = ["//bazel/tests/ros_interfaces:serialization_test.py"],
        main = "//bazel/tests/ros_interfaces:serialization_test.py",
        args = [package],
        env = {"LD_LIBRARY_PATH": "lib"},
        precompile = "disabled",
        deps = [
            "@rclpy",
            "@" + package + "//:py",
        ],
    )
