"""Timed endpoints sharing the 600-frame Universe replay's clock and ROS domain."""
import json
import sys
import time
from pathlib import Path

import rclpy
from std_msgs.msg import Int32

case, role, policy = sys.argv[1:]
case = Path(case)
rclpy.init()
node = rclpy.create_node("churn_" + role)
pubs, subs, received = {}, {}, {}
retired = set()
old = None
start = case / "churn-start"
(case / ("ready-" + role)).touch()
until = time.monotonic() + 40
while not start.exists():
    assert time.monotonic() < until
    time.sleep(.05)
t0 = int(start.read_text())

def emit(**event):
    print(json.dumps(dict(ns=time.time_ns(), **event)), flush=True)

def collect(topic, msg):
    if topic not in received:
        emit(first=topic, value=msg.data)
    received.setdefault(topic, []).append(msg.data)

last_tick = -1
while True:
    t = (time.time_ns() - t0) / 1e9
    if t >= 54:
        break
    phase = ("stable" if t < 7 else "offers12" if t < 14 else "offers48" if t < 21
             else "churn" if t < 31 else "one" if t < 38 else "two" if t < 47 else "detach")
    names = set()
    if phase in ("offers12", "offers48", "churn", "one", "two", "detach"):
        names.update("offer" + str(i) for i in range(12 if phase == "offers12" else 48))
    if phase == "churn":
        names.update(f"changing{int(t)%2}_{i}" for i in range(8))
    active = {"active1","active2"}
    if role == "native":
        names.update(active)
        if phase == "churn" and pubs.keys() != names:
            emit(churn_created=len(names - pubs.keys()), churn_removed=len(pubs.keys() - names))
        for name in sorted(pubs.keys() - names):
            node.destroy_publisher(pubs.pop(name))
        for name in sorted(names - pubs.keys()):
            pubs[name] = node.create_publisher(Int32, "/churn/" + name, 10)
        tick = int(t * 10)
        if tick != last_tick:
            for name in active:
                pubs[name].publish(Int32(data=tick))
            last_tick = tick
    else:
        desired = {"active1"} if phase == "one" else active if phase == "two" else set()
        for name in sorted(subs.keys() - desired):
            node.destroy_subscription(subs.pop(name))
            emit(removed=name)
        for name in sorted(desired - subs.keys()):
            subs[name] = node.create_subscription(Int32, "/churn/" + name,
                lambda msg, n=name: collect(n, msg), 10)
            emit(created=name)
        if phase == "detach":
            for name in list(received):
                present = node.count_publishers("/churn/" + name)
                if policy == "configured-matched" or present == 0:
                    active_samples = received.pop(name)
                    assert active_samples == sorted(set(active_samples))
                    retired.add(name)
                    if policy == "configured-matched":
                        assert present > 0
                        emit(advertised=name, count=len(active_samples))
                    else:
                        emit(retired=name, count=len(active_samples))
    if phase != old:
        emit(phase=phase, publishers=len(pubs), subscriptions=len(subs))
        old = phase
    rclpy.spin_once(node, timeout_sec=.04)
if role == "network":
    # Preserve the seven-second retirement gate while recording what stayed visible.
    if received:
        for name, values in received.items():
            topic = "/churn/" + name
            for info in node.get_publishers_info_by_topic(topic):
                owner = info.node_name.rsplit("_",1)[-1]
                emit(late=topic, node=info.node_name, count=len(values),
                     worker_alive=owner.isdigit() and Path("/proc",owner).exists())
        deadline = time.monotonic()+12
        pending = set(received)
        while pending and time.monotonic()<deadline:
            rclpy.spin_once(node,timeout_sec=.1)
            for name in list(pending):
                if not node.count_publishers("/churn/"+name):
                    emit(late_retirement=name)
                    pending.remove(name)
        raise AssertionError(f"publishers visible at retirement deadline: {sorted(received)}")
    assert retired == ({"active1", "active2"} if policy in ("discovered","configured-matched") else set()), retired
node.destroy_node()
rclpy.shutdown()
