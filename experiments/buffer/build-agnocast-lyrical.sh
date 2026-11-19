#!/usr/bin/env bash
# Run inside run-lyrical.sh with Cargo and rustc on PATH.
set -e
export ENABLE_AGNOCAST=1 RMW_IMPLEMENTATION=rmw_cyclonedds_cpp
export CARGO_BUILD_JOBS=1 CTEST_PARALLEL_LEVEL=1
w=build/agnocast-lyrical
packages=(src/core/agnocast/src/{agnocast_cie_config_msgs,agnocast_cie_thread_configurator,agnocastlib})
args=(--base-paths "${packages[@]}" --build-base "$w/obj" --install-base "$w/install"
      --executor sequential)
colcon --log-base "$w/log" build "${args[@]}" --cmake-args -DBUILD_TESTING=ON \
  -DCMAKE_BUILD_TYPE=Release '-DCMAKE_CXX_FLAGS_RELEASE=-O2 -DNDEBUG --param=ggc-min-expand=20 --param=ggc-min-heapsize=32768'
source "$w/install/local_setup.bash"
hook=src/core/agnocast/agnocast_heaphook/Cargo.toml
export CARGO_TARGET_DIR="$PWD/$w/cargo-target"
cargo build --locked --release --manifest-path "$hook"
install -m 755 "$CARGO_TARGET_DIR/release/libagnocast_heaphook.so" "$w/install/agnocastlib/lib/"
cargo test --locked --lib --manifest-path "$hook"
colcon --log-base "$w/log" test "${args[@]}" --return-code-on-test-failure
colcon test-result --test-result-base "$w/obj"
