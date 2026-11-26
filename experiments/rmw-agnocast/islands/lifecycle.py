"""Two readers, an interface-preserving partition and a receiver-island crash."""
import json
from pathlib import Path
import sys
import time

TOPIC = "/island_control/lifecycle"

def endpoint(name):
    import rclpy
    from std_msgs.msg import Int32
    rclpy.init()
    node = rclpy.create_node("island_" + name)
    received = []
    writer = name in ("source","pinned_writer")
    topic = "/island_control/pinned" if name.startswith("pinned_") else TOPIC
    entity = (node.create_publisher(Int32, topic, 32) if writer else
              node.create_subscription(Int32, topic, lambda msg: received.append(msg.data), 32))
    root = Path("/tmp") / name
    root.mkdir()
    previous = None
    while not (root/"exit").exists():
        rclpy.spin_once(node,timeout_sec=.02)
        count = entity.get_subscription_count() if writer else entity.get_publisher_count()
        state = (count,len(received))
        if state != previous:
            (root/"new").write_text(json.dumps([count,received]))
            (root/"new").replace(root/"state")
            previous = state
        if writer and (root/"send").exists():
            first = int((root/"send").read_text())
            (root/"send").unlink()
            for seq in range(first,first+10):
                entity.publish(Int32(data=seq))
                time.sleep(.04)
    node.destroy_node()
    rclpy.shutdown()

def exercise(islands,backend,out,run,docker,start_gateway):
    import subprocess
    sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"network-peer"))
    from peer_link import link
    spec = "python3 /ws/experiments/rmw-agnocast/islands/lifecycle.py "
    src,dst = islands
    def island(name):
        return int(name not in ("source","pinned_writer"))
    def start(name):
        return run(island(name),name,spec+name,True)
    def wait(test,label,seconds=30):
        end=time.monotonic()+seconds
        while not test():
            if time.monotonic()>end:
                raise TimeoutError(label)
            time.sleep(.12)
    def state(container,name):
        data=docker("exec",container,"sh","-c",f"cat /tmp/{name}/state 2>/dev/null || true")
        return json.loads(data) if data else [None,[]]
    def logcount(role):
        return (out/f"{backend}-{role}.log").read_text().count("discovered "+("export" if role=="export" else "import")+" "+TOPIC+" ")
    def matched(name,n):
        c=islands[island(name)]
        wait(lambda:state(c,name)[0]==n,f"{name} match {n}")
    expected={}
    def batch(first,*names,writer="source"):
        matched(writer,1)
        for name in names:
            matched(name,1)
        docker("exec",src,"sh","-ec",f"echo {first} >/tmp/{writer}/next; mv /tmp/{writer}/next /tmp/{writer}/send")
        for name in names:
            expected.setdefault(name,[]).extend(range(first,first+10))
            wait(lambda:state(dst,name)[1]==expected[name],f"{name} sequence after {first}")
    def stop(name,process):
        docker("exec",dst,"touch","/tmp/"+name+"/exit")
        assert process.wait(timeout=8)==0
    def retired(role):
        # One long-lived supervisor per island; record child teardown for this topic
        # by inspecting the native writer match AND the route process itself.
        c=src if role=="export" else dst
        # Avoid the inspection command itself by testing the Python route supervisor argv.
        probe="""import glob,sys
topic=sys.argv[1].encode()
for path in glob.glob('/proc/[0-9]*/cmdline'):
 try: args=open(path,'rb').read().split(b'\\0')
 except FileNotFoundError: continue
 if len(args)>5 and args[1].endswith(b'/topic_gateway') and args[2] in (b'import',b'export') and topic in args[3:]: sys.exit(1)
"""
        wait(lambda:subprocess.run(["docker","exec",c,"python3","-c",probe,TOPIC]).returncode==0,role+" retirement")
    start("pinned_writer")
    start("pinned_first")
    batch(100,"pinned_first",writer="pinned_writer")
    inodes = [docker("exec", c, "stat", "-Lc", "%i", "/proc/self/ns/ipc") for c in islands]
    rmw = "rmw_cyclonedds_cpp" if backend == "cyclone" else "rmw_zenoh_cpp"
    probe = "python3 /ws/experiments/rmw-agnocast/islands/observe-snapshot.py"
    observer = run(0, "wire", " ".join([probe, *inodes]), environment=("-e", "RMW_IMPLEMENTATION="+rmw))
    assert observer.wait(timeout=15) == 0, "see wire observer log"
    # Join the running discovery owner from an independent native RMW context.
    exe="/ws/build/manual-gateway/install/agnocast_cyclone_gateway/lib/agnocast_cyclone_gateway/topic_gateway"
    joiner=run(0,"joiner","python3 -c 'import rclpy; rclpy.init(); rclpy.shutdown()'",True,
               ("-e","RMW_AGNOCAST_GATEWAY_CONFIG=/tmp/gateway.json","-e","AGNOCAST_GATEWAY_EXECUTABLE="+exe))
    assert joiner.wait(timeout=15)==0
    assert "owner pid=" not in (out/f"{backend}-joiner.log").read_text()
    start("source")
    first=start("first")
    second=start("second")
    batch(0,"first","second")
    stop("first",first)
    batch(10,"second")
    assert logcount("export")==logcount("import")==1
    stop("second",second)
    matched("source",0)
    retired("export")
    retired("import")
    batch(110,"pinned_first",writer="pinned_writer")
    third=start("third")
    batch(20,"third")
    down=time.monotonic()
    index=link(dst,"down")
    try:
        retired("export")
        retired("import")
        matched("source",0)
        matched("third",0)
    finally:
        assert link(dst,"up")==index
    expired=time.monotonic()-down
    batch(30,"third")
    batch(120,"pinned_first",writer="pinned_writer")
    assert logcount("export")==logcount("import")==3
    address=lambda:docker("inspect","-f","{{range .NetworkSettings.Networks}}{{.IPAddress}}{{end}}",dst)
    ip=address()
    docker("kill",dst)
    matched("source",0)
    retired("export")
    docker("start",dst)
    assert address()==ip
    start_gateway(1,"recovered")
    start("pinned_last")
    last=start("last")
    batch(40,"last")
    assert logcount("export")==4 and logcount("recovered")==1
    batch(130,"pinned_last",writer="pinned_writer")
    for role in ("import","export","recovered"):
        assert " /island_control/pinned std_msgs" not in "".join(line for line in (out/f"{backend}-{role}.log").read_text().splitlines() if line.startswith("discovered "))
    stop("last",last)
    retired("export")
    docker("exec",src,"touch","/tmp/source/exit")
    result=dict(received=expected,export_generations=4,partition_down_through_restore_s=expired)
    (out/(backend+"-lifecycle.json")).write_text(json.dumps(result,indent=2))
    print(backend,"lifecycle passed",flush=True)

if __name__=="__main__":
    endpoint(sys.argv[1])
