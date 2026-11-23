"""Launch three Universe crop components, their synchronizer, and a verifying consumer."""
import os
import shlex
from pathlib import Path

from ament_index_python.packages import get_package_prefix, get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, EmitEvent, OpaqueFunction, RegisterEventHandler
from launch.event_handlers import OnProcessExit
from launch.events import Shutdown
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import ComposableNodeContainer, Node
from launch_ros.descriptions import ComposableNode


def graph(context):
    mode = LaunchConfiguration("mode").perform(context)
    directory = LaunchConfiguration("fixture_directory").perform(context)
    if not Path(directory).is_dir() or any((Path(directory) / name).exists() for name in
                                           ("sink-ready", "stamps", "stamps.tmp", "first-output")):
        raise RuntimeError("fixture_directory must be a fresh directory for this launch")
    sensors = ("left", "right", "top")
    cropped = [f"/sync/{sensor}" for sensor in sensors]
    raw = ["/raw" + topic for topic in cropped]
    region_topics = ["/region/near", "/region/far"] if os.getenv("SYNC_REGIONS") else []
    use_agnocast = mode in ("plain", "stock", "runner")
    env = {"ENABLE_AGNOCAST": "0", "SYNC_LARGE": "1", "PIPELINE_DIR": directory}
    if use_agnocast:
        env.update(
            RMW_IMPLEMENTATION="rmw_agnocast",
            LD_PRELOAD=str(Path(get_package_prefix("agnocastlib")) / "lib/libagnocast_heaphook.so"),
            AGNOCAST_BRIDGE_MODE="OFF", AGNOCAST_NO_DISCOVERY_AGENT="1",
            AGNOCAST_RMW_HOST_CLOUD="1", AGNOCAST_RMW_TOPIC=":".join(raw + cropped + ["/output"] + region_topics),
        )
    if mode == "native":
        env.update(ENABLE_AGNOCAST="1", RMW_IMPLEMENTATION="rmw_cyclonedds_cpp",
                   LD_PRELOAD=str(Path(get_package_prefix("agnocastlib")) / "lib/libagnocast_heaphook.so"),
                   AGNOCAST_BRIDGE_MODE="OFF", AGNOCAST_NO_DISCOVERY_AGENT="1")
    sync_env = dict(env, SYNC_BYTE_INPUT="1")
    ownership = os.environ.get("SYNC_STANDALONE_OWNERSHIP")
    if ownership in ("view", "loan", "crop-copy", "crop-view"):
        sync_env.update(SYNC_VIEW_INPUT="1", AGNOCAST_RMW_HOST_VIEW="1")
        if ownership == "loan":
            sync_env.update(SYNC_LOAN_OUTPUT="1", SYNC_SHARED_OUTPUT="1",
                            AGNOCAST_RMW_HOST_LOAN="1", ROS_DISABLE_LOANED_MESSAGES="0")
    if use_agnocast and mode != "plain":
        sync_env.update(SYNC_VIEW_INPUT="1", AGNOCAST_RMW_HOST_VIEW="1")

    share = Path(get_package_share_directory("universe_sync_benchmark"))
    crop_nodes = []
    native_args = []
    for i, sensor in enumerate(sensors):
        frame = "base_link" if i == 0 else "sync_lidar"
        config = str(share / "config/crop_box_filter_node.param.yaml")
        bounds = {"input_frame": frame, "output_frame": frame,
                  "min_x": i * 8192 - 0.5, "max_x": i * 8192 + 16383.5,
                  "min_y": -1.0, "max_y": 24.0, "min_z": -1.0, "max_z": 3.0}
        crop_nodes.append(ComposableNode(
            package="universe_crop_benchmark",
            plugin="autoware::pointcloud_preprocessor::CropBoxFilterComponent",
            name="crop_" + sensor, parameters=[config, bounds],
            remappings=[("input", raw[i]), ("output", cropped[i])],
        ))
        native_args.extend(["--crop", "--ros-args", "-r", "__node:=crop_" + sensor,
                            "-r", "input:=" + raw[i], "-r", "output:=" + cropped[i],
                            "--params-file", config])
        for key, value in bounds.items():
            native_args.extend(["-p", f"{key}:={value}"])
    container_env = env.copy()
    if ownership in ("crop-copy", "crop-view"):
        # Use the same view-capable component for both arms of this comparison.
        prefix = os.environ["SYNC_CROP_COMPONENT_PREFIX"]
        container_env.update(AMENT_PREFIX_PATH=prefix + ":" + os.environ["AMENT_PREFIX_PATH"],
                             BENCH_TYPED_INPUT="1", BENCH_BUFFER="cpu")
        if ownership == "crop-view":
            container_env["AGNOCAST_RMW_HOST_VIEW"] = "1"
    container = ComposableNodeContainer(
        name="lidar_crops", namespace="", package="rclcpp_components",
        executable="component_container", composable_node_descriptions=crop_nodes,
        additional_env=container_env, output="screen",
    )
    if mode == "native":
        container = Node(package="universe_crop_benchmark", executable="native_crop_container",
                         arguments=native_args, additional_env=env, output="screen")
    sync = {"package": "universe_sync_benchmark", "executable": "concatenate_and_time_sync_node"}
    ready = os.path.join(directory, "first-output")
    if mode == "runner":
        sync.update(package="agnocast_rmw_loan_probe", executable="crop_wait_runner",
                    arguments=["libuniverse_sync.so",
                               "autoware::pointcloud_preprocessor::PointCloudConcatenateDataSynchronizerComponent"])
        sync_env.update(BENCH_CSV=ready, AGNOCAST_FIXED_PROBE="1", AGNOCAST_RMW_DIRECT_WAIT="1",
                        AGNOCAST_RMW_READY_FD="0")
    synchronizer = Node(**sync, parameters=[
        str(share / "config/concatenate_and_time_sync_node.param.yaml"),
        {"input_topics": cropped, "is_motion_compensated": False,
         "publish_synchronized_pointcloud": False, "timeout_sec": 0.04},
        *([{"maximum_queue_size": 32}] if os.getenv("SYNC_RECORDER_DEEP_HISTORY") else []),
    ], additional_env=sync_env, output="screen")
    sink_env = dict(env, SYNC_READY_FILE=ready)
    if mode == "stock" and os.getenv("SYNC_SINK_TRACE"):
        sink_env["AGNOCAST_RMW_HANDOFF_FILE"] = os.environ["SYNC_SINK_TRACE"]
    sink = Node(package="universe_sync_benchmark", executable="sync_fixture", name="pipeline_sink",
                additional_env=sink_env, output="screen")
    source = Node(package="universe_sync_benchmark", executable="sync_fixture", name="pipeline_source",
                  additional_env=dict(env, PIPELINE_SOURCE="1"),
                  remappings=[(raw[2], "/dds/top")] if os.getenv("SYNC_DDS_TOP") else [],
                  output="screen")
    tf = Node(package="tf2_ros", executable="static_transform_publisher",
              arguments=["--x", "4", "--frame-id", "base_link", "--child-frame-id", "sync_lidar"],
              additional_env=env, output="screen")
    regions = []
    for topic, (low, high) in zip(region_topics, [(11999.5, 17999.5), (15999.5, 22999.5)]):
        crop_env = dict(env)
        if topic == "/region/near" and os.getenv("SYNC_STALL_NEAR"):
            crop_env["BENCH_STALL_THIS_CROP"] = "1"
        executable = "crop"
        if topic == "/region/near" and mode != "native" and os.getenv("BENCH_EXECUTOR") in ("cie", "two", "three"):
            executable = "crop_" + os.environ["BENCH_EXECUTOR"]
        prefix = None
        if topic == "/region/near" and os.getenv("BENCH_SECOND_TIMER"):
            args = ["taskset", "-c", os.environ["BENCH_NEAR_CPUS"]]
            if crop_env.get("LD_PRELOAD"):
                args.extend(["env", "LD_PRELOAD=" + crop_env["LD_PRELOAD"]])
                crop_env["LD_PRELOAD"] = ""
            prefix = shlex.join(args)
        regions.append(Node(package="universe_crop_benchmark", executable=executable, prefix=prefix,
                            name="region_" + topic.split("/")[-1], output="screen",
                            additional_env=crop_env, remappings=[("input", "/output"), ("output", topic)],
                            parameters=[config, {"input_frame": "base_link", "output_frame": "base_link",
                                                "min_x": low, "max_x": high, "min_y": -1.0,
                                                "max_y": 24.0, "min_z": -1.0, "max_z": 3.0}]))
    def finished(event, _context):
        if event.returncode:
            raise RuntimeError(f"pipeline verifier exited with status {event.returncode}")
        return [EmitEvent(event=Shutdown(reason="pipeline verification finished"))]

    return [RegisterEventHandler(OnProcessExit(target_action=sink, on_exit=finished)),
        tf, container, synchronizer, *regions, sink, source]


def generate_launch_description():
    return LaunchDescription([
        DeclareLaunchArgument("mode", choices=["plain", "stock", "runner", "cyclone", "zenoh", "native"], default_value="stock"),
        DeclareLaunchArgument("fixture_directory", description="Fresh directory for source timestamps and readiness"),
        OpaqueFunction(function=graph),
    ])
