"""Use the installed example for String, then qualify serialized clouds in both directions."""
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import time
from contextlib import ExitStack

out=Path(sys.argv[1]).resolve()
def wait(test):
    end=time.monotonic()+12
    while not test():
        if time.monotonic()>end:
            raise TimeoutError(test)
        time.sleep(.01)

if len(sys.argv)>2:
    import rclpy
    from rclpy.utilities import get_rmw_implementation_identifier
    from sensor_msgs.msg import PointCloud2, PointField
    from std_msgs.msg import String
    role=sys.argv[2]
    route=json.loads((out/"witness.json").read_text())
    cloud=route["type"]=="sensor_msgs/msg/PointCloud2"
    cls=PointCloud2 if cloud else String
    def sample(i):
        if not cloud:
            return String(data=f"value-{i}")
        msg=PointCloud2()
        msg.header.frame_id=f"lidar-{i}"
        msg.header.stamp.sec=i
        msg.height=1
        msg.width=i*1024
        msg.fields=[PointField(name="x",offset=0,datatype=PointField.FLOAT32,count=1)]
        msg.point_step=4
        msg.row_step=msg.width*4
        msg.data=bytes([i])*msg.row_step
        return msg
    rclpy.init()
    assert get_rmw_implementation_identifier()==os.environ["RMW_IMPLEMENTATION"]
    node=rclpy.create_node("gateway_test_"+role)
    if role=="source":
        pub=node.create_publisher(cls,route["source"],10)
        (out/"source-ready").touch()
        wait(pub.get_subscription_count)
        for i in range(20):
            pub.publish(sample(i))
            time.sleep(.05)
    else:
        seen=[]
        def receive(msg):
            assert msg==sample(len(seen))
            seen.append(len(seen))
        sub=node.create_subscription(cls,route["destination"],receive,10)
        (out/"sink-ready").touch()
        end=time.monotonic()+15
        while len(seen)<20 and time.monotonic()<end:
            rclpy.spin_once(node,timeout_sec=.05)
        assert seen==list(range(20)),seen
    if os.environ["RMW_IMPLEMENTATION"]=="rmw_agnocast":
        wait((out/"finish").exists)
    node.destroy_node()
    rclpy.shutdown()
    sys.exit(0)

out.mkdir()
exe=Path(os.environ["AGNOCAST_GATEWAY_EXECUTABLE"])
examples=exe.parents[2]/"share/agnocast_cyclone_gateway/config"
def start(stack,name,cmd,env):
    log=stack.enter_context(name.open("w"))
    proc=subprocess.Popen(cmd,env=env,stdout=log,stderr=log)
    def cleanup():
        if proc.poll() is None:
            proc.terminate()
            proc.wait(timeout=10)
    stack.callback(cleanup)
    return proc

for backend in ("cyclonedds","zenoh"):
    with ExitStack() as outer:
        if backend=="zenoh":
            router=start(outer,out/"router.log",["/opt/ros/lyrical/lib/rmw_zenoh_cpp/rmw_zenohd"],os.environ)
            time.sleep(.5)
            assert router.poll() is None
        for direction,kind in (("export","String"),("export","PointCloud2"),("import","PointCloud2")):
            case=out/f"{backend}-{direction}-{kind}"
            case.mkdir()
            args=json.loads((examples/(backend+".json")).read_text())
            entry=args["exports"][0]
            entry["type"]="sensor_msgs/msg/"+kind if kind=="PointCloud2" else "std_msgs/msg/String"
            if direction=="import":
                entry["topic"],entry["remap"]=entry["remap"],entry["topic"]
                del args["exports"]
                args["imports"]=[entry]
            args["heaphook"]=os.environ["TEST_HEAPHOOK"]
            (case/"witness.json").write_text(json.dumps(dict(type=entry["type"],source=entry["topic"],destination=entry["remap"])))
            config=case/"route.json"
            config.write_text(json.dumps(args))
            network=args["network_rmw"]
            peer=dict(os.environ,RMW_IMPLEMENTATION=network)
            peer.pop("RMW_AGNOCAST_GATEWAY_CONFIG",None)
            peer.pop("LD_PRELOAD",None)
            native=dict(peer,RMW_IMPLEMENTATION="rmw_agnocast",RMW_AGNOCAST_GATEWAY_CONFIG=str(config),LD_PRELOAD=args["heaphook"])
            local,remote=("source","sink") if direction=="export" else ("sink","source")
            with ExitStack() as stack:
                def endpoint(role,env):
                    return start(stack,case/(role+".log"),[sys.executable,__file__,str(case),role],env)
                app=endpoint(local,native)
                wait(lambda:(case/(local+"-ready")).exists() or app.poll() is not None)
                assert app.poll() is None
                other=endpoint(remote,peer)
                assert other.wait(timeout=20)==0
                if direction=="export":
                    wait(lambda:"source idle" in (case/(local+".log")).read_text())
                (case/"finish").touch()
                assert app.wait(timeout=15)==0
                wait(lambda:"stats role=destination samples=20" in (case/(local+".log")).read_text())
            print(case.name,"20/20",flush=True)
