"""Inside the guarded Lyrical runtime, with both gateway and native overlays sourced."""
import json
import os
from pathlib import Path
import signal
import re
import select
import subprocess
import sys
import time

out = Path(sys.argv[1]).resolve()
role = sys.argv[2] if len(sys.argv)>2 else None


def wait_for(condition):
    deadline = time.monotonic()+15
    while not condition():
        if time.monotonic()>deadline:
            raise TimeoutError(condition)
        time.sleep(.01)


if role:
    import rclpy
    from std_msgs.msg import String
    (out/(role+"-waiting")).touch()
    wait_for((out/"start").exists)
    rclpy.init()
    node = rclpy.create_node("ownership_"+role)
    pub = node.create_publisher(String,"/ownership/input",10)
    (out/(role+"-ready")).touch()
    wait_for((out/("send-"+role)).exists)
    wait_for(pub.get_subscription_count)
    for n in range(20):
        pub.publish(String(data=f"{role}-{n}"))
        time.sleep(.02)
    wait_for((out/("exit-"+role)).exists)
    node.destroy_node()
    rclpy.shutdown()
    sys.exit(0)

import rclpy
from std_msgs.msg import String

gateway = os.environ["AGNOCAST_GATEWAY_EXECUTABLE"]
hook = os.environ["TEST_HEAPHOOK"]
route = ["export","std_msgs/msg/String","/ownership/input","/ownership/output","--heaphook",hook]
out.mkdir()
(out/"route.json").write_text(json.dumps(route))
rclpy.init()
node = rclpy.create_node("cyclone_witness")
received = []
sub = node.create_subscription(String,"/ownership/output",lambda m:received.append(m.data),10)
def spin_until(condition):
    def tick():
        rclpy.spin_once(node,timeout_sec=.02)
        return condition()
    wait_for(tick)

for mode in ("auto","manual"):
    case = out/mode
    case.mkdir()
    procs = []
    def start(label,cmd,env):
        with (case/(label+".log")).open("w") as f:
            proc = subprocess.Popen(cmd,stdout=f,stderr=f,env=env)
        procs.append(proc)
        return proc
    try:
        if mode=="manual":
            manual=start("owner",[gateway,*route],os.environ)
            spin_until(lambda:sub.get_publisher_count()==1)
        env=dict(os.environ,RMW_IMPLEMENTATION="rmw_agnocast",LD_PRELOAD=hook,
                 AGNOCAST_GATEWAY_CONFIG=str(out/"route.json"))
        if mode == "manual":
            mismatch=case/"different-depth.json"
            mismatch.write_text(json.dumps(route+["--depth","7"]))
            rejected=start("incompatible",[sys.executable,"-c","import rclpy; rclpy.init()"],
                           dict(env,AGNOCAST_GATEWAY_CONFIG=str(mismatch)))
            assert rejected.wait(timeout=15)!=0
            assert "conflicting route" in (case/"incompatible.log").read_text()
            assert manual.poll() is None and sub.get_publisher_count()==1
        apps={label:start(label,[sys.executable,__file__,str(case),label],env) for label in ("a","b")}
        wait_for(lambda:all((case/(k+"-waiting")).exists() for k in apps))
        (case/"start").touch()
        wait_for(lambda:all((case/(k+"-ready")).exists() for k in apps))
        spin_until(lambda:sub.get_publisher_count()==1)
        # Either app may have elected the owner. Both log streams are separate files.
        labels=list(apps)
        if mode=="auto":
            winners=[k for k in apps if "ready role=destination" in (case/(k+".log")).read_text()]
            assert len(winners)==1,winners
            labels=sorted(apps,key=lambda k:k!=winners[0])
            pid=int(re.search(r"owner pid=(\d+)",(case/(labels[0]+".log")).read_text())[1])
            owner_fd=os.pidfd_open(pid)
            exited=select.poll()
            exited.register(owner_fd,select.POLLIN)
        received.clear()
        expected=[]
        for k in labels:
            (case/("send-"+k)).touch()
            expected.extend(f"{k}-{n}" for n in range(20))
            spin_until(lambda:len(received)>=len(expected))
            assert received==expected and sub.get_publisher_count()==1
            (case/("exit-"+k)).touch()
            assert apps[k].wait(timeout=10)==0
        if mode=="manual":
            assert manual.poll() is None and sub.get_publisher_count()==1
            manual.send_signal(signal.SIGINT)
            assert manual.wait(timeout=10)==0
        else:
            assert exited.poll(8000), "automatic owner still holds the route"
            os.close(owner_fd)
        spin_until(lambda:sub.get_publisher_count()==0)
        assert received==expected
        print(mode,"first exit",labels[0],"40/40 unique ordered samples; owner lifecycle passed",flush=True)
    finally:
        for k in ("a","b"):
            (case/("exit-"+k)).touch()
        for p in reversed(procs):
            if p.poll() is None:
                p.terminate()
                p.wait(timeout=10)
node.destroy_node()
rclpy.shutdown()
