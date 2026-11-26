"""Run one upstream GoogleTest per process so an abort cannot hide later tests."""
import csv
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import xml.etree.ElementTree as ET


def list_cases(program, environment):
    listing = subprocess.check_output(
        [program, "--gtest_list_tests"], env=environment, text=True
    )
    group = None
    cases = []
    for line in listing.splitlines():
        name = line.split("#", 1)[0].strip()
        if not line.startswith(" ") and name.endswith("."):
            group = name
        elif group and line.startswith("  ") and name:
            cases.append(group + name)
    if not cases or len(cases) != len(set(cases)):
        raise RuntimeError(f"Unexpected GoogleTest inventory for {program}: {listing}")
    return cases


def run_case(program, case, directory, environment):
    directory.mkdir(parents=True)
    if any(part.startswith("DISABLED_") for part in re.split(r"[./]", case)):
        return "disabled"
    xml = directory / "result.xml"
    with (directory / "log").open("w") as log:
        proc = subprocess.Popen(
            [program, "--gtest_filter=" + case, "--gtest_output=xml:" + str(xml)],
            env=environment,
            cwd=directory,
            stdout=log,
            stderr=subprocess.STDOUT,
            start_new_session=True,
        )
        try:
            rc = proc.wait(timeout=45)
        except subprocess.TimeoutExpired:
            os.killpg(proc.pid, signal.SIGKILL)
            proc.wait()
            return "timeout"
    if rc < 0:
        return signal.Signals(-rc).name
    if not xml.exists():
        return f"exit_{rc}_without_xml"
    test, = ET.parse(xml).findall(".//testcase")
    if test.find("failure") is not None or rc:
        return "failed"
    if test.find("skipped") is not None:
        return "skipped"
    return "passed"


workspace, hook, output = (Path(arg).resolve() for arg in sys.argv[1:])
output.mkdir()
env = dict(os.environ, LD_PRELOAD=str(hook), RMW_IMPLEMENTATION="rmw_agnocast")
programs = [
    ("rmw", path) for path in sorted((workspace / "conformance-rmw").glob("test_*"))
    if path.is_file() and os.access(path, os.X_OK)
]
if len(programs) != 16:
    raise RuntimeError(f"Expected 16 upstream RMW test programs, found {programs}")
for name in ("test_executor", "test_loaned_message", "test_subscription", "test_node_interfaces__node_graph"):
    exe, = [p for p in (workspace / "build/rclcpp").rglob(name) if p.is_file()]
    programs.append(("rclcpp", exe))

with (output / "cases.csv").open("w", buffering=1) as stream:
    writer = csv.writer(stream, lineterminator="\n")
    writer.writerow(("suite", "program", "case", "status"))
    for suite, exe in programs:
        cases = list_cases(exe, env)
        for index, case in enumerate(cases):
            directory = output / suite / exe.name / str(index)
            status = run_case(exe, case, directory, env)
            writer.writerow((suite, exe.name, case, status))
        print(suite, exe.name, len(cases), "cases", flush=True)
