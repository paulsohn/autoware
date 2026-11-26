"""Run with PYTHONPATH=src/agnocast_cyclone_gateway."""
from gateway_config import configured_routes
from gateway_discovery import wanted_routes

fixed = dict(exports=[dict(topic="/a")], heaphook="/hook")
for value in ({}, {"dynamic_discovery":"false"}, {"dynamic_discovery":0}, {"dynamic_discovery":None}):
    try:
        configured_routes({**fixed, **value})
    except ValueError as error:
        assert "explicit true or false" in str(error)
    else:
        raise AssertionError(value)
assert configured_routes(dict(fixed, dynamic_discovery=False)) == configured_routes(dict(fixed, dynamic_discovery=True))
assert configured_routes(dict(dynamic_discovery=True, heaphook="/hook")) == []

writer = ["/a","std_msgs/msg/String","hash","pub","best_effort"]
reader = [*writer[:3],"sub","best_effort"]
def match(local, remote, fixed=()):
    return wanted_routes(local, remote, fixed)
assert "/a" in match([writer],[reader])
assert not match([writer],[reader],["/a"])
assert not match([writer],[[*reader[:4],"reliable"]])
assert not match([writer],[[reader[0],reader[1],"other-hash",*reader[3:]]])
assert not match([writer],[reader, [reader[0],"other","other-hash",*reader[3:]]])
assert not match([writer,reader],[reader,writer])
