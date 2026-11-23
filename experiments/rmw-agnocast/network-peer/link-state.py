"""Set the prepared peer's eth0 up/down without replacing its veth or IP address."""
import fcntl
import socket
import struct
import sys

with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
    iface = b"eth0"
    request = fcntl.ioctl(sock, 0x8913, struct.pack("16sH14x", iface, 0))  # SIOCGIFFLAGS
    _, flags = struct.unpack("16sH14x", request)
    assert sys.argv[1] in ("up", "down")
    flags = flags | 1 if sys.argv[1] == "up" else flags & ~1
    fcntl.ioctl(sock, 0x8914, struct.pack("16sH14x", iface, flags))  # SIOCSIFFLAGS
    print(socket.if_nametoindex("eth0"))
