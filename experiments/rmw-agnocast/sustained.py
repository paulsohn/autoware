"""Pace three real crop nodes and synchronizer into a live cluster/tracker and full-cloud bag."""
import csv
import os
import resource
import runpy
import signal
import sqlite3
import subprocess
import sys
import time
from pathlib import Path

import rclpy
from autoware_perception_msgs.msg import DetectedObjects, TrackedObjects
from rclpy.qos import qos_profile_sensor_data
from rclpy.serialization import deserialize_message
from sensor_msgs.msg import PointCloud2
from tier4_perception_msgs.msg import DetectedObjectsWithFeature

coverage = runpy.run_path(str(Path(__file__).with_name("route-perception.py")))
wait = coverage["wait"]
mode, target = sys.argv[1:]
out = Path(target).resolve()
if mode != "readback":
    out.mkdir()
procs = {}


def start(name, cmd):
    with (out/(name+".log")).open("x") as log:
        procs[name] = subprocess.Popen(cmd, stdout=log, stderr=log, start_new_session=True)


def stop(name):
    p=procs[name]
    if p.poll() is None:
        p.send_signal(signal.SIGINT)
    rc=p.wait(timeout=10)
    del procs[name]
    return rc


def run():
    if mode=="zenoh":
        start("router", ["/opt/ros/lyrical/lib/rmw_zenoh_cpp/rmw_zenohd"])
        time.sleep(.7)
    rclpy.init()
    node=rclpy.create_node("sustained_perception_driver")
    incoming,clusters,tracks=[],[],[]
    raw=node.create_subscription(PointCloud2,"/perception/cloud",incoming.append,qos_profile_sensor_data)
    pub=node.create_publisher(PointCloud2,"/input",qos_profile_sensor_data)
    sub=node.create_subscription(DetectedObjectsWithFeature,"/output_clusters",clusters.append,1)
    detection=node.create_publisher(DetectedObjects,"/tracker/input/detection01/objects",1)
    tracked=node.create_subscription(TrackedObjects,"/tracker/output/objects",tracks.append,1)
    start("tf",["/opt/ros/lyrical/lib/tf2_ros/static_transform_publisher","--frame-id","map","--child-frame-id","base_link"])
    start("cluster",["build/perception-replay/install/universe_detection_replay/lib/universe_detection_replay/replay_cluster",
          "--ros-args","-r","output:=/output_clusters","--params-file",
          "src/universe/autoware_universe/perception/autoware_euclidean_cluster/config/euclidean_cluster.param.yaml"])
    start("tracker",coverage["helpers"]["tracker_command"]("tracker"))
    start("bag",["ros2","bag","record","--topics","/perception/cloud","-s","sqlite3",
                 "--max-cache-size","0","--disable-keyboard-controls","-o",str(out/"bag")])
    start("graph",["ros2","launch","universe_sync_benchmark","three_lidar.launch.py",
                   "mode:="+mode,"fixture_directory:="+str(out)])
    wait(node,lambda: raw.get_publisher_count() and pub.get_subscription_count() and sub.get_publisher_count()
         and detection.get_subscription_count() and tracked.get_publisher_count())
    (out/"perception-ready").touch()
    cgroup=Path("/sys/fs/cgroup")/Path("/proc/self/cgroup").read_text().strip().split("::")[1].lstrip("/")
    ids={}
    stamps=[]
    start_cpu=time.process_time()
    with (out/"samples.csv").open("w") as f:
        writer=csv.writer(f,lineterminator="\n")
        writer.writerow(("seq","stamp_ns","cloud_ns","tracked_ns","tracked_targets","anon_bytes"))
        for seq in range(600):
            wait(node,lambda: incoming)
            cloud=incoming.pop(0)
            arrival=time.time_ns()
            stamp=cloud.header.stamp.sec*10**9+cloud.header.stamp.nanosec
            msg,sensors=coverage["project"](cloud,seq,False)
            assert sensors==([0,1] if seq%2 else [0,1,2])
            msg.header=cloud.header
            pub.publish(msg)
            wait(node,lambda: clusters)
            result,=clusters
            clusters.clear()
            assert result.header==msg.header
            det,targets=coverage["objects"](result)
            assert targets==([0] if seq%2 else [0,1])
            detection.publish(det)
            wait(node,lambda: tracks)
            output,=tracks
            tracks.clear()
            end=time.time_ns()
            assert output.header.stamp==cloud.header.stamp
            seen=set()
            for obj in output.objects:
                pos=obj.kinematics.pose_with_covariance.pose.position
                t=round(pos.x/8)
                assert t in (0,1) and abs(pos.x-(8*t+.15))<.3 and t not in seen
                seen.add(t)
                uid=bytes(obj.object_id.uuid)
                assert ids.setdefault(t,uid)==uid
            if seq>=4:
                assert seen=={0,1},(seq,seen)
            anon=next(int(line.split()[1]) for line in (cgroup/"memory.stat").read_text().splitlines() if line.startswith("anon ")) if seq%50==0 or seq==599 else ""
            writer.writerow((seq,stamp,arrival-stamp,end-stamp,len(seen),anon))
            stamps.append(stamp)
    (out/"perception-done").touch()
    assert procs["graph"].wait(timeout=15)==0
    assert stop("graph")==0 and stop("bag")==0
    for name in ("tracker","cluster","tf"):
        assert stop(name)==0
    node.destroy_node()
    rclpy.shutdown()
    if mode=="zenoh":
        stop("router")
    own=time.process_time()-start_cpu
    usage=resource.getrusage(resource.RUSAGE_CHILDREN)
    print(f"mode={mode} frames=600 steady_tracks=596/596 cpu_driver_stream_s={own:.3f} cpu_children_s={usage.ru_stime+usage.ru_utime:.3f} scope_peak_bytes={(cgroup/'memory.peak').read_text().strip()}",flush=True)
    verify_bag(stamps)


def verify_bag(stamps):
    by_stamp = {stamp: seq for seq, stamp in enumerate(stamps)}
    with sqlite3.connect('file:'+str(next((out/"bag").glob("*.db3")))+'?mode=ro',uri=True) as db:
        recorded=[]
        for raw, in db.execute("select data from messages join topics on topics.id=topic_id "
                               "where name='/perception/cloud' order by messages.id"):
            c=deserialize_message(raw,PointCloud2)
            stamp=c.header.stamp.sec*10**9+c.header.stamp.nanosec
            seq=by_stamp[stamp]
            assert c.width==16384*(2 if seq%2 else 3) and c.data[12]==seq%256
            recorded.append(seq)
        assert recorded==sorted(set(recorded))
    missing=sorted(set(range(600))-set(recorded))
    print(f"bag={len(recorded)}/600 source_stamps_and_sequences_verified missing={missing}",flush=True)
    if missing:
        raise RuntimeError("incomplete bag; retain this trial in the comparison")




try:
    if mode == "readback":
        with (out/"samples.csv").open() as f:
            verify_bag([int(r["stamp_ns"]) for r in csv.DictReader(f)])
    else:
        run()
finally:
    for name,p in reversed(list(procs.items())):
        if p.poll() is None:
            os.killpg(p.pid,signal.SIGINT)
            try:
                p.wait(timeout=8)
            except subprocess.TimeoutExpired:
                os.killpg(p.pid,signal.SIGKILL)
                p.wait()
