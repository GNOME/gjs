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

function reply(request, success, body) {
    sendMessage({
        type: "response",
        request_set: request.request_seq,
        success,
        command: request.command,
        body,
    });
}

function ensureRequest(request, command) {
    if (request?.type !== "request" || request?.command !== command) quit(1);
}

const request = readMessage();
ensureRequest(request, "initialize");

reply(request, true, {
    supportsConfigurationDoneRequest: false,
});

const launchRequest = readMessage();
ensureRequest(launchRequest, "launch");

// State

function onEnterFrame(frame) {
    print("entered frame", frame?.callee.name);
    return;
}

// Debugger

const dbg = new Debugger();

const debuggeeGlobalWrapper = dbg.addDebuggee(debuggee);

dbg.onEnterFrame = onEnterFrame;

// const reportDO = debuggeeGlobalWrapper.getOwnPropertyDescriptor("report").value;

// reportDO.script.setBreakpoint(0, {
//     hit: function (frame) {
//         print("hit breakpoint in " + frame.callee.name);
//         print("what = " + frame.eval("what").return);
//     },
// });

// keep this
quit(0);
