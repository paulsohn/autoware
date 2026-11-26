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
    fault = out.name in ("crash","manual-stop","legacy")
    if fault:
        start=float((out/"epoch").read_text()) + (.025 if role=="b" else 0)
    for n in range(120 if fault else 20):
        if fault:
            time.sleep(max(0,start+n*.05-time.monotonic()))
        pub.publish(String(data=f"{role}-{n}"))
        if not fault:
            time.sleep(.02)
    (out/("published-"+role)).touch()
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
arrivals = {}
def record(msg):
    received.append(msg.data)
    arrivals[msg.data] = time.monotonic()
sub = node.create_subscription(String,"/ownership/output",record,10)
def spin_until(condition):
    def tick():
        rclpy.spin_once(node,timeout_sec=.02)
        return condition()
    wait_for(tick)

fault_mode = os.getenv("TEST_OWNER_CRASH")
for mode in ((fault_mode,) if fault_mode else ("auto","manual")):
    case = out/mode
    case.mkdir()
    procs = []
    def start(label,cmd,env):
        with (case/(label+".log")).open("w") as f:
            proc = subprocess.Popen(cmd,stdout=f,stderr=f,env=env)
        procs.append(proc)
        return proc
    try:
        if mode in ("manual","manual-stop"):
            manual=start("owner",[gateway,*route],os.environ)
            spin_until(lambda:sub.get_publisher_count()==1)
        env=dict(os.environ,RMW_IMPLEMENTATION="rmw_agnocast",LD_PRELOAD=hook,
                 RMW_AGNOCAST_GATEWAY_CONFIG=str(out/"route.json"))
        if mode == "manual":
            mismatch=case/"different-depth.json"
            mismatch.write_text(json.dumps(route+["--depth","7"]))
            rejected=start("incompatible",[sys.executable,"-c","import rclpy; rclpy.init()"],
                           dict(env,RMW_AGNOCAST_GATEWAY_CONFIG=str(mismatch)))
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
        if mode not in ("manual","manual-stop"):
            winners=[k for k in apps if "ready role=destination" in (case/(k+".log")).read_text()]
            assert len(winners)==1,winners
            labels=sorted(apps,key=lambda k:k!=winners[0])
            pid=int(re.search(r"owner pid=(\d+)",(case/(labels[0]+".log")).read_text())[1])
            owner_fd=os.pidfd_open(pid)
            exited=select.poll()
            exited.register(owner_fd,select.POLLIN)
        received.clear()
        expected=[]
        if mode=="exhaust":
            for generation in range(4):
                signal.pidfd_send_signal(owner_fd,signal.SIGKILL)
                assert exited.poll(4000)
                os.close(owner_fd)
                if generation<3:
                    marker=f"rejoined gateway on recovery attempt {generation+1}"
                    wait_for(lambda:all(marker in (case/(k+".log")).read_text() for k in apps))
                    owners=[int(i) for k in apps for i in re.findall(r"owner pid=(\d+)",(case/(k+".log")).read_text())]
                    assert len(set(owners))==generation+2
                    replacement,=set(owners)-set(previous if generation else [pid])
                    previous=owners
                    owner_fd=os.pidfd_open(replacement)
                    exited=select.poll()
                    exited.register(owner_fd,select.POLLIN)
            wait_for(lambda:all("exhausted three attempts" in (case/(k+".log")).read_text() for k in apps))
            spin_until(lambda:sub.get_publisher_count()==0)
            print("both live contexts exhausted three replacements after four supervisor kills",flush=True)
            continue
        if fault_mode:
            if mode=="manual-stop":
                pid=manual.pid
                owner_fd=os.pidfd_open(pid)
                exited=select.poll()
                exited.register(owner_fd,select.POLLIN)
            epoch=time.monotonic()+.6
            (case/"epoch").write_text(str(epoch))
            for k in labels:
                (case/("send-"+k)).touch()
            spin_until(lambda:all(sum(m.startswith(k+"-") for m in received)>=10 for k in labels))
            killed=time.monotonic()
            before=set(received)
            signal.pidfd_send_signal(owner_fd,signal.SIGKILL)
            assert exited.poll(4000)
            os.close(owner_fd)
            def owners():
                return [int(i) for k in apps for i in re.findall(r"owner pid=(\d+)",(case/(k+".log")).read_text())]
            if mode in ("legacy","manual-stop"):
                spin_until(lambda:all((case/("published-"+k)).exists() for k in labels))
                spin_until(lambda:sub.get_publisher_count()==0)
                assert len(owners())==(1 if mode=="legacy" else 0)
                assert not any(int(m.split("-")[1])>=100 for m in received)
                print(f"{mode} both publishers completed; no replacement; delivered={len(received)}/240",flush=True)
                continue
            spin_until(lambda:len(owners())>=2)
            replacement,=set(owners())-{pid}
            owner_fd=os.pidfd_open(replacement)
            exited=select.poll()
            exited.register(owner_fd,select.POLLIN)
            spin_until(lambda:all(f"{k}-119" in received for k in labels))
            for k in labels:
                nums=[int(m.split("-")[1]) for m in received if m.startswith(k+"-")]
                assert nums==sorted(set(nums)) and nums[-10:]==list(range(110,120))
            assert len(owners())==2
            fresh=min(t for m,t in arrivals.items() if epoch+int(m.split("-")[1])*.05>killed)
            result=dict(sent=240,received=len(received),before_kill=len(before),owners=len(owners()),
                        recovery_ms=round((fresh-killed)*1000,1))
            (case/"result.json").write_text(json.dumps(result))
            print(f"crash replacement={replacement} original={pid} before={len(before)} delivered={len(received)}/240 recovery={result}",flush=True)
            expected=list(received)
        for k in labels:
            if fault_mode:
                (case/("exit-"+k)).touch()
                assert apps[k].wait(timeout=10)==0
                continue
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
        print(mode,"first exit",labels[0],f"{len(received)} unique ordered samples; owner lifecycle passed",flush=True)
    finally:
        for k in ("a","b"):
            (case/("exit-"+k)).touch()
        for p in reversed(procs):
            if p.poll() is None:
                p.terminate()
                p.wait(timeout=10)
node.destroy_node()
rclpy.shutdown()
