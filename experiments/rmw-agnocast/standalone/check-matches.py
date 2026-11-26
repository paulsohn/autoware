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
    node.create_publisher(String, '/match_peer_marker', 1)
    print('peer ready', flush=True)
    input()


def check_inventory(node, live):
    topic = '/match_peer_marker'
    assert ('match_peer' in node.get_node_names()) == live
    assert (topic in dict(node.get_topic_names_and_types())) == live
    assert [ep.node_name for ep in node.get_publishers_info_by_topic(topic)] == (
        ['match_peer'] if live else [])
    assert bool([ep for ep in node.get_subscriptions_info_by_topic('/match_out')
                 if ep.node_name == 'match_peer']) == live


def run(node, best, reliable):
    check_inventory(node, False)
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
            check_inventory(node, True)
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
            check_inventory(node, False)
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
