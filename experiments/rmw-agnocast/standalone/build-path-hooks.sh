#!/usr/bin/env bash
# Run in the guarded Lyrical shell. Reuse unmodified Release objects and link a private reader DSO.
set -e
source build/migration-guide/install/local_setup.bash
source build/standalone/o3-install/local_setup.bash
root=$PWD
obj=build/standalone/o3-obj/agnocastlib
out="$root/build/standalone/path-hooks"
mkdir -p "$out"
cd "$obj"
python3 - "$out" <<'PY'
import pathlib, shlex, subprocess, sys
out = pathlib.Path(sys.argv[1])
target = pathlib.Path('CMakeFiles/agnocast.dir')
flags = dict(line.split(' = ', 1) for line in (target/'flags.make').read_text().splitlines() if ' = ' in line)
args = shlex.split(' '.join(flags[k] for k in ('CXX_DEFINES','CXX_INCLUDES','CXX_FLAGS')))
assert [a for a in args if a.startswith('-O')][-1] == '-O3'
link = shlex.split((target/'link.txt').read_text())
for name in ('agnocast_epoll','agnocast_callback_info'):
    source = pathlib.Path('/ws/src/core/agnocast/src/agnocastlib/src')/(name+'.cpp')
    obj = str(out/(name+'.o'))
    subprocess.run([link[0], *args, '-DAGNOCAST_PATH_PROBE',
                    '-Dpath_probe_wake=path_timing_wake', '-Dpath_probe_loan=path_timing_loan', '-c', source, '-o', obj], check=True)
    link[link.index(str(target/'src'/(name+'.cpp.o')))] = obj
link = [arg.replace("=CMakeFiles/agnocast.dir/link.d", "="+str(out/"link.d")) for arg in link]
link[link.index('-o')+1] = str(out/'libagnocast.so')
subprocess.run(link, check=True)
PY
cd "$root"
incs=()
for dir in /opt/ros/lyrical/include/*; do
  if [[ -d $dir ]]; then incs+=("-I$dir"); fi
done
detail=()
case ${1:-} in
  --with-waits) detail=(-DPATH_WAIT_DETAIL);;
  "") ;;
  *) exit 2;;
esac
/usr/bin/clang++-21 --gcc-install-dir=/usr/lib/gcc/x86_64-linux-gnu/15 -O3 -shared -fPIC "${incs[@]}" "${detail[@]}" experiments/rmw-agnocast/standalone/path_timing.cpp -o "$out/path-timing.so"
