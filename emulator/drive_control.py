#!/usr/bin/env python3
"""Drive Real Racing 3 with the full R36S control mapping, not just the stick.

drive_stick.py only steers, because that is all the tutorial asked for out
loud. This driver adds the two controls the port had no way of sending until
the trigger axes were wired: R2 (accel, ControllerAxis.RTRIGGER = ordinal 5)
and L2 (brake, LTRIGGER = ordinal 4). The throttle only exists as a player
input once the control scheme is a manual one - schemes 0, 5 and 6 store a
literal 1.0 in Car::ReadPlayerAccelerationInput and ignore the pad - so run
this with REALRACING3_CONTROL_METHOD=7 (Wheel_Manual, the game's "Wheel B").

Three phases:
  1. warm-up  - the same click/start/a nudges drive_frames.py uses, to get
                past the splash and into the tutorial;
  2. steering - hold the left stick fully left, then fully right, which is
                what tutorial states 2 and 3 wait for (front wheel angle past
                75% of its limit for 500 ms);
  3. driving  - hold R2 and keep steering, which is what a real player would
                do at "Just focus on steering for now".

Always run with REALRACING3_MOTION_TRACE_ALL=1 and REALRACING3_TUTORIAL_TRACE=1:
the first makes every dispatch visible instead of every 30th, the second prints
the tutorial's phase/state/timer and whether its GameTaskQueue gate is open.
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
OUTDIR = Path(os.environ.get("RR3_DRIVE_OUT", "/tmp/rr3-frames"))
WARMUP_SECS = int(os.environ.get("RR3_WARMUP_SECS", "330"))
STEER_SECS = int(os.environ.get("RR3_STEER_SECS", "96"))
DRIVE_SECS = int(os.environ.get("RR3_DRIVE_SECS", "200"))
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
            (OUTDIR / f"f{tick:03d}-{tag}-{h}.png").write_bytes(raw)
        print(f"[{int(time.time()-t0):4d}s] {tag:14s} sha={h} "
              f"{'NEW' if first else 'rep'} ({len(raw)} b)", flush=True)

    try:
        call("initialize", {"protocolVersion": "2025-06-18", "capabilities": {},
                            "clientInfo": {"name": "control", "version": "1"}})
        r = tool("start_emulator", rebuild=False)
        assert not r.get("isError"), r
        t0 = time.time()
        tick = 0

        while time.time() - t0 < WARMUP_SECS:
            time.sleep(PERIOD)
            tick += 1
            nudge = ["click", "start", "a"][tick % 3]
            if nudge == "click":
                tool("click", x=320, y=240)
            else:
                tool("press_control", control=nudge)
            shot(tick, f"warm-{nudge}", t0)

        t1 = time.time()
        steer = [("left", -1.0), ("centre", 0.0), ("right", 1.0), ("centre", 0.0)]
        i = 0
        while time.time() - t1 < STEER_SECS:
            tag, x = steer[i % len(steer)]
            i += 1
            tick += 1
            tool("set_stick", stick="left", x=x, y=0.0)
            time.sleep(PERIOD)
            shot(tick, f"steer-{tag}", t0)

        # Throttle on and left there: the engine stores an axis value and
        # samples it every frame, so one down edge is a held trigger.
        tool("hold_control", control="r2", down=True)
        t2 = time.time()
        drive = [("accel-left", -0.4), ("accel-straight", 0.0),
                 ("accel-right", 0.4), ("accel-straight", 0.0)]
        i = 0
        while time.time() - t2 < DRIVE_SECS:
            tag, x = drive[i % len(drive)]
            i += 1
            tick += 1
            tool("set_stick", stick="left", x=x, y=0.0)
            time.sleep(PERIOD)
            shot(tick, f"{tag}", t0)
        tool("hold_control", control="r2", down=False)

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
