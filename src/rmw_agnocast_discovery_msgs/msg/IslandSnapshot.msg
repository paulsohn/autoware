# AWF island identity: kernel boot ID and Linux IPC namespace inode.
string host_uuid
uint64 ipc_ns_inode
# Monotonic start time in nanoseconds. Compare only within one island.
uint64 generation
DiscoveryEndpoint[] endpoints
