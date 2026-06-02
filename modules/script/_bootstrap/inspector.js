/* -*- indent-tabs-mode: nil; js-indent-level: 4 -*- */
/* global debuggee, quit, loadNative, readline, uneval, getSourceMapRegistry */
// SPDX-License-Identifier: MPL-2.0
// SPDX-FileCopyrightText: 2026 Angelo Verlain

const { print } = loadNative("_print");

const { Gio, GioUnix } = debuggee.imports.gi;

function encode(str) {
    const encoder = new debuggee.TextEncoder();
    return encoder.encode(str);
}

function decode(bytes) {
    const decoder = new debuggee.TextDecoder();
    return decoder.decode(bytes);
}

const input = Gio.DataInputStream.new(GioUnix.InputStream.new(0, false));
const output = GioUnix.OutputStream.new(1, false);

function readMessage() {
    let contentLength = null;

    while (true) {
        const [lineBytes] = input.read_line(null);
        if (lineBytes === null) return null;

        const line = decode(lineBytes);
        if (line == "" || line == "\r") break;

        const match = /^Content-Length: (\d+)\r$/i.exec(line);
        if (match !== null) {
            contentLength = parseInt(match[1]);
            // break;
        }
    }

    const bytes = input.read_bytes(contentLength, null);
    return JSON.parse(decode(bytes));
}

let REQUEST_SEQ = 0;

function sendMessage(message) {
    const body = encode(
        JSON.stringify({ request_seq: REQUEST_SEQ++, message }),
    );
    const header = encode(`Content-Length: ${body.length}\r\n\r\n`);

    output.write_all(header, null);
    output.write_all(body, null);
    output.flush(null);
}

function sendResponse(command, body) {
    sendMessage({
        type: "response",
        request_set: REQUEST_SEQ,
        success: true,
        command,
        body,
    });
}

function sendEvent(type, body = {}) {
    sendMessage({
        type: "event",
        event: type,
        body,
    });
}

const handlers = {
    initialize() {
        // TODO: fill in our capabilities one by one
        sendResponse("initialize", {
            supportsConfigurationDoneRequest: false,
        });
    },
    launch() {
        /* TODO: currently, we don't really open the correct debuggee

        Request args are like this:

        {
          "type": "pwa-node",
          "request": "launch",
          "program": "test.js",
          "stopOnEntry": true,
          "cwd": "/home/alien/sites/gjs",
          "console": "externalTerminal",
          "sourceMaps": true,
          "pauseForSourceMap": true,
          "sourceMapRenames": true
        }

        */
        sendEvent("initialized");
        sendResponse("launch");
    },
    setExceptionBreakpoints(args) {
        /**
        TODO: Currently no-op

        {
          "filters": [],
          "filterOptions": []
        }
        */
        sendResponse("setExceptionBreakpoints");
    },
    configurationDone() {
        sendResponse("configurationDone");
    },
};

for (;;) {
    const request = readMessage();
    if (request === null) break;

    const handler = handlers[request.command];
    if (handler === undefined) {
        // TODO: use the error message
        throw new Error(`Unknown request command: ${request.command}`);
    }

    handler(request.arguments);
}

// State

function onEnterFrame(frame) {
    print("entered frame", frame?.callee.name);
    return;
}

// Debugger

const dbg = new Debugger();

const debuggeeGlobalWrapper = dbg.addDebuggee(debuggee);

dbg.onEnterFrame = onEnterFrame;

quit(0);
