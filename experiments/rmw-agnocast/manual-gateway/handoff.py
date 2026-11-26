"""Exercise real context joins before and during retirement of an automatic gateway."""
import csv
import json
import os
from pathlib import Path
import re
import select
import signal
import subprocess
import sys
import time
from contextlib import ExitStack

import rclpy
from std_msgs.msg import String

out = Path(sys.argv[1]).resolve()
out.mkdir()
gateway = os.environ["AGNOCAST_GATEWAY_EXECUTABLE"]
hook = os.environ["TEST_HEAPHOOK"]
route = ["export","std_msgs/msg/String","/ownership/input","/ownership/output","--heaphook",hook]
config = out/"route.json"
config.write_text(json.dumps(route))
native = dict(os.environ,RMW_IMPLEMENTATION="rmw_agnocast",LD_PRELOAD=hook,AGNOCAST_GATEWAY_CONFIG=str(config))
rclpy.init()
node = rclpy.create_node("handoff_witness")
received = []
sub = node.create_subscription(String,"/ownership/output",lambda msg:received.append(msg.data),10)


def wait(test, timeout=12):
    deadline = time.monotonic()+timeout
    while not test():
        if time.monotonic()>deadline:
            raise TimeoutError(test)
        rclpy.spin_once(node,timeout_sec=.01)


def launch(stack, case, name, cmd, env):
    log=stack.enter_context((case/(name+".log")).open("w"))
    p=subprocess.Popen(cmd,env=env,stdout=log,stderr=log)
    def cleanup():
        if p.poll() is None:
            p.terminate()
            p.wait(timeout=10)
    stack.callback(cleanup)
    return p


def watch(stack,pid):
    fd=os.pidfd_open(pid)
    stack.callback(os.close,fd)
    poll=select.poll()
    poll.register(fd,select.POLLIN)
    return fd,poll


def run(mode, case, stack):
    apps={}
    received.clear()
    for k in ("a","b"):
        work=case/k
        work.mkdir()
        apps[k]=launch(stack,case,k,[sys.executable,str(Path(__file__).with_name("ownership.py")),str(work),k],native)
    wait(lambda:all((case/k/(k+"-waiting")).exists() for k in apps))
    def ready(k):
        wait(lambda:(case/k/(k+"-ready")).exists() or apps[k].poll() is not None)
        assert apps[k].poll() is None,(k,(case/(k+".log")).read_text()[-700:])
    def send(k):
        (case/k/("send-"+k)).touch()
        wait(lambda:k+"-19" in received)
    def finish(k):
        (case/k/("exit-"+k)).touch()
        assert apps[k].wait(timeout=8)==0
    (case/"a/start").touch()
    ready("a")
    send("a")
    log=(case/"a.log").read_text()
    old=int(re.search(r"owner pid=(\d+)",log)[1])
    workers=list(map(int,re.findall(r"ready role=.* pid=(\d+)",log)))
    assert len(workers)==2
    _,old_exit=watch(stack,old)
    watched=[watch(stack,pid) for pid in workers]
    manual=None
    if mode=="race":
        (case/"b/start").symlink_to("../a/exit-a")
        finish("a")
        ready("b")
    elif mode.startswith("overlap"):
        (case/"b/start").touch()
        ready("b")
        if mode=="overlap-manual":
            manual=launch(stack,case,"manual",[gateway,*route],os.environ)
        finish("a")
        assert not old_exit.poll(0)
    else:
        # Leave the old owner in worker cleanup long enough to exercise this interval deterministically.
        fd=watched[0][0]
        signal.pidfd_send_signal(fd,signal.SIGSTOP)
        try:
            finish("a")
            wait(lambda:watched[1][1].poll(0))
            assert not old_exit.poll(0)
            if mode=="manual-first":
                manual=launch(stack,case,"manual",[gateway,*route],os.environ)
            (case/"b/start").touch()
            if mode=="auto-first":
                time.sleep(.15)
                manual=launch(stack,case,"manual",[gateway,*route],os.environ)
            time.sleep(.35)
        finally:
            signal.pidfd_send_signal(fd,signal.SIGCONT)
        ready("b")
        assert old_exit.poll(6000)
    if mode=="successor-first":
        manual=launch(stack,case,"manual",[gateway,*route],os.environ)
    logs=[(case/(label+".log")).read_text() for label in ("b","manual") if (case/(label+".log")).exists()]
    winners=re.findall(r"^owner pid=(\d+)", "\n".join(logs),re.M)
    if mode=="race":
        assert len(winners)<=1
    else:
        assert len(winners)==(0 if mode.startswith("overlap") else 1)
    new_processes=[watch(stack,int(pid)) for pid in winners+
                   re.findall(r"ready role=.* pid=(\d+)", "\n".join(logs))]
    if winners:
        assert len(new_processes)==3
    wait(lambda:sub.get_publisher_count()==1)
    send("b")
    finish("b")
    if manual and manual.poll() is None:
        # A losing launcher can still be returning its conflict diagnostic.
        wait(lambda:manual.poll() is not None or "ready role=destination" in (case/"manual.log").read_text())
    if manual and manual.poll() is None:
        assert sub.get_publisher_count()==1
        manual.send_signal(signal.SIGINT)
        assert manual.wait(timeout=8)==0
        winner="manual"
    else:
        if manual:
            assert "conflicting route" in (case/"manual.log").read_text()
        winner="automatic" if winners else "original"
    wait(lambda:sub.get_publisher_count()==0)
    assert old_exit.poll(6000) and all(p.poll(6000) for _,p in watched)
    assert all(p.poll(6000) for _,p in new_processes)
    assert received==[f"{k}-{n}" for k in apps for n in range(20)]
    return winner,len(received)


with (out/"results.csv").open("w",buffering=1) as f:
    writer=csv.writer(f,lineterminator="\n")
    writer.writerow(("trial","last_owner","received"))
    for i,mode in enumerate(sys.argv[2:]):
        case=out/f"{i}-{mode}"
        case.mkdir()
        with ExitStack() as stack:
            winner,count=run(mode,case,stack)
        writer.writerow((case.name,winner,count))
        print(case.name,winner,count,flush=True)
node.destroy_node()
rclpy.shutdown()
