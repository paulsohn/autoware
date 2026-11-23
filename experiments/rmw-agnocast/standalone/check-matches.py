"""Exercise live matched counts in both directions, including an unclean peer exit."""
import subprocess
import sys

import rclpy
from rclpy.qos import QoSProfile, ReliabilityPolicy
from std_msgs.msg import Int32, String


def endpoints(node, msg, writer, reader, offered, requested):
    pub = node.create_publisher(msg, writer, offered)
    sub = node.create_subscription(msg, reader, lambda _: None, requested)
    return pub, sub


def check(pub, sub, expected):
    for _ in range(3):
        actual = pub.get_subscription_count(), sub.get_publisher_count()
        assert actual == (expected, expected), (expected, actual)


def remote(node, offered, requested):
    endpoints(node, String, '/match_in', '/match_out', offered, requested)
    print('peer ready', flush=True)
    input()


def run(node, best, reliable):
    pub, sub = endpoints(node, String, '/match_out', '/match_in', best, reliable)
    check(pub, sub, 0)
    endpoints(node, Int32, '/match_in', '/match_out', reliable, best)
    endpoints(node, String, '/match_in', '/match_out', best, reliable)
    check(pub, sub, 0)
    for kill in (False, True, False):
        peer = subprocess.Popen([sys.executable, __file__, 'peer'], stdin=subprocess.PIPE,
                                stdout=subprocess.PIPE, text=True)
        try:
            for line in peer.stdout:
                if line.strip() == 'peer ready':
                    break
            else:
                raise RuntimeError('peer exited before discovery')
            check(pub, sub, 1)
            extra_pub, extra_sub = endpoints(node, String, '/match_in', '/match_out', reliable, best)
            check(pub, sub, 2)
            node.destroy_publisher(extra_pub)
            node.destroy_subscription(extra_sub)
            check(pub, sub, 1)
            if kill:
                peer.kill()
            output, _ = peer.communicate('\n' if not kill else None, timeout=8)
            assert peer.returncode == (-9 if kill else 0), output
            check(pub, sub, 0)
        finally:
            if peer.poll() is None:
                peer.kill()
                peer.wait()
    print('both matched counts verified across QoS, types, join, removal, kill and restart')


rclpy.init()
is_peer = len(sys.argv) > 1
node = rclpy.create_node('match_peer' if is_peer else 'match_checker')
best = QoSProfile(depth=4, reliability=ReliabilityPolicy.BEST_EFFORT)
reliable = QoSProfile(depth=4, reliability=ReliabilityPolicy.RELIABLE)
try:
    if is_peer:
        remote(node, reliable, best)
    else:
        run(node, best, reliable)
finally:
    node.destroy_node()
    rclpy.shutdown()
