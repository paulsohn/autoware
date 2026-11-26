"""Compare two real recorder bags across a gateway handoff on a shared laptop clock."""
import csv
import itertools
import json
import os
from pathlib import Path
import re
import select
import signal
import sqlite3
import subprocess
import sys
import time
from contextlib import ExitStack

import rclpy
from rclpy.serialization import deserialize_message
from std_msgs.msg import String

out = Path(sys.argv[1]).resolve()
source = "/continuity/input"
destination = "/continuity/output"


def wait(test, seconds=12):
    end = time.monotonic()+seconds
    while not test():
        if time.monotonic()>end:
            raise TimeoutError(test)
        time.sleep(.01)


if sys.argv[2] in ("lease-a","lease-b","publisher"):
    role=sys.argv[2]
    (out/(role+"-waiting")).touch()
    wait((out/(role+"-start")).exists)
    rclpy.init()
    node=rclpy.create_node(role.replace("-","_"))
    if role=="publisher":
        pub=node.create_publisher(String,source,10)
        wait(lambda:pub.get_subscription_count()==2)
        start=time.monotonic()
        for i in range(100):
            time.sleep(max(0,start+i*.05-time.monotonic()))
            pub.publish(String(data=f"{i},{time.time_ns()}"))
            if i==29:
                (out/"transition").touch()
    else:
        (out/(role+"-ready")).touch()
        wait((out/(role+"-exit")).exists,35)
    node.destroy_node()
    rclpy.shutdown()
    sys.exit(0)


def read_bag(path,topic):
    with sqlite3.connect(f"file:{next(path.glob('*.db3'))}?mode=ro",uri=True) as db:
        return [(*map(int,deserialize_message(data,String).data.split(",")),stamp)
                for data,stamp in db.execute("select data,timestamp from messages join topics on topic_id=topics.id where name=? order by messages.id",(topic,))]


def trial(case,mode,stack):
    gateway=os.environ["AGNOCAST_GATEWAY_EXECUTABLE"]
    hook=os.environ["TEST_HEAPHOOK"]
    route=["export","std_msgs/msg/String",source,destination,"--heaphook",hook]
    config=case/"route.json"
    config.write_text(json.dumps(route))
    ordinary=os.environ.copy()
    ordinary.pop("RMW_AGNOCAST_GATEWAY_CONFIG",None)
    native=dict(ordinary,RMW_IMPLEMENTATION="rmw_agnocast",LD_PRELOAD=hook)
    lease=dict(native,RMW_AGNOCAST_GATEWAY_CONFIG=str(config))
    def start(name,cmd,env):
        log=stack.enter_context((case/(name+".log")).open("w"))
        proc=subprocess.Popen(cmd,env=env,stdout=log,stderr=log)
        def cleanup():
            if proc.poll() is None:
                proc.terminate()
                proc.wait(timeout=10)
        stack.callback(cleanup)
        return proc
    def app(name,env):
        return start(name,[sys.executable,__file__,str(case),name],env)
    def ready(k,proc):
        wait(lambda:(case/(k+"-ready")).exists() or proc.poll() is not None)
        assert proc.poll() is None,k
    a=app("lease-a",lease)
    (case/"lease-a-start").touch()
    ready("lease-a",a)
    owner=int(re.search(r"owner pid=(\d+)",(case/"lease-a.log").read_text())[1])
    owner_fd=os.pidfd_open(owner)
    stack.callback(os.close,owner_fd)
    exited=select.poll()
    exited.register(owner_fd,select.POLLIN)
    if mode!="stable":
        b=app("lease-b",lease)
        wait((case/"lease-b-waiting").exists)
    recorders=[]
    for name,topic,env in (("native",source,native),("cyclone",destination,ordinary)):
        rec=start(name,["ros2","bag","record","--topics",topic,"-s","sqlite3","--max-cache-size","0",
                        "--disable-keyboard-controls","-o",str(case/name)],env)
        recorders.append(rec)
    pub=app("publisher",native)
    (case/"publisher-start").touch()
    wait((case/"transition").exists)
    event=time.time_ns()
    manual=None
    winner="original"
    if mode=="overlap":
        (case/"lease-b-start").touch()
        ready("lease-b",b)
    if mode!="stable":
        (case/"lease-a-exit").touch()
        assert a.wait(timeout=8)==0
        if mode=="overlap":
            assert not exited.poll(0)
        else:
            assert exited.poll(8000)
            if mode=="manual-lead":
                manual=start("manual",[gateway,*route],ordinary)
            (case/"lease-b-start").touch()
            if mode=="auto-lead":
                time.sleep(.12)
                manual=start("manual",[gateway,*route],ordinary)
            ready("lease-b",b)
            winners=[k for k in ("lease-b","manual") if (case/(k+".log")).exists()
                     and "ready role=destination" in (case/(k+".log")).read_text()]
            assert len(winners)==1,winners
            winner="manual" if winners==["manual"] else "automatic"
    assert pub.wait(timeout=10)==0
    time.sleep(.35)
    for rec in recorders:
        rec.send_signal(signal.SIGINT)
        assert rec.wait(timeout=10)==0
    k="lease-a" if mode=="stable" else "lease-b"
    (case/(k+"-exit")).touch()
    assert (a if mode=="stable" else b).wait(timeout=10)==0
    if manual:
        if winner=="manual":
            manual.send_signal(signal.SIGINT)
            assert manual.wait(timeout=8)==0
        else:
            assert manual.wait(timeout=8)!=0
            assert "conflicting route" in (case/"manual.log").read_text()
    for name in ("lease-a","lease-b","manual"):
        log=case/(name+".log")
        if log.exists():
            for pid in re.findall(r"^owner pid=(\d+)",log.read_text(),re.M):
                def gone():
                    try:
                        return Path(f"/proc/{pid}/stat").read_text().split(") ")[1].startswith("Z")
                    except FileNotFoundError:
                        return True
                wait(gone)
    local=read_bag(case/"native",source)
    remote=read_bag(case/"cyclone",destination)
    assert [i for i,_,_ in local]==list(range(100))
    ids=[i for i,_,_ in remote]
    assert ids==sorted(set(ids)) and ids[0]==0 and ids[-1]==99
    assert all(s==local[i][1] for i,s,_ in remote)
    missing=sorted(set(range(100))-set(ids))
    ranges=["%d-%d"%(v[0],v[-1]) for _,g in itertools.groupby(enumerate(missing),lambda e:e[1]-e[0])
            if (v:=[n for _,n in g])]
    if mode in ("stable","overlap"):
        assert not missing,missing
    return (winner,len(local),len(remote),";".join(ranges),len(ids)-len(set(ids)),
            round(max(y[2]-x[2] for x,y in zip(remote,remote[1:]))/1e6,2),
            round((next(t for _,s,t in remote if s>=event)-event)/1e6,2))


out.mkdir()
with (out/"results.csv").open("w",buffering=1) as f:
    w=csv.writer(f,lineterminator="\n")
    w.writerow(("trial","owner","local","cyclone","missing_ranges","duplicates","max_receive_gap_ms","first_fresh_ms"))
    for i,mode in enumerate(sys.argv[2:]):
        assert mode in ("stable","overlap","replace","manual-lead","auto-lead")
        case=out/f"{i}-{mode}"
        case.mkdir()
        with ExitStack() as stack:
            row=trial(case,mode,stack)
        w.writerow((case.name,*row))
        print(case.name,*row,flush=True)
