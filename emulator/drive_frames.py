#!/usr/bin/env python3
"""Drive the emulator via MCP: long warm-up, periodic captures, input nudges.

Prints one line per capture (elapsed, sha1 head, nonblack estimate via PNG size
variance) and saves every distinct frame to OUTDIR so a human can look at the
sequence. Exit 0 if at least MIN_DISTINCT distinct frames were observed.
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
TOTAL_SECS = int(os.environ.get("RR3_DRIVE_SECS", "240"))
PERIOD = int(os.environ.get("RR3_DRIVE_PERIOD", "15"))
MIN_DISTINCT = 3


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

    seen = {}
    try:
        call("initialize", {"protocolVersion": "2025-06-18", "capabilities": {},
                            "clientInfo": {"name": "drive", "version": "1"}})
        r = call("tools/call", {"name": "start_emulator",
                                "arguments": {"rebuild": False}})
        assert not r.get("isError"), r
        t0 = time.time()
        tick = 0
        while time.time() - t0 < TOTAL_SECS:
            time.sleep(PERIOD)
            tick += 1
            # Alternate nudges: tap center, then START, then A.
            nudge = ["click", "start", "a"][tick % 3]
            if nudge == "click":
                call("tools/call", {"name": "click",
                                    "arguments": {"x": 320, "y": 240}})
            else:
                call("tools/call", {"name": "press_control",
                                    "arguments": {"control": nudge}})
            cap = call("tools/call", {"name": "capture_screen", "arguments": {}})
            if cap.get("isError"):
                print(f"[{int(time.time()-t0):4d}s] capture ERROR: {cap}")
                continue
            img = next(i for i in cap["content"] if i["type"] == "image")
            raw = base64.b64decode(img["data"])
            h = hashlib.sha1(raw).hexdigest()[:10]
            first = h not in seen
            if first:
                seen[h] = tick
                (OUTDIR / f"frame-{tick:02d}-{h}.png").write_bytes(raw)
            print(f"[{int(time.time()-t0):4d}s] nudge={nudge:5s} sha={h} "
                  f"{'NEW' if first else 'repetido'} ({len(raw)} bytes)")
        call("tools/call", {"name": "stop_emulator", "arguments": {}})
    finally:
        if server.poll() is None:
            server.stdin.close()
            try:
                server.wait(timeout=20)
            except subprocess.TimeoutExpired:
                server.kill()

    print(f"distintos={len(seen)} -> {OUTDIR}")
    return 0 if len(seen) >= MIN_DISTINCT else 1


if __name__ == "__main__":
    raise SystemExit(main())
