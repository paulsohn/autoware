#!/bin/sh
# Applies the kernel settings bazel/config/cyclonedds.xml relies on, in the current
# network namespace. Run it as the runtime container's entrypoint step, or at boot on a
# bare-metal/OTA host (host/autoware-dds-setup.service). Idempotent.
#
# - net.core.rmem_max is machine-wide: only root on the host or a privileged container
#   (writing the host kernel) can raise it. Docker refuses --sysctl for it, and CAP_NET_ADMIN
#   does not help (CycloneDDS does not use SO_RCVBUFFORCE).
# - lo multicast needs CAP_NET_ADMIN in the network namespace the processes run in.
# - net.ipv4.ipfrag_* are per network namespace (settable with docker run --sysctl).
# Values from the Autoware DDS settings guide.
set -u

fail=0

# Required: CycloneDDS aborts if SocketReceiveBufferSize min="10MB" cannot be met.
want=2147483647
need=10485760
cur=$(cat /proc/sys/net/core/rmem_max)
if [ "$cur" -lt "$need" ]; then
  if { echo "$want" > /proc/sys/net/core/rmem_max; } 2>/dev/null; then
    echo "dds_setup: net.core.rmem_max $cur -> $want"
  else
    echo "dds_setup: ERROR net.core.rmem_max=$cur < $need and cannot be raised here;" \
      "run as a privileged container or apply on the host" >&2
    fail=1
  fi
fi

# Required: cyclonedds.xml discovers over multicast on lo.
if ! ip link show lo | grep -q MULTICAST; then
  if ip link set lo multicast on 2>/dev/null; then
    echo "dds_setup: lo multicast on"
  else
    echo "dds_setup: ERROR lo has no multicast and cannot be changed here (needs CAP_NET_ADMIN)" >&2
    fail=1
  fi
fi

# Recommended: fragment reassembly tuning for large messages (point clouds).
for kv in net.ipv4.ipfrag_time=3 net.ipv4.ipfrag_high_thresh=134217728; do
  f=/proc/sys/$(echo "${kv%%=*}" | tr . /)
  [ "$(cat "$f")" = "${kv#*=}" ] && continue
  { echo "${kv#*=}" > "$f"; } 2>/dev/null || echo "dds_setup: WARNING cannot set $kv" >&2
done

exit "$fail"
