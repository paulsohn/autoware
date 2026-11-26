"""Observe discovery election in a single IPC island with two simultaneous claimants."""
import json
import os
from pathlib import Path
import subprocess
import sys
import time
from contextlib import ExitStack


def until(predicate, label):
    deadline = time.monotonic() + 18
    while not predicate():
        if time.monotonic() >= deadline:
            raise TimeoutError(label)
        time.sleep(.02)


def contender(root, name, mode):
    if mode == "auto":
        import rclpy
    (root / (name + ".waiting")).touch()
    until((root / "go").exists, "start barrier")
    config = str(root / (name + ".json"))
    if mode == "manual":
        exe = os.environ["AGNOCAST_GATEWAY_EXECUTABLE"]
        os.execv(exe, [exe, "--config", config])
    os.environ["RMW_AGNOCAST_GATEWAY_CONFIG"] = config
    rclpy.init()
    (root / (name + ".joined")).touch()
    until((root / (name + ".exit")).exists, "context release")
    rclpy.shutdown()


if len(sys.argv) == 4:
    contender(Path(sys.argv[1]), sys.argv[2], sys.argv[3])
    sys.exit(0)


def run_case(root, network, modes, compatible, node):
    from rmw_agnocast_discovery_msgs.msg import IslandSnapshot
    import rclpy
    root.mkdir()
    generations = set()
    visible = set()
    def observe(msg):
        topics = {ep.topic_name for ep in msg.endpoints}
        if topics:
            generations.add(msg.generation)
            visible.update(topics)
    sub = node.create_subscription(IslandSnapshot, "/_rmw_agnocast/discovery_v2", observe, 10)
    with ExitStack() as stack:
        stack.callback(node.destroy_subscription, sub)
        processes = {}
        for name, mode in zip(("a","b"), modes):
            prefix = "/race/a" if compatible else "/race/" + name
            config = dict(dynamic_discovery=True, discovery_namespace=prefix, network_rmw=network,
                          heaphook=os.environ["TEST_HEAPHOOK"])
            (root/(name+".json")).write_text(json.dumps(config))
            env = dict(os.environ)
            if mode == "auto":
                env.update(RMW_IMPLEMENTATION="rmw_agnocast", LD_PRELOAD=config["heaphook"])
            log = stack.enter_context((root/(name+".log")).open("w"))
            proc = subprocess.Popen([sys.executable,__file__,str(root),name,mode],
                                    env=env,stdout=log,stderr=subprocess.STDOUT)
            processes[name] = proc
            def cleanup(p=proc):
                if p.poll() is None:
                    p.terminate()
                    p.wait(timeout=10)
            stack.callback(cleanup)
        until(lambda: all((root/(n+".waiting")).exists() for n in processes), "two waiting contenders")
        (root/"go").touch()
        def settled():
            rclpy.spin_once(node,timeout_sec=.05)
            joined = sum((root/(n+".joined")).exists() for n in processes)
            rejected = sum(p.poll() not in (None,0) for p in processes.values())
            return visible and ((joined==2) if compatible else rejected==1)
        until(settled, "election with observable inventory")
        owners = [n for n in processes if "owner pid=" in (root/(n+".log")).read_text()]
        assert len(owners)==1 and len(generations)==1, (owners,generations)
        winner, = owners
        expected = "/race/a/data" if compatible else f"/race/{winner}/data"
        assert visible == {expected}, visible
        if compatible:
            (root/(winner+".exit")).touch()
            assert processes[winner].wait(timeout=10)==0
            visible.clear()
            def surviving_owner():
                rclpy.spin_once(node,timeout_sec=.1)
                return sub.get_publisher_count()==1 and visible
            until(surviving_owner, "remaining lease")
            assert visible=={expected} and len(generations)==1
        else:
            loser = "b" if winner=="a" else "a"
            assert processes[loser].returncode not in (None, 0)
            assert "discovery gateway already owns ROS domain" in (root/(loser+".log")).read_text()
            assert processes[winner].poll() is None
        for name, mode in zip(("a","b"),modes):
            if mode=="auto":
                (root/(name+".exit")).touch()
            elif processes[name].poll() is None:
                processes[name].terminate()
        for proc in processes.values():
            proc.wait(timeout=10)
        def detached():
            rclpy.spin_once(node,timeout_sec=.05)
            return sub.get_publisher_count()==0
        until(detached, "discovery owner cleanup")
    print(root.name,winner,expected,flush=True)


root=Path(sys.argv[1]).resolve()
root.mkdir()
network = os.environ["TEST_RACE_NETWORK"]
os.environ["RMW_IMPLEMENTATION"] = network
import rclpy
from rclpy.utilities import get_rmw_implementation_identifier

# Keep both native offers live throughout all elections, independently of contenders.
offer_code="""import rclpy,time
from std_msgs.msg import Int32
rclpy.init()
node=rclpy.create_node('cold_race_offers')
pubs=[node.create_publisher(Int32,'/race/'+key+'/data',10) for key in ('a','b')]
while True: rclpy.spin_once(node,timeout_sec=.1)
"""
with ExitStack() as stack:
    def launch(command, env, label):
        log=stack.enter_context((root/(label+".log")).open("w"))
        p=subprocess.Popen(command,env=env,stdout=log,stderr=subprocess.STDOUT)
        def stop():
            p.terminate()
            p.wait(timeout=10)
        stack.callback(stop)
        return p
    launch([sys.executable,"-c",offer_code],dict(os.environ,RMW_IMPLEMENTATION="rmw_agnocast",
           LD_PRELOAD=os.environ["TEST_HEAPHOOK"]),"offers")
    if network=="rmw_zenoh_cpp":
        launch(["/opt/ros/lyrical/lib/rmw_zenoh_cpp/rmw_zenohd"],os.environ,"router")
        time.sleep(.6)
    rclpy.init()
    assert get_rmw_implementation_identifier() == network
    node=rclpy.create_node("race_wire_observer")
    try:
        for repeat in range(2):
            for label,modes,compatible in (("shared",("auto","auto"),True),
                    ("exclusive",("auto","auto"),False),("mixed",("manual","auto"),False)):
                run_case(root/f"{repeat}-{label}",network,modes,compatible,node)
    finally:
        node.destroy_node()
        rclpy.shutdown()
