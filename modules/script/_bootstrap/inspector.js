/* -*- indent-tabs-mode: nil; js-indent-level: 4 -*- */
/* global debuggee, quit, loadNative, readline, uneval, getSourceMapRegistry */
// SPDX-License-Identifier: MPL-2.0
// SPDX-FileCopyrightText: 2026 Angelo Verlain

"use strict";

const { Gio, GLib, GioUnix } = debuggee.imports.gi;

function encode(str) {
    const encoder = new TextEncoder();
    return encoder.encode(str);
}

function decode(bytes) {
    const decoder = new TextDecoder();
    return decoder.decode(bytes);
}

function readMessage() {
    const input = Gio.DataInputStream.new(GioUnix.InputStream.new(0, false));

    let contentLength = null;

    while (true) {
        const [lineBytes] = input.read_line(null);

        if (lineBytes === null) {
            return null;
        }

        const line = decode(lineBytes)
            // handle carriage returns
            .replace(/\r$/, "");

        if (line == "") {
            break;
        }

        const match = /^Content-Length: (\d+)$/i.exec(line);

        if (match !== null) {
            contentLength = parseInt(match[1]);
            break;
        }
    }

    const bytes = input.read_bytes(contentLength, null);
    return JSON.parse(decode(bytes));
}

function sendMessage(message) {
    const body = encode(JSON.stringify(message));
    const header = `Content-Length: ${body.length}\r\n\r\n`;

    const output = GioUnix.OutputStream.new(1, false);

    output.write_all(encode(header), null);
    output.write_all(body, null);
    output.flush(null);
}

const request = readMessage();

if (request && request.type === "request" && request.command === "initialize") {
    sendMessage({
        seq: 1,
        type: "response",
        request_set: request.seq,
        success: true,
        command: "initialize",
        body: {
            supportsConfigurationDoneRequest: false,
        },
    });
}

quit(0);
