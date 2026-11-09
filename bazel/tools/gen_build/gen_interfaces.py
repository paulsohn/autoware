"""Migration aid: write initial MODULE.bazel and BUILD.bazel files for ROS interface packages.

Usage: bazel run //bazel/tools/gen_build:gen_interfaces -- [ROOT ...]   (default ROOT: src/core)

The written files are ordinary sources from then on, maintained by hand like CMakeLists.txt.
Packages that already have a MODULE.bazel are skipped, so hand edits are never overwritten.
Delete this tool once every package is migrated.

For every package under ROOT that has .msg/.srv/.action files and no MODULE.bazel, writes
(conventions: docs/bazel/PLAN.md):
- <pkg>/MODULE.bazel: module(name = <pkg>) and a bazel_dep per referenced interface package.
- <pkg>/BUILD.bazel: default_generators() over all of the package's interfaces.
- <dir>/BUILD.bazel for each directory holding interface files (msg/, srv/, action/, or
  <group>/msg/ as in autoware_adapi_v1_msgs): one ros_message/ros_service/ros_action per file.
Then rewrites bazel/autoware_packages.MODULE.bazel (bazel_dep + local_path_override for every
package under src/ that has a MODULE.bazel), which the root MODULE.bazel includes, and
bazel/autoware_packages.txt (target patterns for --target_pattern_file).
"""

import os
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

KINDS = {".msg": ("msg", "ros_message"), ".srv": ("srv", "ros_service"), ".action": ("action", "ros_action")}
PRIMITIVES = {
    "bool", "byte", "char", "float32", "float64", "int8", "uint8", "int16", "uint16",
    "int32", "uint32", "int64", "uint64", "string", "wstring",
}
SKIP_DIRS = {".git", "test", "tests", "build", "install", "log"}


def field_types(text):
    """ROS type names referenced by a .msg/.srv/.action file, as (package or None, Name)."""
    types = set()
    for line in text.splitlines():
        line = line.split("#", 1)[0].strip()
        if not line or line == "---":
            continue
        t = line.split()[0]
        t = re.sub(r"\[[^\]]*\]$", "", t)  # arrays: T[], T[3], T[<=3]
        t = re.sub(r"<=\d+$", "", t)  # bounded strings: string<=10
        if t in PRIMITIVES:
            continue
        if t == "Header":
            types.add(("std_msgs", "Header"))
        elif "/" in t:
            pkg, name = t.split("/")[0], t.split("/")[-1]
            types.add((pkg, name))
        else:
            types.add((None, t))
    return types


def package_name(pkg_dir):
    return ET.parse(pkg_dir / "package.xml").getroot().findtext("name").strip()


def find_packages(root):
    """{ros package name: dir} for every package.xml below root."""
    found = {}
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in dirnames if d not in SKIP_DIRS]
        if "package.xml" in filenames:
            found[package_name(Path(dirpath))] = Path(dirpath)
    return found


def interface_files(pkg_dir):
    """Interface files of a package: [(bazel subdir relative to pkg, kind, rule, Name, Path)]."""
    out = []
    for dirpath, dirnames, filenames in os.walk(pkg_dir):
        dirnames[:] = [d for d in dirnames if d not in SKIP_DIRS]
        d = Path(dirpath)
        for f in sorted(filenames):
            ext = Path(f).suffix
            if ext in KINDS and d.name == KINDS[ext][0]:
                kind, rule = KINDS[ext]
                out.append((d.relative_to(pkg_dir).as_posix(), kind, rule, Path(f).stem, d / f))
    return sorted(out)


def fmt_list(items, indent):
    if not items:
        return "[]"
    pad = " " * indent
    return "[\n" + "".join(f'{pad}    "{i}",\n' for i in items) + pad + "]"


def generate(ws, roots):
    in_repo = find_packages(ws / "src")
    targets = {}  # pkg -> {"dir": Path, "files": [...]}
    for root in roots:
        for name, d in find_packages(ws / root).items():
            files = interface_files(d)
            if files and not (d / "MODULE.bazel").exists():
                targets[name] = {"dir": d, "files": files}

    # Label of every in-repo message (already migrated or not), for cross-package lookups
    # (subdirs vary, e.g. adapi groups).
    index = {}
    migrated = {name for name, d in in_repo.items() if (d / "MODULE.bazel").exists()}
    for name, d in in_repo.items():
        for sub, kind, _, iname, _ in interface_files(d):
            if kind == "msg":
                index[(name, iname)] = sub

    for name, info in sorted(targets.items()):
        pkg_dir = info["dir"]
        by_dir = {}
        pkg_deps = set()
        for sub, kind, rule, iname, path in info["files"]:
            deps = set()
            for tpkg, tname in field_types(path.read_text()):
                tpkg = tpkg or name
                if tpkg == name:
                    tsub = index.get((name, tname))
                    if tsub is None:
                        sys.exit(f"{path}: unknown type {tname} in package {name}")
                    deps.add(f":{tname}" if tsub == sub else f"//{tsub}:{tname}")
                elif tpkg in in_repo:
                    if tpkg not in targets and tpkg not in migrated:
                        sys.exit(f"{path}: in-repo package {tpkg} is neither migrated nor being generated; add its root")
                    deps.add(f"@{tpkg}//{index[(tpkg, tname)]}:{tname}")
                    pkg_deps.add(tpkg)
                else:
                    deps.add(f"@{tpkg}//msg:{tname}")
                    pkg_deps.add(tpkg)
            by_dir.setdefault(sub, []).append((rule, iname, path.name, sorted(deps)))

        module = f'module(name = "{name}")\n\n'
        bazel_deps = sorted(pkg_deps | {"rosidl_default_generators"})
        for dep in bazel_deps:
            # "0.0.0" for in-repo deps too, so the root can switch any of them to the RCR.
            module += f'bazel_dep(name = "{dep}", version = "0.0.0")\n'
        (pkg_dir / "MODULE.bazel").write_text(module)

        all_labels = [f"//{sub}:{iname}" for sub, items in sorted(by_dir.items()) for _, iname, _, _ in items]
        build = (
            'load("@rosidl_default_generators//:defs.bzl", "default_generators")\n\n'
            + 'package(default_visibility = ["//visibility:public"])\n\n'
            + "default_generators(\n"
            + f"    dependencies = {fmt_list(sorted('@' + d for d in pkg_deps), 4)},\n"
            + '    package_xml = "package.xml",\n'
            + f"    deps = {fmt_list(all_labels, 4)},\n"
            + ")\n"
        )
        (pkg_dir / "BUILD.bazel").write_text(build)

        for sub, items in by_dir.items():
            rules = sorted({rule for rule, _, _, _ in items})
            loads = ", ".join(f'"{r}"' for r in rules)
            body = f'load("@rosidl_default_generators//:defs.bzl", {loads})\n\n'
            body += 'package(default_visibility = ["//visibility:public"])\n'
            for rule, iname, fname, deps in items:
                body += f'\n{rule}(\n    name = "{iname}",\n    src = "{fname}",\n'
                if deps:
                    body += f"    deps = {fmt_list(deps, 4)},\n"
                body += ")\n"
            (pkg_dir / sub / "BUILD.bazel").write_text(body)
        print(f"{name}: {len(info['files'])} interfaces, deps {sorted(pkg_deps)}")

    # Root include: every package module under src/ (migrated now or earlier).
    entries = []
    for name, d in sorted(in_repo.items()):
        m = d / "MODULE.bazel"
        if m.exists():
            rel = d.relative_to(ws).as_posix()
            entries.append(
                f'bazel_dep(name = "{name}")\nlocal_path_override(module_name = "{name}", path = "{rel}")\n'
            )
    (ws / "bazel" / "autoware_packages.MODULE.bazel").write_text(
        "# Every ROS package module under src/, included by the root MODULE.bazel.\n\n" + "\n".join(entries)
    )
    # //... does not reach package modules: bazel build --target_pattern_file=bazel/autoware_packages.txt
    names = [e.split('"')[1] for e in entries]
    (ws / "bazel" / "autoware_packages.txt").write_text("".join(f"@{n}//...\n" for n in names))


if __name__ == "__main__":
    ws = Path(os.environ.get("BUILD_WORKSPACE_DIRECTORY", "."))
    generate(ws, sys.argv[1:] or ["src/core"])
