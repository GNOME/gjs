/* -*- indent-tabs-mode: nil; js-indent-level: 4 -*- */
/* global debuggee, quit, loadNative, readline, uneval, loadFile */
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
            break;
        }
    }

    const bytes = input.read_bytes(contentLength + 2, null);
    print(`[${contentLength}][START]${decode(bytes)}[END]`);

    return JSON.parse(decode(bytes));
}

let REQUEST_SEQ = 0;
let pendingLaunchPath = null;

function sendMessage(message) {
    const newSeq = ++REQUEST_SEQ;

    const body = encode(
        JSON.stringify({ seq: newSeq, ...message }),
    );
    const header = encode(`Content-Length: ${body.length}\r\n\r\n`);

    output.write_all(header, null);
    output.write_all(body, null);
    output.flush(null);
}

function sendResponse(command, body, requestSeq) {
    sendMessage({
        type: "response",
        success: true,
        command,
        request_seq: requestSeq,
        body,
    });
}

function newMessage(id, format) {
    return {
        id,
        format,
    };
}

/**
 *
 * @param {string} command
 * @param {ReturnType<typeof newMessage>} error
 */
function sendErrorResponse(command, error, requestSeq) {
    sendMessage({
        type: "response",
        success: false,
        command,
        request_seq: requestSeq,
        body: { error },
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
    initialize(args, seq) {
        sendResponse("initialize",
            { supportsConfigurationDoneRequest: true },
            seq);
        sendEvent("initialized");
    },
    launch(args, seq) {
        const cwd = args.cwd || ".";
        const filePath = cwd + "/" + args.program;
        print(`[LAUNCH] ${filePath}`);

        pendingLaunchPath = filePath;
        sendResponse("launch", undefined, seq);
    },
    setExceptionBreakpoints(args, seq) {
        /**
        TODO: Currently no-op

        {
          "filters": [],
          "filterOptions": []
        }
        */
        sendResponse("setExceptionBreakpoints", undefined, seq);
    },
    setBreakpoints(args, seq) {
        sendResponse("setExceptionBreakpoints", undefined, seq);
    },
    configurationDone(args, seq) {
        sendResponse("configurationDone", undefined, seq);

        if (pendingLaunchPath) {
            try {
                launchFile(pendingLaunchPath);

                sendEvent("exited", { exitCode: 0 });
                sendEvent("terminated");
            } catch (e) {
                print(`[ERROR] ${e}`);
                sendEvent("output", {
                    category: "stderr",
                    output: `Error: ${e}\n`,
                });
                sendEvent("exited", { exitCode: 1 });
                sendEvent("terminated");
                quit(1);
            }
            pendingLaunchPath = null;
        }
    },
    disconnect(args, seq) {
        sendResponse("disconnect", undefined, seq);
        quit(0);
    },
    attach(args, seq) {
        sendErrorResponse(
            "attach",
            newMessage(0, "GJS does not support attach mode"),
            seq,
        );
    },
};

function handleRequests() {
    const request = readMessage();
    print("[REQUEST]" + request);
    if (request === null) return false;

    const handler = handlers[request.command];
    if (handler === undefined) {
        // TODO: use the error message
        throw new Error(`Unknown request command: ${request.command}`);
    }

    handler(request.arguments, request.seq);
    return true;
}

// State

function onInitialEnterFrame(frame) {
    // print("entered frame", frame?.callee.name);
    dbg.onEnterFrame = undefined;
    handleRequests();
    return;
}

// Debugger

const dbg = new Debugger();

const debuggeeGlobalWrapper = dbg.addDebuggee(debuggee);

// dbg.onEnterFrame = onInitialEnterFrame;

for (;;) {
    if (!handleRequests()) break;
}

quit(0);
