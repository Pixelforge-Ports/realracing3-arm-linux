#!/usr/bin/env python3
"""End-to-end smoke test for the stdio MCP server."""

import json
import os
from pathlib import Path
import subprocess
import sys
import time


HERE = Path(__file__).resolve().parent


def main() -> int:
    server = subprocess.Popen(
        [sys.executable, str(HERE / "mcp_server.py")],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        text=True,
    )
    assert server.stdin and server.stdout
    serial = 0

    def call(method: str, params: dict) -> dict:
        nonlocal serial
        serial += 1
        message = {"jsonrpc": "2.0", "id": serial, "method": method, "params": params}
        server.stdin.write(json.dumps(message) + "\n")
        server.stdin.flush()
        response = json.loads(server.stdout.readline())
        assert response["id"] == serial, response
        return response["result"]

    try:
        init = call(
            "initialize",
            {
                "protocolVersion": "2025-06-18",
                "capabilities": {},
                "clientInfo": {"name": "smoke-test", "version": "1"},
            },
        )
        assert init["serverInfo"]["name"] == "realracing3-emulator"

        started = call(
            "tools/call",
            {"name": "start_emulator", "arguments": {"rebuild": True}},
        )
        assert not started.get("isError"), started
        # Let the first content/scene load settle before asserting pixels.
        time.sleep(5)
        tapped = call(
            "tools/call", {"name": "click", "arguments": {"x": 320, "y": 240}}
        )
        assert not tapped.get("isError"), tapped
        call("tools/call", {"name": "press_control", "arguments": {"control": "start"}})
        call("tools/call", {"name": "press_control", "arguments": {"control": "a"}})

        held = call(
            "tools/call",
            {
                "name": "set_stick",
                "arguments": {"stick": "right", "x": 1.0, "y": 0.0},
            },
        )
        assert not held.get("isError"), held
        # Leave the stick held across rendered frames so the game can sample
        # the motion event; an immediate release can collapse both commands
        # into one control tick.
        time.sleep(1)
        released = call(
            "tools/call",
            {
                "name": "set_stick",
                "arguments": {"stick": "right", "x": 0.0, "y": 0.0},
            },
        )
        assert not released.get("isError"), released

        capture = call(
            "tools/call", {"name": "capture_screen", "arguments": {}}
        )
        assert not capture.get("isError"), capture
        image = next(item for item in capture["content"] if item["type"] == "image")
        import base64
        assert base64.b64decode(image["data"]).startswith(b"\x89PNG\r\n\x1a\n")
        time.sleep(2)
        capture2 = call(
            "tools/call", {"name": "capture_screen", "arguments": {}}
        )
        assert not capture2.get("isError"), capture2
        image2 = next(item for item in capture2["content"] if item["type"] == "image")
        moving = image2["data"] != image["data"]
        if os.environ.get("REALRACING3_REQUIRE_MOTION") == "1":
            assert moving, "two captures were identical"

        stopped = call(
            "tools/call", {"name": "stop_emulator", "arguments": {}}
        )
        assert not stopped.get("isError"), stopped
        print("MCP smoke test PASS: initialize, start, stick, PNG captures, "
              f"motion={'yes' if moving else 'static'}, stop")
        return 0
    finally:
        if server.poll() is None:
            server.stdin.close()
            server.wait(timeout=20)


if __name__ == "__main__":
    raise SystemExit(main())
