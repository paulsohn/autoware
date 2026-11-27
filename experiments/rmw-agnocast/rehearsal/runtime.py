"""Run ordinary ROS binaries, native recording and stock remote readback in clean containers."""
from contextlib import ExitStack
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess as sp
import sys
import time

if not __debug__:
    raise RuntimeError("Runtime verification requires Python assertions")
workspace = Path(sys.argv[1]).resolve()
evidence = Path(sys.argv[2]).resolve()
evidence.mkdir(parents=True, exist_ok=False)
prefix = "/ws/" + str(workspace.relative_to(Path.cwd()))
hook = str(Path(os.environ["REHEARSAL_HOOK"]).resolve())
ros = Path(os.environ["LYRICAL_ROOT"]) / "opt/ros/lyrical"
image = os.environ.get("REHEARSAL_IMAGE","local/autoware-migration:awf-buffer-lyrical-zenoh")
native, cyclone, zenoh = "rmw_agnocast", "rmw_cyclonedds_cpp", "rmw_zenoh_cpp"
topic = "/rehearsal/sequence"
outside = "/recorded/sequence"
packages = dict(pub=("examples_rclcpp_minimal_publisher","publisher_member_function"),
                sub=("examples_rclcpp_minimal_subscriber","subscriber_member_function"))
results = []


def docker(*args):
    return sp.check_output(["docker","--context","default",*args], text=True).strip()


def until(probe, label, processes=(), timeout=25):
    deadline = time.monotonic()+timeout
    while time.monotonic() < deadline:
        value = probe()
        if value:
            return value
        for p in processes:
            assert p.poll() is None, (label, p.returncode)
        time.sleep(.1)
    raise TimeoutError(label)


with ExitStack() as cleanup:
    net = docker("network","create","--internal",f"migration-{os.getpid()}")
    cleanup.callback(docker,"network","rm",net)
    containers=[]
    for application in (True,False):
        extra = ["--device=/dev/agnocast","-v",f"{workspace}/install:{prefix}/install:ro",
                 "-v",f"{hook}:/runtime/libagnocast_heaphook.so:ro"] if application else []
        c = docker("run","-d","--pull=never","--network",net,"--ipc=private","--memory=640m",
                   "--memory-swap=640m","-v",f"{ros}:/opt/ros/lyrical:ro",*extra,
                   "--entrypoint=sleep",image,"infinity")
        cleanup.callback(docker,"rm","-f",c)
        containers.append(c)
    app,peer = containers
    docker("exec",peer,"test","!","-e","/dev/agnocast")
    ips = [docker("inspect","-f","{{range .NetworkSettings.Networks}}{{.IPAddress}}{{end}}",c) for c in containers]
    metadata = {
        "middleware_revision": (workspace / "middleware.rev").read_text().strip(),
        "image_id": docker("image", "inspect", "-f", "{{.Id}}", image),
        "kernel_module": Path("/sys/module/agnocast/version").read_text().strip(),
        "hook_sha256": hashlib.sha256(Path(hook).read_bytes()).hexdigest(),
        "containers": containers, "addresses": ips,
    }
    (evidence / "fixture.json").write_text(json.dumps(metadata, indent=2))
    hashes = {}
    for role,(pkg,exe) in packages.items():
        installed = workspace/"install"/pkg/"lib"/pkg/exe
        hashes[role] = hashlib.sha256(installed.read_bytes()).hexdigest()

    def command(c, rmw, args, extra=()):
        xml = ('<CycloneDDS><Domain><General><Interfaces><NetworkInterface name="eth0"/></Interfaces>'
               '<AllowMulticast>false</AllowMulticast></General><Discovery><ParticipantIndex>auto</ParticipantIndex>'
               + "<Peers>"+ "".join(f'<Peer Address="{ip}"/>' for ip in ips)
               + "</Peers></Discovery></Domain></CycloneDDS>")
        setup = "source /opt/ros/lyrical/setup.bash\n"
        if c==app:
            setup += "source " + shlex.quote(prefix+"/install/local_setup.bash") + "\n"
        setup += 'export RMW_IMPLEMENTATION="$1"\nshift\n'
        return ["docker","--context","default","exec","-e","ROS_DOMAIN_ID=173","-e","RCUTILS_COLORIZED_OUTPUT=0",
                "-e","CYCLONEDDS_URI="+xml,"-e",f'ZENOH_CONFIG_OVERRIDE=connect/endpoints=["tcp/{ips[1]}:7447"];listen/endpoints=["tcp/0.0.0.0:0"]',
                *extra,c,"bash","-ec",setup+'exec "$@"',"bash",rmw,*args]

    peer_packages = sp.check_output(command(peer, cyclone, ["ros2", "pkg", "list"]), text=True)
    assert "rmw_cyclonedds_cpp" in peer_packages.splitlines()
    assert "rmw_zenoh_cpp" in peer_packages.splitlines()
    assert "rmw_agnocast" not in peer_packages.splitlines()
    assert "agnocast_cyclone_gateway" not in peer_packages.splitlines()

    def run_case(name, transport, network=None, automatic=False):
        path=evidence/name
        path.mkdir()
        procs={}
        def start(label,c,rmw,args,extra=()):
            pidfile="/tmp/"+label+".pid"
            # ros2 run owns a child process; capture the child for /proc maps and clean signals.
            record_pid = f'echo $$ > {pidfile}\n'
            if rmw == native:
                record_pid += 'export LD_PRELOAD=/runtime/libagnocast_heaphook.so\n'
            capture = 'sh -c ' + shlex.quote(record_pid + 'exec "$@"') + ' sh'
            if label in packages:
                # Run the exact developer CLI. Read the application PID after its first delivery.
                if rmw == native:
                    args = ["env", "LD_PRELOAD=/runtime/libagnocast_heaphook.so", *args]
            elif args[:2]==["ros2","run"]:
                args=args[:2]+["--prefix",capture]+args[2:]
            else:
                args=["sh","-ec",record_pid+'exec "$@"',"sh",*args]
            with (path/(label+".log")).open("w") as log:
                p=sp.Popen(command(c,rmw,args,extra),stdout=log,stderr=sp.STDOUT)
            procs[label]=(c,p)
            return p
        def stop(label):
            c,p=procs[label]
            pid=docker("exec",c,"cat","/tmp/"+label+".pid")
            docker("exec",c,"kill","-INT",pid)
            rc=p.wait(timeout=15)
            assert rc in (0,130,-2), (label,rc)
        def log(label):
            return (path/(label+".log")).read_text()
        def sequences():
            return list(map(int,re.findall(r"I heard: 'Hello, world! (\d+)'",log("sub"))))
        def tick():
            return until(lambda:sequences(), "listener payload",[procs["sub"][1],procs["pub"][1]])[-1]
        def window():
            first=tick()+2
            until(lambda:sequences() and sequences()[-1]>=first+4,"five ordered samples")
            return list(range(first,first+5))
        def bridge(label):
            return start(label,app,network,["ros2","run","agnocast_cyclone_gateway","topic_gateway",
                                           "--config","/tmp/migration.json"])
        def bag(label,c,rmw,record_topic):
            return start(label,c,rmw,["ros2","bag","record","--topics",record_topic,
                         "--disable-keyboard-controls","-o","/tmp/"+name+"-"+label])
        def decode(label,c,rmw,record_topic):
            script="""import json,sys
import rosbag2_py
from rclpy.serialization import deserialize_message
from std_msgs.msg import String
reader=rosbag2_py.SequentialReader()
reader.open_uri(sys.argv[1])
metadata=reader.get_metadata()
assert metadata.storage_identifier==rosbag2_py.get_default_storage_id()=="mcap"
topics={t.name:t.type for t in reader.get_all_topics_and_types()}
assert topics[sys.argv[2]]=="std_msgs/msg/String"
values=[]
while reader.has_next():
 topic,data,*_=reader.read_next_ext()
 assert topic==sys.argv[2]
 value=deserialize_message(data,String).data
 assert value.startswith('Hello, world! ')
 values.append(int(value.removeprefix('Hello, world! ')))
print(json.dumps(values))"""
            values=json.loads(sp.check_output(command(c,rmw,["python3","-c",script,"/tmp/"+name+"-"+label,record_topic]),text=True))
            assert values==sorted(set(values))
            return values
        if network:
            config=dict(dynamic_discovery=False,network_rmw=network,heaphook="/runtime/libagnocast_heaphook.so",
                        exports=[dict(topic=topic,remap_to=outside,type="std_msgs/msg/String")])
            docker("exec",app,"sh","-ec",'echo "$1" >/tmp/migration.json',"sh",json.dumps(config))
            if network==zenoh:
                start("router",peer,zenoh,["env","-u","ZENOH_CONFIG_OVERRIDE","/opt/ros/lyrical/lib/rmw_zenoh_cpp/rmw_zenohd"])
            bag("remote",peer,network,outside)
            if not automatic:
                bridge("gateway")
        def app_args(role):
            return ["ros2","run",*packages[role],"--ros-args","-r","topic:="+topic]
        start("sub",app,transport,app_args("sub"))
        if transport==native:
            bag("local",app,native,topic)
        extra=("-e","RMW_AGNOCAST_GATEWAY_CONFIG=/tmp/migration.json","-e","AGNOCAST_GATEWAY_EXECUTABLE="+prefix+"/install/agnocast_cyclone_gateway/lib/agnocast_cyclone_gateway/topic_gateway") if automatic else ()
        start("pub",app,transport,app_args("pub"),extra)
        tick()
        for role,(pkg,exe) in packages.items():
            executable = f"{prefix}/install/{pkg}/lib/{pkg}/{exe}"
            lookup = """import os, sys
from pathlib import Path
matches = []
for path in Path('/proc').glob('[0-9]*/exe'):
    try:
        if os.readlink(path) == sys.argv[1]:
            matches.append(path.parent.name)
    except (FileNotFoundError, PermissionError):
        pass
pid, = matches
Path('/tmp/' + sys.argv[2] + '.pid').write_text(pid)
print(pid)"""
            pid = docker("exec", app, "python3", "-c", lookup, executable, role)
            assert docker("exec",app,"sha256sum","/proc/"+pid+"/exe").split()[0]==hashes[role]
            maps=docker("exec",app,"cat","/proc/"+pid+"/maps")
            libs=sorted(set(line.split()[-1] for line in maps.splitlines() if ".so" in line))
            (path/(role+"-libraries.txt")).write_text("\n".join(libs)+"\n")
            assert any(x.startswith(prefix+"/install/rclcpp/lib/librclcpp.so") for x in libs)
            assert any("lib"+transport+".so" in x for x in libs)
            if transport==native:
                assert not any("libddsc.so" in x or "librmw_cyclonedds_cpp" in x or "librmw_zenoh_cpp" in x for x in libs)
        if network:
            until(lambda:"Subscribed to topic" in log("remote"),"remote recorder subscribed",[procs["remote"][1]])
        if network and not automatic:
            until(lambda:"source armed" in log("gateway"), "gateway armed", [procs["gateway"][1]])
        if transport==native:
            until(lambda:"Subscribed to topic" in log("local"),"local recorder subscribed",[procs["local"][1]])
        expected=[window()]
        outage=[]
        if network and not automatic:
            stop("gateway")
            outage=window()
            bridge("replacement")
            until(lambda:"source armed" in log("replacement"),"gateway rearmed",[procs["replacement"][1]])
            expected.append(window())
        stop("sub")
        if transport==native:
            stop("local")
        if network:
            stop("remote")
            if not automatic:
                stop("replacement")
        stop("pub")
        if "router" in procs:
            stop("router")
        delivered=sequences()
        assert delivered==list(range(delivered[0],delivered[-1]+1))
        local=decode("local",app,native,topic) if transport==native else delivered
        assert local==list(range(local[0],local[-1]+1))
        remote=decode("remote",peer,network,outside) if network else []
        for interval in expected:
            assert set(interval)<=set(local)&set(delivered)
            if network: assert set(interval)<=set(remote)
        assert set(outage)<=set(local)&set(delivered) and not set(outage)&set(remote)
        result=dict(case=name,binary_sha256=hashes,delivery=delivered,local=local if transport==native else None,remote=remote,windows=expected,outage=outage)
        (path/"result.json").write_text(json.dumps(result,indent=2))
        print(name,"passed",len(delivered),"listener samples,",len(remote),"remote bag samples",flush=True)
        results.append(result)

    for args in [("baseline",cyclone),("local",native),("manual-cyclone",native,cyclone),
                 ("manual-zenoh",native,zenoh),("automatic-cyclone",native,cyclone,True),("rollback",cyclone)]:
        run_case(*args)
(evidence/"results.json").write_text(json.dumps(results,indent=2))
