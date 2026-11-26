"""A late native writer, an ambiguous network input and a typed, remapped export in one group."""
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import time
from contextlib import ExitStack

import rclpy
from std_msgs.msg import Int32, String

case = Path(sys.argv[1]).resolve()
def wait(test, node=None):
    deadline = time.monotonic()+15
    while not test():
        if time.monotonic()>deadline:
            raise TimeoutError(test)
        if node:
            rclpy.spin_once(node,timeout_sec=.02)
        else:
            time.sleep(.02)

if len(sys.argv)>2:
    side = sys.argv[2]
    rclpy.init()
    node = rclpy.create_node("group_test_"+side)
    if side=="peer":
        # Both types exist before the owner is elected.
        cmd = node.create_publisher(String,"/cmd",10)
        competing = node.create_publisher(Int32,"/cmd",10)
        data, status = [], []
        node.create_subscription(Int32,"/data",lambda m:data.append(m.data),64)
        node.create_subscription(String,"/robot/status",lambda m:status.append(m.data),64)
        (case/"peer-ready").touch()
        wait((case/"resolve").exists,node)
        node.destroy_publisher(competing)
        wait(lambda:cmd.get_subscription_count()==1,node)
        for i in range(20):
            cmd.publish(String(data=str(i)))
            time.sleep(.03)
        wait(lambda:len(data)>=20 and len(status)>=20,node)
        assert data==list(range(20)) and status==list(map(str,range(20)))
    else:
        incoming=[]
        node.create_subscription(String,"/cmd",lambda m:incoming.append(m.data),64)
        (case/"context-ready").touch()
        wait((case/"resolve").exists,node)
        data=node.create_publisher(Int32,"/data",10)
        status=node.create_publisher(String,"/status",10)
        wait(lambda:data.get_subscription_count()==1 and status.get_subscription_count()==1,node)
        for i in range(20):
            data.publish(Int32(data=i))
            status.publish(String(data=str(i)))
            time.sleep(.03)
        wait(lambda:len(incoming)>=20,node)
        assert incoming==list(map(str,range(20)))
    (case/(side+"-done")).touch()
    wait((case/"stop").exists,node)
    node.destroy_node()
    rclpy.shutdown()
    sys.exit(0)

case.mkdir()
exe = os.environ["AGNOCAST_GATEWAY_EXECUTABLE"]
def start(stack,log,cmd,env):
    output=stack.enter_context(log.open("w"))
    proc=subprocess.Popen(cmd,env=env,stdout=output,stderr=output)
    def stop():
        if proc.poll() is None:
            proc.terminate()
            proc.wait(timeout=12)
    stack.callback(stop)
    return proc

for backend in ("rmw_cyclonedds_cpp","rmw_zenoh_cpp"):
    for mode in ("auto","manual"):
        root=case/(backend+"-"+mode)
        root.mkdir()
        peer_env=dict(os.environ,RMW_IMPLEMENTATION=backend)
        peer_env.pop("RMW_AGNOCAST_GATEWAY_CONFIG",None)
        hook=os.environ["TEST_HEAPHOOK"]
        config=dict(network_rmw=backend, heaphook=hook, exports=[dict(topic="/data"),dict(topic="/status",remap="/robot/status",type="std_msgs/msg/String")],imports=[dict(topic="/cmd")])
        path=root/"route.json"
        path.write_text(json.dumps(config))
        with ExitStack() as stack:
            if backend=="rmw_zenoh_cpp":
                router=start(stack,root/"router.log",["/opt/ros/lyrical/lib/rmw_zenoh_cpp/rmw_zenohd"],peer_env)
                time.sleep(.6)
                assert router.poll() is None
            peer=start(stack,root/"peer.log",[sys.executable,__file__,str(root),"peer"],peer_env)
            wait((root/"peer-ready").exists)
            log=root/("owner.log" if mode=="manual" else "native.log")
            if mode=="manual":
                owner=start(stack,log,[exe,"--config",str(path)],peer_env)
            native_env=dict(peer_env,RMW_IMPLEMENTATION="rmw_agnocast",LD_PRELOAD=hook,RMW_AGNOCAST_GATEWAY_CONFIG=str(path))
            if mode=="manual":
                # Equivalent configurations must join the same manual owner across route ordering.
                wait(lambda:log.exists() and "owner pid=" in log.read_text())
                reordered=root/"reordered.json"
                reordered.write_text(json.dumps(dict(config,exports=config["exports"][::-1])))
                native_env["RMW_AGNOCAST_GATEWAY_CONFIG"]=str(reordered)
            native=start(stack,root/"native.log",[sys.executable,__file__,str(root),"native"],native_env)
            wait((root/"context-ready").exists)
            wait(lambda:"std_msgs/msg/Int32', 'std_msgs/msg/String" in log.read_text())
            wait(lambda:"ready role=destination" in log.read_text())
            assert "ready role=destination rmw="+backend+" type=std_msgs/msg/String" in log.read_text()
            assert "ready role=destination rmw="+backend+" type=std_msgs/msg/Int32" not in log.read_text()
            # A separate gateway attempting to reflect our same-name export back to native
            # must fail while this owner continues to carry all three routes.
            reverse=subprocess.run([exe,"import","std_msgs/msg/Int32","/data","/data",
                                    "--network-rmw",backend,"--heaphook",hook],
                                   env=peer_env,capture_output=True,text=True,timeout=8)
            assert reverse.returncode and "conflicting route" in reverse.stderr,reverse.stderr
            bad=root/"cycle.json"
            bad.write_text(json.dumps(dict(config,imports=[dict(topic="/data")])))
            cycle=subprocess.run([exe,"--config",str(bad)],env=peer_env,capture_output=True,text=True,timeout=8)
            assert cycle.returncode and "destination must be unique" in cycle.stderr,cycle.stderr
            (root/"resolve").touch()
            wait(lambda:all((root/(s+"-done")).exists() for s in ("peer","native")))
            (root/"stop").touch()
            assert native.wait(timeout=10)==0 and peer.wait(timeout=10)==0
            if mode=="manual":
                owner.send_signal(signal.SIGINT)
                assert owner.wait(timeout=10)==0
            wait(lambda:log.read_text().count("stats role=destination samples=20 ")==3)
            assert log.read_text().count("owner pid=")==1
            print(backend,mode,"3 routes x 20/20",flush=True)
