"""Migration aid: write initial MODULE.bazel and BUILD.bazel files for ament_cmake_auto C++ packages.

Usage: bazel run //bazel/tools/gen_build:gen_cc -- PKG_DIR [PKG_DIR ...]

Handles the common Autoware shape: `ament_auto_add_library(${PROJECT_NAME} SHARED <srcs>)` (or a
header-only package) plus gtests from test/**/*.cpp. Follows the RCR C++ package pattern
(`<pkg>_library` cc_library + `<pkg>` cc_shared_library, see docs/bazel/PLAN.md). Packages that
already have a MODULE.bazel are skipped. Anything it cannot map is printed; fix those by hand.
Run gen_interfaces afterwards to refresh bazel/autoware_packages.*. Delete once migration is done.
"""

import os
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

from bazel.tools.gen_build.gen_interfaces import find_packages, interface_files, fmt_list

GOOGLETEST_VERSION = "1.18.0.bcr.1"  # what the ros release module resolves (MODULE.bazel.lock)
RULES_CC_VERSION = "0.2.26.rcr.0"  # same as the root MODULE.bazel
TOOLING = re.compile(r"^(ament_.*|autoware_cmake|autoware_lint_common|ros_environment|rosidl_default_.*)$")
# package.xml system keys whose targets are found from #includes instead (see INCLUDE_TARGETS).
SYSTEM_KEYS = re.compile(r"^(lib.*-dev|libboost.*|eigen|fmt|boost)$")
INCLUDE_TARGETS = [  # (regex on the include path, target in @rosdistro//cc)
    (re.compile(r"^boost/([a-z_0-9]+)[/.]"), lambda m: "boost." + BOOST_HEADER_COMPONENT.get(m.group(1), m.group(1))),
    (re.compile(r"^Eigen/"), lambda m: "eigen"),
    (re.compile(r"^fmt/"), lambda m: "fmt"),
]
INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', re.M)
INTERFACE_SUFFIXES = ("_msgs", "_srvs", "_interfaces")
# RCR packages whose name misleads the suffix heuristic: True = provides interface bindings (:cc).
EXTERNAL_IS_INTERFACE = {"tf2_geometry_msgs": False, "tf2_sensor_msgs": False, "logging_demo": True}
BOOST_HEADER_COMPONENT = {"version": "config"}  # boost/<x>.hpp living in another component


def deps_of(pkg_xml, tags):
    root = ET.parse(pkg_xml).getroot()
    return sorted({e.text.strip() for t in tags for e in root.findall(t)})


def third_party(files):
    """@rosdistro//cc targets for third-party headers included by files."""
    targets = set()
    for f in files:
        for inc in INCLUDE_RE.findall(f.read_text(errors="replace")):
            for rx, target in INCLUDE_TARGETS:
                m = rx.match(inc)
                if m:
                    targets.add("@rosdistro//cc:" + target(m))
    return targets


def lib_sources(cmake, pkg_dir):
    """Sources of ament_auto_add_library(${PROJECT_NAME} ...), [] if header-only, None if unsupported."""
    m = re.search(r"ament_auto_add_library\(\s*\$\{PROJECT_NAME\}\s+(?:SHARED\s+)?([^)]*)\)", cmake)
    if not m:
        return [] if "ament_auto_add_library" not in cmake and "add_library" not in cmake else None
    srcs = re.findall(r'"?([\w./${}-]+\.(?:cpp|cc|c))"?', m.group(1))
    if any("$" in s or not (pkg_dir / s).exists() for s in srcs):
        return None
    return srcs


def known_ros_packages(ws, in_repo):
    """In-repo packages plus every module the lockfile knows (RCR and BCR)."""
    lock = (ws / "MODULE.bazel.lock").read_text()
    return set(in_repo) | set(re.findall(r"/modules/([A-Za-z0-9_.-]+)/[^/]+/MODULE\.bazel", lock))


def undeclared_includes(files, declared, known, self_name):
    """ROS packages whose headers are included but not declared in package.xml."""
    used = {inc.split("/")[0] for f in files for inc in INCLUDE_RE.findall(f.read_text(errors="replace"))}
    third_party = {"boost", "pcl", "Eigen", "fmt"}  # resolved from #includes, not package.xml
    return sorted(p for p in (used & known) - third_party if p not in declared and p != self_name)


def generate(ws, pkg_dirs):
    in_repo = find_packages(ws / "src")
    known = known_ros_packages(ws, in_repo)
    interface_pkgs = {n for n, d in in_repo.items() if interface_files(d)}
    for pkg_dir in pkg_dirs:
        pkg_dir = ws / pkg_dir
        name = ET.parse(pkg_dir / "package.xml").getroot().findtext("name").strip()
        if (pkg_dir / "MODULE.bazel").exists():
            print(f"{name}: already migrated, skipped")
            continue
        srcs = lib_sources((pkg_dir / "CMakeLists.txt").read_text(), pkg_dir)
        if srcs is None:
            print(f"{name}: SKIPPED, CMakeLists.txt is not the simple ament_auto shape; migrate by hand")
            continue

        def pairs(keys):
            """{shared: static} for ROS package dependencies; unmapped system keys are returned separately."""
            out, unmapped = {}, []
            for k in keys:
                if TOOLING.match(k) or k == name:
                    continue
                if SYSTEM_KEYS.match(k):
                    continue  # resolved from #includes
                if k in interface_pkgs or (
                    k not in in_repo and EXTERNAL_IS_INTERFACE.get(k, k.endswith(INTERFACE_SUFFIXES))
                ):
                    out[f"@{k}//:cc_shared"] = f"@{k}//:cc"
                elif k in in_repo or not k.startswith("lib"):
                    out[f"@{k}//:{k}"] = f"@{k}//:{k}_library"
                else:
                    unmapped.append(k)
            return out, unmapped

        xml = pkg_dir / "package.xml"
        lib_deps, un1 = pairs(deps_of(xml, ["depend", "build_depend", "build_export_depend"]))
        test_only, un2 = pairs(deps_of(xml, ["test_depend"]))
        test_only = {k: v for k, v in test_only.items() if k not in lib_deps}
        hdrs = sorted(p for p in (pkg_dir / "include").rglob("*") if p.suffix in (".hpp", ".h"))
        tests = sorted((pkg_dir / "test").rglob("*.cpp")) if (pkg_dir / "test").exists() else []
        lib_third = third_party(hdrs + [pkg_dir / s for s in srcs])
        test_third = third_party(tests) - lib_third
        own_main = any(re.search(r"\bint\s+main\s*\(", t.read_text(errors="replace")) for t in tests)
        gtest = "@googletest//:gtest" if own_main else "@rosdistro//cc:googletest"

        modules = {lbl.split("//")[0][1:] for lbl in list(lib_deps) + list(test_only)} | {"rosdistro"}
        module = f'module(name = "{name}")\n\n'
        for dep in sorted(modules):
            version = "" if dep in in_repo else ', version = "0.0.0"'
            module += f'bazel_dep(name = "{dep}"{version})\n'
        if tests and own_main:
            # Not dev_dependency: Bazel ignores dev dependencies of non-root modules.
            module += f'bazel_dep(name = "googletest", version = "{GOOGLETEST_VERSION}")\n'
        module += f'bazel_dep(name = "rules_cc", version = "{RULES_CC_VERSION}")\n'
        (pkg_dir / "MODULE.bazel").write_text(module)

        def dict_lit(d):
            if not d:
                return "{}"
            return "{\n" + "".join(f'    "{k}": "{v}",\n' for k, v in sorted(d.items())) + "}"

        b = 'load("@rosdistro//cc:defs.bzl", "collect_transitive_dynamic_deps", "ros_local_defines")\n'
        b += 'load("@rules_cc//cc:defs.bzl", "cc_library", "cc_shared_library"'
        b += ', "cc_test")\n\n' if tests else ")\n\n"
        b += 'package(default_visibility = ["//visibility:public"])\n\n'
        b += f"DEPENDENCIES = {dict_lit(lib_deps)}\n\n"
        b += "cc_library(\n"
        b += f'    name = "{name}_library",\n'
        if srcs:
            b += f"    srcs = {fmt_list(srcs, 4)},\n"
        # Bazel 9 rejects glob patterns that match nothing: emit only patterns with matches.
        hdr_globs = [f"include/**/*{ext}" for ext in (".hpp", ".h") if any(h.suffix == ext for h in hdrs)]
        if hdr_globs:
            b += f"    hdrs = glob({fmt_list(hdr_globs, 4)}),\n"
        b += '    includes = ["include"],\n    local_defines = ros_local_defines(),\n'
        extra = f" + {fmt_list(sorted(lib_third), 4)}" if lib_third else ""
        b += f"    deps = DEPENDENCIES.values(){extra},\n)\n\n"
        b += "cc_shared_library(\n"
        b += f'    name = "{name}",\n    dynamic_deps = DEPENDENCIES.keys(),\n'
        b += f'    exports_filter = [":{name}_library"],\n    deps = [":{name}_library"],\n)\n'
        if tests:
            b += f"\nTEST_DEPENDENCIES = {dict_lit(test_only)}\n\n"
            b += "collect_transitive_dynamic_deps(\n    name = \"test_dynamic_deps\",\n"
            b += f'    dynamic_deps = [":{name}"] + TEST_DEPENDENCIES.keys(),\n'
            b += '    visibility = ["//visibility:private"],\n)\n\n'
            rel = [t.relative_to(pkg_dir).as_posix() for t in tests]
            b += "cc_test(\n"
            test_dir = pkg_dir / "test"
            test_hdrs = sorted(p.relative_to(pkg_dir).as_posix() for p in test_dir.rglob("*.hpp"))
            test_data = sorted(p.relative_to(pkg_dir).as_posix() for p in test_dir.rglob("*")
                               if p.is_file() and p.suffix not in (".cpp", ".hpp"))
            b += f'    name = "test_{name}",\n    srcs = {fmt_list(rel + test_hdrs, 4)},\n'
            b += f'    data = {fmt_list(["@@//bazel/config:cyclonedds.xml"] + test_data, 4)},\n'
            b += '    dynamic_deps = [":test_dynamic_deps"],\n'
            b += f'    deps = [":{name}_library"] + DEPENDENCIES.values() + TEST_DEPENDENCIES.values() + {fmt_list(sorted(test_third | {gtest}), 4)},\n)\n'
        (pkg_dir / "BUILD.bazel").write_text(b)
        declared = set(deps_of(xml, ["depend", "build_depend", "build_export_depend", "test_depend"]))
        missing = undeclared_includes(hdrs + [pkg_dir / s for s in srcs] + tests, declared, known, name)
        if missing:
            print(f"{name}: WARNING includes undeclared packages {missing}; add them to package.xml")
        notes = sorted(set(un1 + un2))
        print(f"{name}: {len(srcs)} srcs, {len(tests)} tests, deps {sorted(modules)}" + (f"; UNMAPPED {notes}" if notes else ""))


if __name__ == "__main__":
    ws = Path(os.environ.get("BUILD_WORKSPACE_DIRECTORY", "."))
    generate(ws, sys.argv[1:])
