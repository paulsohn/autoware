"""Matched source coverage replay through the Universe cluster and multi-object tracker."""
import copy
import runpy
import signal
import sqlite3
import struct
import subprocess
import sys
import time
from pathlib import Path

import rclpy
from geometry_msgs.msg import Point32
from rclpy.qos import qos_profile_sensor_data
from rclpy.serialization import deserialize_message
from sensor_msgs.msg import PointCloud2, PointField
from tier4_perception_msgs.msg import DetectedObjectsWithFeature
from autoware_perception_msgs.msg import DetectedObjects, TrackedObjects, Shape

helpers = runpy.run_path(str(Path(__file__).with_name("replay-tracks.py")))
wait = helpers["wait"]


def captures(path):
    db, = (path/"bag").glob("*.db3")
    with sqlite3.connect(f"file:{db}?mode=ro", uri=True) as conn:
        clouds = [deserialize_message(row[0], PointCloud2) for row in conn.execute(
            "SELECT data FROM messages JOIN topics ON topics.id=topic_id "
            "WHERE name='/dds/synchronized' ORDER BY messages.id")]
    assert len(clouds) == 24
    return clouds


def project(cloud, seq, omit):
    # Map the first 24 channels of each real sensor segment into the rectangular test landmarks.
    payload = bytes(cloud.data)
    landmarks = []
    sensors = []
    for segment in range(0, cloud.width, 16384):
        sensor = payload[segment*16+13]
        if omit and sensor == 2:
            continue
        sensors.append(sensor)
        for i in range(24 if sensor == 2 else 12):
            x, y, z, intensity, kind, channel = struct.unpack_from("<fffBBH", payload, 16*(segment+i))
            assert (x,y,z,intensity,kind,channel) == (
                sensor*8192+i+(4 if sensor else 0), seq, sensor, seq, sensor, sensor*8192+i)
            target = i // 12
            landmarks.append((8.*target+.1*(i%4), .1*((i%12)//4), 1.))
    msg = PointCloud2(height=1,width=len(landmarks),point_step=12,row_step=12*len(landmarks),is_dense=True)
    msg.fields = [PointField(name=k,offset=4*i,datatype=PointField.FLOAT32,count=1) for i,k in enumerate("xyz")]
    msg.data = b"".join(struct.pack("<fff", *p) for p in sorted(landmarks))
    return msg, sorted(sensors)


def objects(clustered):
    msg = DetectedObjects(header=clustered.header)
    targets = []
    for feature in clustered.feature_objects:
        obj = feature.object
        pos = obj.kinematics.pose_with_covariance.pose.position
        target = round(pos.x/8)
        assert target in (0,1) and abs(pos.x-(target*8+.15)) < .001 and abs(pos.y-.1)<.001
        obj.shape.type = Shape.POLYGON
        obj.shape.dimensions.x, obj.shape.dimensions.y, obj.shape.dimensions.z = .3,.2,.1
        obj.shape.footprint.points = [Point32(x=x,y=y) for x,y in ((-.15,-.1),(.15,-.1),(.15,.1),(-.15,.1))]
        obj.kinematics.pose_with_covariance.pose.orientation.w = 1.
        msg.objects.append(obj)
        targets.append(target)
    return msg, sorted(targets)


def replay(node, conditions, headers, cluster_pub, clusters):
    processes, endpoints, rows, identities = {}, {}, {}, {}
    try:
        for name in conditions:
            tracker = "coverage_" + name
            replies = []
            pub = node.create_publisher(DetectedObjects, f"/{tracker}/input/detection01/objects", 1)
            sub = node.create_subscription(TrackedObjects, f"/{tracker}/output/objects", replies.append, 1)
            endpoints[name] = pub, sub, replies
            processes[name] = subprocess.Popen(helpers["tracker_command"](tracker))
            wait(node, lambda: pub.get_subscription_count() == sub.get_publisher_count() == 1)
            rows[name], identities[name] = [], {}
        stamps = [h.stamp.sec*10**9+h.stamp.nanosec for h in headers]
        shift = time.time_ns()+200_000_000-stamps[0]
        for seq, stamp in enumerate(stamps):
            due = stamp+shift
            time.sleep(max(0,(due-time.time_ns())/1e9))
            for name,frames in conditions.items():
                msg,sensors = frames[seq]
                msg.header = copy.deepcopy(headers[seq])
                msg.header.stamp.sec,msg.header.stamp.nanosec = divmod(due,10**9)
                cluster_pub.publish(msg)
                wait(node,lambda: clusters)
                result, = clusters
                clusters.clear()
                assert result.header == msg.header
                det,targets = objects(result)
                assert targets == ([0,1] if 2 in sensors else [0])
                pub,sub,replies=endpoints[name]
                pub.publish(det)
                wait(node,lambda: replies)
                tracked, = replies
                replies.clear()
                assert tracked.header.stamp == msg.header.stamp and tracked.header.frame_id=="map"
                visible=[]
                for obj in tracked.objects:
                    pos=obj.kinematics.pose_with_covariance.pose.position
                    target=round(pos.x/8)
                    assert target in (0,1) and abs(pos.x-(8*target+.15))<.3 and target not in visible
                    uid=bytes(obj.object_id.uuid)
                    assert identities[name].setdefault(target,uid)==uid, (name,seq,target)
                    visible.append(target)
                rows[name].append((targets,sorted(visible)))
                print(f"frame {name} {seq} stamp={due} sensors={sensors} detections={targets} tracks={sorted(visible)}",flush=True)
        assert all(len(set(ids.values()))==2 for ids in identities.values())
        return rows
    finally:
        for proc in processes.values():
            proc.send_signal(signal.SIGINT)
        for proc in processes.values():
            proc.wait(timeout=8)


def main():
    root=Path(sys.argv[1])
    good=captures(root/"healthy")
    fault=captures(root/"ingress")
    headers=[copy.deepcopy(c.header) for c in fault]
    conditions=dict(healthy=[project(c,i,False) for i,c in enumerate(good)],
                    omission=[project(c,i,i==8) for i,c in enumerate(good)],
                    ingress=[project(c,i,False) for i,c in enumerate(fault)])
    assert conditions["omission"]==conditions["ingress"]
    rclpy.init()
    node=rclpy.create_node("coverage_replay")
    pub=node.create_publisher(PointCloud2,"/input",qos_profile_sensor_data)
    clusters=[]
    sub=node.create_subscription(DetectedObjectsWithFeature,"/output",clusters.append,1)
    try:
        wait(node,lambda: pub.get_subscription_count()==1 and sub.get_publisher_count()==1)
        rows=replay(node,conditions,headers,pub,clusters)
        assert rows["omission"]==rows["ingress"]
        assert [i for i in range(24) if rows["healthy"][i][0]!=rows["ingress"][i][0]]==[8]
        for k,trace in rows.items():
            print(k,"first_tracks", [next(i for i,(_,tr) in enumerate(trace) if target in tr) for target in (0,1)],
                  "top_track_absent_after_group_6",[i for i,(_,tr) in enumerate(trace) if i>=6 and 1 not in tr])
        print("top observation recovery ms",((headers[10].stamp.sec-headers[8].stamp.sec)*10**9+
              headers[10].stamp.nanosec-headers[8].stamp.nanosec)/1e6)
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__=="__main__":
    main()
