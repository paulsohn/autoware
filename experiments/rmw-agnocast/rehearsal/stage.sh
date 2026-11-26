#!/usr/bin/env bash
set -euo pipefail
out=${1:?fresh workspace path}
examples=${2:?path to a git checkout of ros2/examples}
schema=${3:?directory containing the ROS package format 3 schemas}
mkdir "$out"
out=$(realpath "$out")
mkdir "$out/src" "$out/schema"
git rev-parse HEAD > "$out/middleware.rev"
git archive HEAD src/rclcpp/rclcpp src/core/agnocast/src/agnocast_cie_config_msgs \
    src/core/agnocast/src/agnocast_cie_thread_configurator src/core/agnocast/src/agnocastlib \
    src/rmw_agnocast src/rmw_agnocast_discovery_msgs src/agnocast_cyclone_gateway | tar -x -C "$out"
rev=cc721964a79192e46db0111c0ba9f6a831fc9395
git -C "$examples" archive "$rev" rclcpp/topics/minimal_publisher rclcpp/topics/minimal_subscriber | tar -x -C "$out/src"
cp "$schema"/{package_format3.xsd,package_common.xsd} "$out/schema/"
echo '<catalog xmlns="urn:oasis:names:tc:entity:xmlns:xml:catalog"><uri name="http://download.ros.org/schema/package_format3.xsd" uri="package_format3.xsd"/></catalog>' > "$out/schema/catalog.xml"
(cd "$out/schema" && sha256sum --check <<'HASHES'
941ea8645344f3c4b7b9d7e68799898309d65a18225fa9cbef4169d95d1a3211  package_common.xsd
f096a197ed6d7878984bb2501a55f7f1bd4895d254399fb7857e154bfb644f41  package_format3.xsd
HASHES
)
