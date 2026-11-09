"""Check that a package's Bazel build reflects its CMakeLists.txt (migration aid; writes nothing).

Usage (from the repo root; it runs `bazel query`, so not via `bazel run`):
    python3 -m bazel.tools.cmake_parity.check_cmake_parity src/core/.../<pkg> [...]

Each finding has a key. A deliberate difference is recorded in the package's BUILD.bazel or MODULE.bazel as
    # cmake-diff <key>: <reason>
and then reported as recorded instead of open. Recorded keys that no longer match a finding are reported as stale.
Exit status 1 if anything is open or stale. Interface packages (rosidl_generate_interfaces) are skipped.

Checked: libraries, executables, components, tests (names, sources, TIMEOUT), every source file CMake compiles and
every .c/.cpp Bazel compiles, compile options/definitions/features, INSTALL_TO_SHARE and install() targets, Python
packages, environment hooks, if() conditions, package.xml build/test dependencies vs MODULE.bazel, and any CMake
command it does not know. Not reported, because handled the same way for every package (PLAN.md, "Package
conventions"): autoware_package() flags, ament_add_ros_isolated_gtest isolation, include directories, link lines,
ament_export_*, USE_SCOPED_HEADER_INSTALL_DIR, CONFIG_EXTRAS, and if() checks of the compiler or C++ standard.
"""

import ast
import fnmatch
import re
import subprocess
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

SOURCE_EXT = (".c", ".cc", ".cpp", ".cu", ".h", ".hpp")
COMPILED_EXT = (".c", ".cc", ".cpp", ".cu")
AUTOWARE_FLAGS = {"-Wall", "-Wextra", "-Wpedantic", "-Werror"}
TRIVIAL_IF = re.compile(r"BUILD_TESTING|CMAKE_CXX_STANDARD|CMAKE_CXX_COMPILER|CMAKE_COMPILER_IS_GNUCXX|MSVC|WIN32")
IGNORED = {
    "add_library",  # only reached for IMPORTED targets (compatibility shims)
    "cmake_minimum_required", "find_package", "autoware_package", "ament_auto_find_build_dependencies",
    "ament_auto_find_test_dependencies", "ament_lint_auto_find_test_dependencies", "include_directories",
    "target_include_directories", "target_link_libraries", "ament_target_dependencies", "ament_package",
    "message", "get_target_property", "else", "elseif", "endif", "mark_as_advanced", "option", "string",
    "find_path", "find_library", "pkg_check_modules", "set_property", "add_dependencies",
}
TOOLING_DEP = re.compile(r"^(ament_.*|autoware_cmake|autoware_lint_common|ros_environment|rosidl_default_.*)$")
SYSTEM_DEP = re.compile(r"^(lib.*|python3-.*|.*-dev|eigen|boost|fmt|range-v3|geographiclib|yaml-cpp|.*_cmake_module)$")


# ---------------------------------------------------------------------------------------------- CMake reading
def commands(text):
    """[(name, [args])] for a CMake file; args keep generator expressions and quoted strings intact."""
    text = re.sub(r"#[^\n]*", "", text)
    out, i = [], 0
    for m in re.finditer(r"\b([A-Za-z_]\w*)\s*\(", text):
        if m.start() < i:
            continue
        depth, j = 1, m.end()
        while depth and j < len(text):
            depth += {"(": 1, ")": -1}.get(text[j], 0)
            j += 1
        body, i = text[m.end():j - 1], j
        args = re.findall(r'(?:"[^"]*"|\$<[^>]*>|[^\s"])+', body)
        out.append((m.group(1).lower(), [a[1:-1] if a[:1] == a[-1:] == '"' else a for a in args]))
    return out


class CMake:
    """Collects what one CMakeLists.txt builds, with a small interpreter for set/list/file(GLOB)/foreach."""

    def __init__(self, pkg_dir):
        self.dir = Path(pkg_dir).resolve()
        self.vars = {"CMAKE_CURRENT_SOURCE_DIR": str(self.dir), "PROJECT_SOURCE_DIR": str(self.dir)}
        self.libraries, self.executables, self.tests, self.components = {}, set(), {}, set()
        self.sources, self.copts, self.defines, self.features = set(), set(), set(), set()
        self.share, self.programs, self.installs, self.python, self.hooks = set(), set(), set(), set(), set()
        self.ifs, self.unhandled, self.timeouts, self.dynamic = set(), set(), {}, set()

    def expand(self, s):
        for _ in range(5):
            s = re.sub(r"\$\{(\w+)\}", lambda m: self.vars.get(m.group(1), m.group(0)), s)
        return s

    def words(self, args):
        return [w for a in args for w in self.expand(a).split(";") if w]

    def glob(self, recursive, pattern):
        base = Path(pattern)
        if not base.is_absolute():
            base = self.dir / pattern
        root, pat = base.parent, base.name
        while "*" in root.name or root.name == "**":
            root = root.parent
        files = root.rglob(pat) if recursive or "**" in pattern else root.glob(pat)
        return sorted(str(f) for f in files if f.is_file())

    def rel(self, f):
        p = Path(f)
        p = p if p.is_absolute() else self.dir / p
        try:
            return p.resolve().relative_to(self.dir.resolve()).as_posix()
        except ValueError:
            return f

    def add_sources(self, items):
        rest, it = [], iter(items)
        for a in it:
            if a == "DIRECTORY":
                d = self.dir / next(it)
                self.sources |= {self.rel(f) for f in d.rglob("*") if f.suffix in SOURCE_EXT}
            elif a.endswith(SOURCE_EXT):
                if "$" in a:
                    self.dynamic.add(a)
                else:
                    self.sources.add(self.rel(a))
            else:
                rest.append(a)
        return rest

    def run(self, cmds):
        i = 0
        while i < len(cmds):
            name, raw = cmds[i]
            if name == "foreach":
                depth, j = 1, i + 1
                while depth:
                    depth += {"foreach": 1, "endforeach": -1}.get(cmds[j][0], 0)
                    j += 1
                body = cmds[i + 1:j - 1]
                var, items = raw[0], raw[1:]
                if items[:1] == ["IN"] and items[1:2] == ["LISTS"]:
                    items = self.words(["${%s}" % v for v in items[2:]])
                elif items[:1] == ["IN"] and items[1:2] == ["ITEMS"]:
                    items = self.words(items[2:])
                else:
                    items = self.words(items)
                for item in items:
                    self.vars[var] = item
                    self.run(body)
                i = j
                continue
            self.command(name, raw)
            i += 1

    def command(self, name, raw):
        a = self.words(raw)
        if name == "project":
            self.vars["PROJECT_NAME"] = a[0]
        elif name == "set" and a:
            vals = [v for v in a[1:] if v not in ("CACHE", "PARENT_SCOPE", "FORCE")]
            self.vars[a[0]] = ";".join(vals)
        elif name == "list" and a[:1] == ["APPEND"]:
            cur = self.vars.get(a[1], "")
            self.vars[a[1]] = ";".join(([cur] if cur else []) + a[2:])
        elif name == "file" and a[:1] and a[0] in ("GLOB", "GLOB_RECURSE"):
            found = []
            for p in a[2:]:
                if p not in ("CONFIGURE_DEPENDS", "RELATIVE"):
                    found += self.glob(a[0] == "GLOB_RECURSE", p)
            self.vars[a[1]] = ";".join(found)
        elif name == "get_filename_component":
            out, path, mode = a[0], Path(a[1]), a[2]
            self.vars[out] = {"NAME": path.name, "NAME_WE": path.name.split(".")[0],
                              "DIRECTORY": str(path.parent)}.get(mode, a[1])
        elif name == "if":
            cond = " ".join(raw)
            if not TRIVIAL_IF.search(cond):
                self.ifs.add(" ".join(cond.split()))
        elif name in ("ament_auto_add_library", "add_library") and "IMPORTED" not in a:
            kind = "STATIC" if "STATIC" in a else "SHARED"
            self.libraries[a[0]] = kind
            self.add_sources([x for x in a[1:] if x not in ("SHARED", "STATIC", "MODULE")])
        elif name in ("ament_auto_add_executable", "add_executable"):
            self.executables.add(a[0])
            self.add_sources(a[1:])
        elif name in ("ament_add_ros_isolated_gtest", "ament_auto_add_gtest", "ament_add_gtest", "ament_add_gmock",
                      "ament_add_ros_isolated_gmock"):
            rest = self.add_sources(a[1:])
            self.tests[a[0]] = "cc"
            if "TIMEOUT" in rest:
                self.timeouts[a[0]] = rest[rest.index("TIMEOUT") + 1]
        elif name == "ament_add_pytest_test":
            self.tests[a[0]] = "py"
        elif name in ("rclcpp_components_register_node", "autoware_agnocast_wrapper_register_node"):
            if "PLUGIN" in a:
                self.components.add(a[a.index("PLUGIN") + 1])
            if "EXECUTABLE" in a:
                self.executables.add(a[a.index("EXECUTABLE") + 1])
        elif name == "rclcpp_components_register_nodes":
            self.components |= set(a[1:])
        elif name in ("target_compile_options", "add_compile_options"):
            flags = a[1:] if name == "target_compile_options" else a
            self.copts |= {f for f in flags if f.startswith("-") and f not in AUTOWARE_FLAGS}
        elif name in ("target_compile_definitions", "add_compile_definitions"):
            defs = a[1:] if name == "target_compile_definitions" else a
            self.defines |= {d.split("=")[0] for d in defs if d not in ("PRIVATE", "PUBLIC", "INTERFACE")}
        elif name == "target_compile_features":
            self.features |= {f for f in a[1:] if f.startswith("cxx_std_")}
        elif name == "set_target_properties" and "CXX_STANDARD" in a:
            std = a[a.index("CXX_STANDARD") + 1]
            if std != "17":
                self.features.add("cxx_std_" + std)
        elif name in ("ament_auto_package", "autoware_ament_auto_package"):
            if "INSTALL_TO_SHARE" in a:
                for d in a[a.index("INSTALL_TO_SHARE") + 1:]:
                    if d.isupper():
                        break
                    self.share.add(d)
        elif name == "install":
            dest = a[a.index("DESTINATION") + 1] if "DESTINATION" in a else ""
            items = a[1:a.index("DESTINATION")] if "DESTINATION" in a else a[1:]
            if a[0] == "PROGRAMS":
                self.programs |= {self.rel(x) for x in items}
            elif a[0] == "DIRECTORY" and dest == "share/" + self.vars.get("PROJECT_NAME", ""):
                self.share |= {x.rstrip("/") for x in items}
            else:
                self.installs.add(f"{a[0]}:{','.join(self.rel(x) for x in items)}->{dest}")
        elif name == "ament_python_install_package":
            self.python.add(a[0])
        elif name == "ament_environment_hooks":
            self.hooks |= {self.rel(x) for x in a}
        elif name.startswith("ament_export_") or name.startswith("ament_auto_") or name in IGNORED:
            pass
        else:
            self.unhandled.add(name)


# ---------------------------------------------------------------------------------------------- Bazel reading
def bazel_rules(module):
    """Rules of @module//:* after macro expansion, as {name: {"kind": ..., attr: value}}."""
    out = subprocess.run(
        ["bazel", "query", "--output=build", f"kind(rule, @{module}//:*)"],
        capture_output=True, text=True, check=True,
    ).stdout
    rules = {}
    for node in ast.parse(out).body:
        if not (isinstance(node, ast.Expr) and isinstance(node.value, ast.Call)):
            continue
        attrs = {"kind": getattr(node.value.func, "id", "")}
        for kw in node.value.keywords:
            try:
                attrs[kw.arg] = ast.literal_eval(kw.value)
            except ValueError:
                attrs[kw.arg] = None
        rules[attrs["name"]] = attrs
    return rules


def label_path(label):
    return label.split("//:", 1)[1] if "//:" in label else label


def recorded(pkg_dir):
    text = "".join((pkg_dir / f).read_text() for f in ("BUILD.bazel", "MODULE.bazel") if (pkg_dir / f).exists())
    return dict(re.findall(r"#\s*cmake-diff\s+(.+?):\s+(.*)", text))


def package_deps(pkg_dir):
    root = ET.parse(pkg_dir / "package.xml").getroot()
    tags = ("depend", "build_depend", "build_export_depend", "test_depend")
    names = {e.text.strip() for t in tags for e in root.findall(t)}
    return {n for n in names if not TOOLING_DEP.match(n) and not SYSTEM_DEP.match(n)}


# ---------------------------------------------------------------------------------------------- comparison
def findings(pkg_dir):
    pkg_dir = Path(pkg_dir)
    module = ET.parse(pkg_dir / "package.xml").getroot().findtext("name").strip()
    cm = CMake(pkg_dir)
    cm.vars["PROJECT_NAME"] = module
    cmds = commands((pkg_dir / "CMakeLists.txt").read_text())
    if any(name == "rosidl_generate_interfaces" for name, _ in cmds):
        return module, None  # interface package: its BUILD files follow mechanically from the .msg/.srv files
    cm.run(cmds)
    rules = bazel_rules(module)

    def strings(*attrs, kinds=None):
        return {v for r in rules.values() if kinds is None or kinds(r["kind"])
                for a in attrs for v in (r.get(a) or []) if isinstance(v, str)}

    found = set()
    for lib, kind in cm.libraries.items():
        want = "cc_library" if kind == "STATIC" else "cc_shared_library"
        if not (lib in rules and rules[lib]["kind"] == want):
            found.add(f"library:{lib}")
    found |= {f"executable:{e}" for e in cm.executables if e not in rules}
    plugins = {r.get("plugin") for r in rules.values()} | strings("plugins")
    found |= {f"component:{p}" for p in cm.components if p not in plugins}
    found |= {f"test:{t}" for t in cm.tests if not (t in rules and rules[t]["kind"].endswith("test"))}
    tiers = {"short": 60, "moderate": 300, "long": 900, "eternal": 3600}
    for t, secs in cm.timeouts.items():
        if tiers.get(rules.get(t, {}).get("timeout")) != int(secs):
            found.add(f"timeout:{t}")  # Bazel has only fixed tiers
    bazel_files = {label_path(s) for s in strings("srcs", "hdrs")}
    found |= {f"source:{s}" for s in cm.sources if s not in bazel_files}
    # Only files in the package: macros such as rclcpp_components_register_node generate their own main.cpp.
    found |= {f"extra-source:{s}" for s in bazel_files
              if s.endswith(COMPILED_EXT) and s not in cm.sources and (pkg_dir / s).is_file()}
    found |= {f"dynamic-source:{s}" for s in cm.dynamic}
    opts = strings("copts", "cxxopts", "conlyopts")
    found |= {f"copt:{c}" for c in cm.copts if c not in opts}
    defines = {d.split("=")[0] for d in strings("local_defines", "defines")}
    found |= {f"define:{d}" for d in cm.defines if d not in defines}
    std = {o for o in opts if o.startswith("-std=")}
    found |= {f"feature:{f}" for f in cm.features if f"-std=c++{f[len('cxx_std_'):]}" not in std}
    data = {label_path(s) for s in strings("data", kinds=lambda k: k.startswith("ament_index"))}
    found |= {f"share:{d}" for d in cm.share if not any(p.startswith(d + "/") for p in data)}
    found |= {f"program:{p}" for p in cm.programs if p not in bazel_files}
    found |= {f"install:{i}" for i in cm.installs}
    found |= {f"python-package:{p}" for p in cm.python if not any(r["kind"] == "py_library" for r in rules.values())}
    found |= {f"env-hook:{h}" for h in cm.hooks}
    found |= {f"if:{c}" for c in cm.ifs}
    found |= {f"unhandled:{c}" for c in cm.unhandled}
    module_text = (pkg_dir / "MODULE.bazel").read_text()
    found |= {f"dep:{d}" for d in package_deps(pkg_dir) if f'name = "{d}"' not in module_text}
    return module, found


def main(argv):
    status = 0
    for d in argv:
        module, found = findings(d)
        if found is None:
            print(f"{module}: interface package, not checked")
            continue
        rec = recorded(Path(d))
        open_ = sorted(found - set(rec))
        stale = sorted(set(rec) - found)
        if open_ or stale:
            status = 1
        print(f"{module}: {len(open_)} open, {len(found & set(rec))} recorded, {len(stale)} stale")
        for k in open_:
            print(f"  OPEN   {k}")
        for k in stale:
            print(f"  STALE  {k}")
    return status


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
