#!/usr/bin/env python3

# usage: tools/dap-test.py | build/gjs-console --inspect test.js
import json
import sys

message = {
    "seq": 1,
    "type": "request",
    "command": "initialize",
    "arguments": {"clientID": "manual", "adapterId": "gjs"},
}

body = json.dumps(message, separators=(",", ":")).encode()
sys.stdout.buffer.write(f"Content-Length: {len(body) + 4}\r\n\r\n".encode())
sys.stdout.buffer.write(body)
