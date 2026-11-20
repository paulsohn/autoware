#!/usr/bin/env bash
set -e
source build/agnocast-lyrical/install/local_setup.bash
obj=build/agnocast-lyrical/obj/agnocastlib
src=src/core/agnocast/src/agnocastlib
dest=build/rmw-agnocast/path-trace-lib
test -f "$obj/CMakeCache.txt"
mkdir -p "$dest"
# Restore the default build configuration even when the diagnostic build fails.
restore() { cmake -S "$src" -B "$obj" -DAGNOCAST_PATH_PROBE=OFF; }
trap restore EXIT
cmake -S "$src" -B "$obj" -DAGNOCAST_PATH_PROBE=ON
cmake --build "$obj" --target agnocast --parallel 1
cp "$obj/libagnocast.so" "$dest/"
