"""Configured two-island control. Run with access to the local Docker engine."""
import os
from pathlib import Path
import subprocess
import sys
import time
from contextlib import ExitStack

TOPIC = "/island_control/data"
if len(sys.argv) > 1 and sys.argv[1] in ("writer", "reader"):
    import rclpy
    from std_msgs.msg import Int32
    rclpy.init()
    node = rclpy.create_node("island_" + sys.argv[1])
    received = []
    writer = sys.argv[1] == "writer"
    endpoint = (node.create_publisher(Int32, TOPIC, 64) if writer else
                node.create_subscription(Int32, TOPIC, lambda msg: received.append(msg.data), 64))
    Path("/tmp/app-ready").touch()
    end = time.monotonic() + 45
    while not Path("/tmp/test-start").exists():
        assert time.monotonic() < end
        rclpy.spin_once(node, timeout_sec=.05)
    for i in range(30):
        count = endpoint.get_subscription_count() if writer else endpoint.get_publisher_count()
        assert count == 0 and not received, (count, received)
        if writer:
            endpoint.publish(Int32(data=-1))
        rclpy.spin_once(node, timeout_sec=.03)
    Path("/tmp/isolated").touch()
    while not (endpoint.get_subscription_count() if writer else endpoint.get_publisher_count()):
        assert time.monotonic() < end
        rclpy.spin_once(node, timeout_sec=.05)
    if writer:
        # The export subscription is created after the importer matches over the wire.
        for i in range(20):
            endpoint.publish(Int32(data=i))
            time.sleep(.05)
    else:
        while len(received) < 20:
            assert time.monotonic() < end, received
            rclpy.spin_once(node, timeout_sec=.05)
        assert received == list(range(20)), received
    node.destroy_node()
    rclpy.shutdown()
    sys.exit(0)

ws = Path.cwd()
out = Path(sys.argv[1])
out.mkdir(parents=True, exist_ok=False)
image = "local/autoware-migration:awf-buffer-lyrical-zenoh"
hook = "/ws/build/migration-guide/packaged-hook-check/libagnocast_heaphook.so"
setup = "source /opt/ros/lyrical/setup.bash\n" + "".join(
    f"source /ws/build/{overlay}/local_setup.bash\n" for overlay in
    ("agnocast-lyrical/install","standalone/o3-install","standalone/release-path/install",
     "standalone/poll-install","manual-gateway/install"))
def docker(*args):
    return subprocess.check_output(["docker","--context","default",*args],text=True).strip()

def trial(backend):
    with ExitStack() as stack:
        net = docker("network","create",f"island-control-{os.getpid()}")
        stack.callback(docker,"network","rm",net)
        islands = []
        for i in range(2):
            container = docker("run","-d","--pull=never","--network",net,"--ipc=private",
                "--memory=512m","--memory-swap=512m","--device=/dev/agnocast",
                "-v",f"{ws}:/ws:ro","-v",f"{Path('build/agnocast-lyrical/universe-root').read_text().strip()}/opt/ros/lyrical:/opt/ros/lyrical:ro",
                "--entrypoint=sleep",image,"180")
            stack.callback(docker,"rm","-f",container)
            islands.append(container)
        ips = [docker("inspect","-f","{{range .NetworkSettings.Networks}}{{.IPAddress}}{{end}}",c) for c in islands]
        namespaces = [docker("exec",c,"readlink","/proc/self/ns/ipc") for c in islands]
        assert len(set(ips)) == len(set(namespaces)) == 2
        print(backend,ips,namespaces,flush=True)
        def run(i,role,command,native=False):
            log=stack.enter_context((out/f"{backend}-{role}.log").open("w"))
            xml=f'<CycloneDDS><Domain><General><Interfaces><NetworkInterface name="eth0"/></Interfaces><AllowMulticast>false</AllowMulticast></General><Discovery><ParticipantIndex>auto</ParticipantIndex><Peers><Peer Address="{ips[1-i]}"/></Peers></Discovery></Domain></CycloneDDS>'
            args=["docker","--context","default","exec","-e","ROS_DOMAIN_ID=175","-e","HOME=/tmp",
                  "-e",f"CYCLONEDDS_URI={xml}","-e",f'ZENOH_CONFIG_OVERRIDE=connect/endpoints=["tcp/{ips[0]}:7447"];listen/endpoints=["tcp/{ips[i]}:0"]',
                  islands[i],"bash","-ec",setup+("export LD_PRELOAD="+hook+" RMW_IMPLEMENTATION=rmw_agnocast\n" if native else "")+"exec "+command]
            return subprocess.Popen(args,stdout=log,stderr=subprocess.STDOUT)
        apps=[run(i,role,"python3 /ws/experiments/rmw-agnocast/islands/control.py "+role,True)
              for i,role in enumerate(("writer","reader"))]
        def markers(path):
            until=time.monotonic()+12
            for c in islands:
                while subprocess.run(["docker","exec",c,"test","-f",path]).returncode:
                    assert all(p.poll() is None for p in apps) and time.monotonic()<until, path
                    time.sleep(.1)
        markers("/tmp/app-ready")
        for c in islands:
            docker("exec",c,"touch","/tmp/test-start")
        markers("/tmp/isolated")
        if backend=="zenoh":
            run(0,"router","env -u ZENOH_CONFIG_OVERRIDE /opt/ros/lyrical/lib/rmw_zenoh_cpp/rmw_zenohd")
            time.sleep(.5)
        def counters():
            return [int(docker("exec",c,"cat",f"/sys/class/net/eth0/statistics/{key}_bytes"))
                    for c,key in zip(islands,("tx","rx"))]
        before=counters()
        for i,direction in ((1,"import"),(0,"export")):
            run(i,direction,f"/ws/build/manual-gateway/install/agnocast_cyclone_gateway/lib/agnocast_cyclone_gateway/topic_gateway {direction} std_msgs/msg/Int32 {TOPIC} {TOPIC} --network-rmw rmw_{'cyclonedds' if backend=='cyclone' else 'zenoh'}_cpp --heaphook {hook}")
        for p in apps:
            assert p.wait(timeout=50)==0, "see endpoint logs"
        delta=[a-b for a,b in zip(counters(),before)]
        assert min(delta)>0
        print(backend,"20/20; eth0 tx/rx deltas including discovery",delta,flush=True)
for backend in sys.argv[2:] or ("cyclone","zenoh"):
    trial(backend)
