"""Public route-file formats and conversion to validated CLI arguments."""
import sys


def route_arguments(config):
    """Translate the public file schema into the same validated route as a manual launch."""
    if isinstance(config, list) and all(isinstance(word, str) for word in config):
        print("Deprecated gateway argument array; migrate to a JSON route object", file=sys.stderr)
        return config
    if not isinstance(config, dict):
        raise ValueError("expected a JSON route object")
    required = ("direction", "type", "source", "destination", "heaphook")
    unknown = config.keys() - {*required, "network_rmw", "always_on", "qos"}
    if unknown:
        raise ValueError(f"unknown route fields: {', '.join(sorted(unknown))}")
    for key in required:
        if not isinstance(config.get(key), str) or not config[key] or config[key].startswith("-"):
            raise ValueError(f"{key} requires a nonempty route value")
    words = [config[key] for key in ("direction", "type", "source", "destination")]
    words.append("--heaphook=" + config["heaphook"])
    if "network_rmw" in config:
        if not isinstance(config["network_rmw"], str):
            raise ValueError("network_rmw requires an RMW name")
        words.append("--network-rmw=" + config["network_rmw"])
    always_on = config.get("always_on", False)
    if not isinstance(always_on, bool):
        raise ValueError("always_on requires a boolean")
    if always_on:
        words.append("--always-on")
    qos = config.get("qos", {})
    if not isinstance(qos, dict) or qos.keys() - {"depth", "input_reliability", "output_reliability"}:
        raise ValueError("qos accepts depth, input_reliability and output_reliability")
    for key, value in qos.items():
        if key == "depth":
            if type(value) is not int or value < 1:
                raise ValueError("qos.depth requires a positive integer")
        elif not isinstance(value, str):
            raise ValueError(f"qos.{key} requires a reliability name")
        words.append(f"--{key.replace('_', '-')}={value}")
    return words


def configured_routes(config):
    if not isinstance(config, dict) or not ({"exports", "imports"} & config.keys()):
        return [route_arguments(config)]
    unknown = config.keys() - {"exports", "imports", "network_rmw", "heaphook", "qos"}
    if unknown:
        raise ValueError(f"unknown deployment fields: {sorted(unknown)}")
    defaults = {k: config[k] for k in ("network_rmw", "heaphook") if k in config}
    base_qos = config.get("qos", {})
    if not isinstance(base_qos, dict):
        raise ValueError("deployment qos must be an object")
    routes = []
    for direction, remap_key in (("export", "remap_to"), ("import", "remap_from")):
        entries = config.get(direction+"s", [])
        if not isinstance(entries, list):
            raise ValueError(f"{direction}s must be an array of topics")
        for index, entry in enumerate(entries):
            if not isinstance(entry, dict) or "topic" not in entry:
                raise ValueError(f"{direction}s[{index}] must be an object with a topic field")
            unknown = entry.keys() - {"topic", remap_key, "type", "always_on", "qos"}
            if unknown:
                raise ValueError(f"unknown {direction} fields: {sorted(unknown)}")
            qos = entry.get("qos", {})
            if not isinstance(qos, dict):
                raise ValueError("route qos must be an object")
            local = entry["topic"]
            network = entry.get(remap_key, local)
            source, destination = (local, network) if direction == "export" else (network, local)
            routes.append(route_arguments(dict(defaults, direction=direction,
                source=source, destination=destination,
                type=entry.get("type","?"), always_on=entry.get("always_on",False),
                qos={**base_qos, **qos})))
    if not routes:
        raise ValueError("configure at least one export or import")
    return routes
