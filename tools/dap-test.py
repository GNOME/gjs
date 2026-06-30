#!/usr/bin/env python3
import asyncio
import json
import re


async def main():
    proc = await asyncio.create_subprocess_exec(
        "_build/gjs-console",
        "--inspect",
        "test.js",
        stdin=asyncio.subprocess.PIPE,
        stdout=asyncio.subprocess.PIPE,
        stderr=asyncio.subprocess.PIPE,
    )

    def send_message(message):
        body = json.dumps(message, separators=(",", ":")).encode()
        header = f"Content-Length: {len(body)}\r\n\r\n".encode()
        print(">>> ", repr(header + body), flush=True)
        if proc.stdin is not None:
            proc.stdin.write(header + body)

    async def read_message():
        content_length = 0
        if proc.stdout is None:
            return None
        while True:
            line = await proc.stdout.readline()
            print("<<< header:", repr(line), flush=True)
            if line in (b"\r\n", b"\n"):
                break
            match = re.match(rb"^Content-Length: (\d+)", line)
            if match:
                content_length = int(match[1])
        body = await proc.stdout.readexactly(content_length)
        print("<<< body:", repr(body), flush=True)
        return json.loads(body)

    send_message(
        {
            "seq": 1,
            "type": "request",
            "command": "initialize",
            "arguments": {"clientID": "manual", "adapterId": "gjs"},
        }
    )

    await read_message()
    await read_message()

    send_message(
        {
            "seq": 2,
            "type": "request",
            "command": "launch",
            "arguments": {
                "type": "pwa-node",
                "request": "launch",
                "program": "test.js",
                "stopOnEntry": True,
                "cwd": "/home/alien/sites/gsoc/gjs",
                "console": "externalTerminal",
                "sourceMaps": True,
                "pauseForSourceMap": True,
                "sourceMapRenames": True,
            },
        }
    )

    await read_message()

    while True:
        await read_message()

    proc.terminate()


asyncio.run(main())
