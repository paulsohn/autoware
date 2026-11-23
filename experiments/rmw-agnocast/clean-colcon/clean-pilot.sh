#!/usr/bin/env bash
# Run inside the guarded Lyrical root. Inputs and build products use separate directories.
set -e
w=build/reproduction
inputs=$PWD/$w/inputs
export XML_CATALOG_FILES="$inputs/catalog.xml"
source build/rmw-agnocast/install/local_setup.bash
export CC=/usr/bin/clang-21 CXX=/usr/bin/clang++-21 ENABLE_AGNOCAST=0
flags=(-DCMAKE_BUILD_TYPE=Release '-DCMAKE_CXX_FLAGS_RELEASE=-O1 -DNDEBUG'
       '-DCMAKE_CXX_FLAGS=--gcc-install-dir=/usr/lib/gcc/x86_64-linux-gnu/15 -Wno-error=unknown-warning-option'
       -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DBUILD_TESTING=OFF)
install_base=$w/install
build_packages() {
  colcon --log-base "$w/log" build --base-paths "$@" --executor sequential \
    --build-base "$w/obj" --install-base "$install_base" --cmake-args "${flags[@]}"
  source "$install_base/local_setup.bash"
}
if [[ -f "$w/install/local_setup.bash" ]]; then source "$w/install/local_setup.bash"; fi
case "$1" in
  transport)
    mkdir -p "$w/src/point_cloud_msg_wrapper"
    echo "7c3b7e8a8099891f3bff22c11b572978bf7fdf27660df45c3d0fd6da6bcf4373  $inputs/point_cloud_msg_wrapper.tar.gz" | sha256sum -c -
    tar -xzf "$inputs/point_cloud_msg_wrapper.tar.gz" --strip-components=1 -C "$w/src/point_cloud_msg_wrapper"
    build_packages src/core/agnocast/src/{agnocast_cie_config_msgs,agnocast_cie_thread_configurator,agnocastlib} src/rmw_agnocast
    export PATH="$inputs/rust-toolchain/bin:$PATH" CARGO_BUILD_JOBS=1
    export CARGO_TARGET_DIR="$PWD/$w/cargo"
    rustc --version
    cargo build --offline --locked --release --manifest-path src/core/agnocast/agnocast_heaphook/Cargo.toml \
      --config 'source.crates-io.replace-with="vendor"' --config "source.vendor.directory=\"$inputs/vendor\""
    install -m755 "$CARGO_TARGET_DIR/release/libagnocast_heaphook.so" "$install_base/agnocastlib/lib/"
    ;;
  pipeline)
    build_packages src/core/autoware_cmake/{autoware_cmake,autoware_lint_common} \
      src/core/autoware_utils/autoware_utils_rclcpp src/core/autoware_core/common/autoware_agnocast_wrapper \
      src/core/autoware_internal_msgs/{autoware_internal_debug_msgs,autoware_internal_msgs} \
      src/core/autoware_msgs/{autoware_common_msgs,autoware_planning_msgs,autoware_sensing_msgs} \
      "$w/src/point_cloud_msg_wrapper" src/universe/external/managed_transform_buffer/managed_transform_buffer \
      src/agnocast_rmw_loan_probe src/universe_crop_benchmark src/universe_sync_benchmark
    ;;
  perception)
    flags+=(-DCMAKE_CXX_STANDARD=17)
    install_base=build/perception-replay/install
    build_packages src/core/autoware_msgs/autoware_perception_msgs src/universe/external/tier4_autoware_msgs/tier4_perception_msgs src/universe_detection_replay
    ;;
  tracker)
    source build/perception-replay/install/local_setup.bash
    pkg=autoware_multi_object_tracker
    for pair in "muSSP src/universe/external/muSSP mussp-parent" \
                "$pkg src/universe/autoware_universe/perception/$pkg tracker-lyrical"; do
      read -r name upstream fix <<< "$pair"
      if [[ ! -d "$w/src/$name" ]]; then
        cp -a "$upstream" "$w/src/$name"
        patch -d "$w/src/$name" -p1 < "experiments/rmw-agnocast/$fix.patch"
        if [[ $name == "$pkg" ]]; then
          patch -d "$w/src/$name" -p1 < experiments/rmw-agnocast/tracker-consume-stamp.patch
        fi
      fi
    done
    CC=/usr/bin/gcc CXX=/usr/bin/g++ colcon --log-base "$w/log" build --base-paths "$w/src/muSSP" --executor sequential \
      --build-base "$w/obj" --install-base "$install_base" --cmake-args -DBUILD_TESTING=OFF -DCMAKE_POLICY_VERSION_MINIMUM=3.5
    source "$install_base/local_setup.bash"
    flags+=(-DCMAKE_CXX_STANDARD=17)
    build_packages src/core/autoware_utils/{autoware_utils_system,autoware_utils_debug,autoware_utils_math,autoware_utils_geometry} \
      src/core/autoware_msgs/autoware_vehicle_msgs src/core/autoware_internal_msgs/autoware_internal_planning_msgs "$w/src/$pkg"
    ;;
  *)
    exit 2
    ;;
esac
