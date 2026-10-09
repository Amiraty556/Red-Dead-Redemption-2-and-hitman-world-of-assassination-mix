#!/usr/bin/env python3
"""Call a Melty MCP tool. Token comes from $MELTY_TOKEN only (never stored here).

Usage: MELTY_TOKEN=... python3 tools/melty.py <tool_name> ['<args-json-file>' | -]
Prints the tool result content (and any isError flag).
"""
import json
import os
import subprocess
import sys

URL = "https://melty.gg/api/mcp"


def call(tool: str, args: dict) -> dict:
    token = os.environ.get("MELTY_TOKEN")
    if not token:
        sys.exit("MELTY_TOKEN is not set")
    payload = {
        "jsonrpc": "2.0",
        "id": 1,
        "method": "tools/call",
        "params": {"name": tool, "arguments": args},
    }
    p = subprocess.run(
        [
            "curl", "-sSf", URL,
            "-H", f"Authorization: Bearer {token}",
            "-H", "content-type: application/json",
            "-H", "accept: application/json, text/event-stream",
            "-d", json.dumps(payload),
        ],
        capture_output=True, text=True,
    )
    if p.returncode != 0:
        sys.exit(f"curl failed: {p.stderr}")
    return json.loads(p.stdout)


def main() -> None:
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    tool = sys.argv[1]
    if len(sys.argv) == 2:
        args: dict = {}
    elif sys.argv[2] == "-":
        args = json.load(sys.stdin)
    else:
        with open(sys.argv[2]) as f:
            args = json.load(f)
    resp = call(tool, args)
    if "error" in resp:
        print("JSON-RPC ERROR:", json.dumps(resp["error"], indent=1))
        sys.exit(1)
    result = resp.get("result", {})
    if result.get("isError"):
        print("TOOL isError=true")
    for c in result.get("content", []):
        if c.get("type") == "text":
            print(c["text"])
    if not result.get("content"):
        print(json.dumps(resp, indent=1))


if __name__ == "__main__":
    main()
