from pathlib import Path
import subprocess as sp


def link(peer, state):
    recipe = Path(__file__).parent
    image = sp.check_output(["docker", "inspect", "-f", "{{.Image}}", peer], text=True).strip()
    return sp.check_output(["docker", "run", "--rm", "-i", "--pull=never", "--network", "container:"+peer,
        "--cap-drop", "ALL", "--cap-add", "NET_ADMIN", "--memory", "96m", "--memory-swap", "96m",
        "--entrypoint", "python3", image, "-", state], input=(recipe/"link-state.py").read_text(), text=True).strip()
