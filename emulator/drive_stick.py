#!/usr/bin/env python3
"""Same warm-up as drive_frames.py, then feed the LEFT ANALOG STICK.

The tutorial stops on "Press the left analog stick left to steer left", which
drive_frames.py can never satisfy: it only sends click/start/a. This driver
replays the button nudges until the tutorial is up, then holds the left stick
left/right so we can see whether the engine consumes SetJoystickValue and
advances the tutorial step.

Set REALRACING3_MOTION_TRACE_ALL=1 when the question is how many events the
bridge dispatched: the "input: rr3 joystick" trace is sampled by default (the
first MOVE and then every 30th, all STOPs), so counting those lines measures
the sampler, not the input.
"""

import base64
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time

HERE = Path(__file__).resolve().parent
OUTDIR = Path(os.environ.get("RR3_DRIVE_OUT", "/tmp/rr3-stick"))
WARMUP_SECS = int(os.environ.get("RR3_WARMUP_SECS", "330"))
STICK_SECS = int(os.environ.get("RR3_STICK_SECS", "180"))
PERIOD = 12


def main() -> int:
    OUTDIR.mkdir(parents=True, exist_ok=True)
    server = subprocess.Popen(
        [sys.executable, str(HERE / "mcp_server.py")],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True,
    )
    serial = 0

    def call(method: str, params: dict) -> dict:
        nonlocal serial
        serial += 1
        msg = {"jsonrpc": "2.0", "id": serial, "method": method, "params": params}
        server.stdin.write(json.dumps(msg) + "\n")
        server.stdin.flush()
        return json.loads(server.stdout.readline())["result"]

    def tool(name, **args):
        return call("tools/call", {"name": name, "arguments": args})

    seen = {}

    def shot(tick, tag, t0):
        cap = tool("capture_screen")
        if cap.get("isError"):
            print(f"[{int(time.time()-t0):4d}s] {tag}: capture ERROR", flush=True)
            return
        img = next(i for i in cap["content"] if i["type"] == "image")
        raw = base64.b64decode(img["data"])
        h = hashlib.sha1(raw).hexdigest()[:10]
        first = h not in seen
        if first:
            seen[h] = tick
            (OUTDIR / f"f{tick:02d}-{tag}-{h}.png").write_bytes(raw)
        print(f"[{int(time.time()-t0):4d}s] {tag:14s} sha={h} "
              f"{'NEW' if first else 'rep'} ({len(raw)} b)", flush=True)

    try:
        call("initialize", {"protocolVersion": "2025-06-18", "capabilities": {},
                            "clientInfo": {"name": "stick", "version": "1"}})
        r = tool("start_emulator", rebuild=False)
        assert not r.get("isError"), r
        t0 = time.time()
        tick = 0

        # Phase 1: identical to drive_frames.py, to reach the tutorial prompt.
        while time.time() - t0 < WARMUP_SECS:
            time.sleep(PERIOD)
            tick += 1
            nudge = ["click", "start", "a"][tick % 3]
            if nudge == "click":
                tool("click", x=320, y=240)
            else:
                tool("press_control", control=nudge)
            shot(tick, f"warm-{nudge}", t0)

        # Phase 2: the tutorial wants an axis, not a button. Hold the stick
        # hard left for a few seconds, release, then hard right, then release.
        t1 = time.time()
        seq = [("left", -1.0, 0.0), ("release", 0.0, 0.0),
               ("right", 1.0, 0.0), ("release", 0.0, 0.0),
               ("up", 0.0, -1.0), ("release", 0.0, 0.0)]
        i = 0
        while time.time() - t1 < STICK_SECS:
            tag, x, y = seq[i % len(seq)]
            i += 1
            tick += 1
            tool("set_stick", stick="left", x=x, y=y)
            time.sleep(PERIOD)
            shot(tick, f"stick-{tag}", t0)

        tool("stop_emulator")
    finally:
        if server.poll() is None:
            server.stdin.close()
            try:
                server.wait(timeout=20)
            except subprocess.TimeoutExpired:
                server.kill()

    print(f"distintos={len(seen)} -> {OUTDIR}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
