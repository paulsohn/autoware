# Autoware map asset

The product root selects the local `autoware_map_asset` module
with `bazel_dep` and `local_path_override(path = "assets/map")`. This module
owns the official sample-map-planning archive used by AWF's
[planning-simulation preparation](https://docs.autoware.org/main/simulation/planning-simulation/).
The [version 1.4.0 tutorial](https://docs.autoware.org/1.4.0/tutorials/ad-hoc-simulation/planning-simulation/)
uses the same named map with sample_vehicle and sample_sensor_kit. Both pages
were reviewed on 2026-11-21; their HOME-directory installation instructions are
not the Bazel runtime resource convention.

Source: <https://autoware-files.s3.us-west-2.amazonaws.com/maps/demos/sample-map-planning.zip>

Archive SHA-256: `5536fce7bb8db7688fdf94ec004118b898637ad0d5b6175108b10989dd6e93b9`.
The upstream URL is mutable; the pinned checksum defines the accepted input.
Archive members are dated 2023-11-09. Map copyright is 2020 TIER IV, Inc., as
attributed by the official preparation page. No map payload is edited or stored
in this source tree.

| File after stripping sample-map-planning/ | Bytes | SHA-256 |
| --- | ---: | --- |
| lanelet2_map.osm | 1071550 | `4fe358f2d1e182dc76de296e5619086f8d8ad2fcab3e6d8f99f6e3d724a84e6d` |
| pointcloud_map.pcd | 28125828 | `2da791365627241513c56a88a78baaf17d86b5e90febcb8987ec92c9e99ad2c6` |
| map_projector_info.yaml | 60 | `9753eba4724aa6d41ffa2cc03e8da2d82e9f317dd14da837b0e96a7705e72bfa` |
| map_config.yaml | 178 | `c7f8caeaad3559d2bc924a0683f36673f72c9e15f315c2e95f371c9ccceb7e39` |

Its MODULE.bazel pins/fetches the archive and applies the module-owned BUILD
overlay; the private data repository is not a product-level API.
`@autoware_map_asset//:ament_index` installs the unchanged payload into the
resource package `autoware_map_asset`. The asset identity is shared by core and
simulator products; the pinned archive is the current development dataset.
Consumers declare that target and resolve its share directory through
ament_index. The supported core entrypoint uses
`$(find-pkg-share autoware_map_asset)` internally; its callers supply no map_path
argument. Native and container products carry the map through the declared
runtime closure without a separate map mount. Bazel fetches
the archive during the build; runtime lookup does not download or consult HOME.
This map asset index is independent of the per-repository model indexes. See
[the asset/deployer sketch](../README.md) for the proposed selection boundary.

The projector file specifies MGRS / 54SVE / WGS84. Retain the legacy map_config.yaml
for fidelity, but use the projector file for the current map-loader contract.
The OSM has 4499 nodes and 241 relations; it has no MetaInfo version element.
The official binary XYZ/RGB PCD declares 1757841 points and has 186 trailing zero
padding bytes. The real PCL reader accepts the cloud; do not strip or rewrite it.

The root [scenario fixture](../../bazel/tests/planning_simulator_resources/scenario.yaml)
records a short initial/goal pair and its observed preferred lanelets 9102/9540.
Its Universe DefaultPlanner gate checks route feasibility with the indexed
sample vehicle dimensions. This establishes resource/geometry preparation;
full simulator startup, autonomous motion, RViz and packaged driving acceptance
have separate gates.


## Colcon resource installation

The package also supplies package.xml and minimal ament_cmake support. With a
ROS/colcon environment sourced, include this asset alongside the source packages:

```sh
colcon build --base-paths assets/map --packages-select autoware_map_asset --symlink-install
source install/setup.bash
```

CMake 3.24 or newer fetches the same checksum-pinned archive into the build tree
and installs the four original files plus the package index. With symlink install,
the large files remain in the build tree. Include assets/map in colcon's discovery
paths when building components that declare this runtime dependency. Backend
injection and alternative datasets remain the sketch in ../README.md.
