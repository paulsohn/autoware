import tempfile
import unittest
from pathlib import Path

import ast

from bazel.tools.cmake_parity.check_cmake_parity import CMake, commands, literal


class CMakeReaderTest(unittest.TestCase):
    def test_foreach_glob_and_names(self):
        with tempfile.TemporaryDirectory() as tmp:
            d = Path(tmp)
            (d / "src").mkdir()
            (d / "test").mkdir()
            for f in ("src/a.cpp", "src/b.cpp", "test/x.cpp", "test/y.cpp"):
                (d / f).write_text("")
            text = """
project(pkg)
ament_auto_add_library(lib_${PROJECT_NAME} SHARED src/a.cpp "src/b.cpp")  # comment
target_compile_options(lib_pkg PRIVATE -Wno-error=foo)
target_compile_definitions(t PRIVATE FILE="${CMAKE_CURRENT_SOURCE_DIR}/x.json")
if(BUILD_TESTING)
  file(GLOB_RECURSE tests test/*.cpp)
  foreach(t IN LISTS tests)
    get_filename_component(n ${t} NAME_WE)
    ament_add_ros_isolated_gtest(${n}_test ${t} TIMEOUT 10)
  endforeach()
endif()
if(SOMETHING)
endif()
if(BUILD_TESTING AND NOT "$ENV{X}" STREQUAL "1")
endif()
autoware_ament_auto_package(USE_SCOPED_HEADER_INSTALL_DIR INSTALL_TO_SHARE config launch)
generate_parameter_library(my_params param/p.yaml)
set_tests_properties(x_test PROPERTIES ENVIRONMENT "A=1" TIMEOUT 120 LABELS "l")
weird_command(x)
"""
            cm = CMake(d)
            cm.run(commands(text))
            self.assertEqual(cm.libraries, {"lib_pkg": "SHARED"})
            self.assertEqual(set(cm.tests), {"x_test", "y_test"})
            self.assertEqual(cm.timeouts, {"x_test": "120", "y_test": "10"})
            self.assertEqual(cm.sources, {"src/a.cpp", "src/b.cpp", "test/x.cpp", "test/y.cpp"})
            self.assertEqual(cm.copts, {"-Wno-error=foo"})
            self.assertEqual(cm.defines, {"FILE"})
            self.assertEqual(cm.share, {"config", "launch"})
            self.assertEqual(cm.ifs, {"SOMETHING", "NOT $ENV{X} STREQUAL 1"})
            self.assertEqual(cm.unhandled, {"weird_command"})
            self.assertEqual(cm.param_libs, {"my_params"})

    def test_function_regex_and_launch_test(self):
        with tempfile.TemporaryDirectory() as tmp:
            d = Path(tmp)
            (d / "test" / "utils").mkdir(parents=True)
            text = """
project(pkg)
function(add_testcase filepath)
  get_filename_component(filename ${filepath} NAME)
  string(REGEX REPLACE ".cpp" "" test_name ${filename})
  ament_add_gtest(${test_name} ${filepath})
endfunction()
add_testcase(test/utils/test_a.cpp)
add_launch_test(test/utils/test_launch.py TIMEOUT "30")
add_launch_test(test/other.py TARGET named)
pluginlib_export_plugin_description_file(base_pkg plugins.xml)
add_library(hdr INTERFACE)
cuda_add_executable(cu_test test/k.cu)
ament_add_gtest_test(cu_test)
"""
            cm = CMake(d)
            cm.run(commands(text))
            self.assertEqual(cm.tests, {"test_a": "cc", "test_utils_test_launch.py": "py", "named": "py", "cu_test": "cc"})
            self.assertEqual(cm.libraries, {"hdr": "STATIC"})
            self.assertEqual(cm.executables, set())
            self.assertEqual(cm.timeouts, {"test_utils_test_launch.py": "30"})
            self.assertEqual(cm.sources, {"test/utils/test_a.cpp", "test/k.cu"})
            self.assertEqual(cm.unhandled, set())
            self.assertEqual(cm.plugin_exports, {("base_pkg", "plugins.xml")})


class LiteralTest(unittest.TestCase):
    def test_select_and_concatenation(self):
        node = ast.parse('["-a"] + select({"//x": ["-b"], "//conditions:default": []}) + ["-c"]').body[0].value
        self.assertEqual(literal(node), ["-a", "-b", "-c"])


if __name__ == "__main__":
    unittest.main()
