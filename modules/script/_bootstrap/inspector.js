/* -*- indent-tabs-mode: nil; js-indent-level: 4 -*- */
/* global debuggee, quit, loadNative, readline, uneval, launchFile */
// SPDX-License-Identifier: MPL-2.0
// SPDX-FileCopyrightText: 2026 Angelo Verlain

const { print } = loadNative('_print');
const Encoding = loadNative('_encodingNative');

const { Gio, GioUnix } = debuggee.imports.gi;

function encode(str) {
    if (str == '') return new Uint8Array();
    return Encoding.encode(str, 'utf8');
}

const input = Gio.DataInputStream.new(GioUnix.InputStream.new(0, false));
const output = GioUnix.OutputStream.new(1, false);

function readMessage() {
    let contentLength = null;

    while (true) {
        const line = input.read_line_utf8(null)[0];

        if (line == '' || line == '\r') break;

        const match = /^Content-Length: (\d+)\r$/i.exec(line);
        if (match !== null) {
            contentLength = parseInt(match[1]);
            break;
        }
    }

    const bytes = input.read_bytes(contentLength + 2, null);

    return JSON.parse(bytes.toArray().toString().slice(2));
}

let REQUEST_SEQ = 0;
let pendingLaunchPath = null;

function sendMessage(message) {
    const newSeq = ++REQUEST_SEQ;

    const body = encode(JSON.stringify({ seq: newSeq, ...message }));
    const header = encode(`Content-Length: ${body.length}\r\n\r\n`);

    output.write_all(header, null);
    output.write_all(body, null);
    output.flush(null);
}

function sendResponse(command, body, requestSeq) {
    sendMessage({
        type: 'response',
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
        type: 'response',
        success: false,
        command,
        request_seq: requestSeq,
        body: { error },
    });
}

function sendEvent(type, body = {}) {
    sendMessage({
        type: 'event',
        event: type,
        body,
    });
}

const handlers = {
    initialize(args, seq) {
        sendResponse(
            'initialize',
            { supportsConfigurationDoneRequest: true },
            seq,
        );
        sendEvent('initialized');
    },
    launch(args, seq) {
        const cwd = args.cwd || '.';
        const filePath = cwd + '/' + args.program;
        print(`[LAUNCH] ${filePath}`);

        if (args.stopOnEntry) {
            dbg.onEnterFrame = onInitialEnterFrame;
        }

        pendingLaunchPath = filePath;
        sendResponse('launch', undefined, seq);
    },
    setExceptionBreakpoints(args, seq) {
        /**
        TODO: Currently no-op

        {
          "filters": [],
          "filterOptions": []
        }
        */
        sendResponse('setExceptionBreakpoints', undefined, seq);
    },
    setBreakpoints(args, seq) {
        sendResponse('setExceptionBreakpoints', undefined, seq);
    },
    configurationDone(args, seq) {
        sendResponse('configurationDone', undefined, seq);

        if (pendingLaunchPath) {
            try {
                launchFile(pendingLaunchPath);

                sendEvent('exited', { exitCode: 0 });
                sendEvent('terminated');
            } catch (e) {
                print(`[ERROR] ${e}`);
                sendEvent('output', {
                    category: 'stderr',
                    output: `Error: ${e}\n`,
                });
                sendEvent('exited', { exitCode: 1 });
                sendEvent('terminated');
                quit(1);
            }
            pendingLaunchPath = null;
        }
    },
    disconnect(args, seq) {
        sendResponse('disconnect', undefined, seq);
        quit(0);
    },
    attach(args, seq) {
        sendErrorResponse(
            'attach',
            newMessage(0, 'GJS does not support attach mode'),
            seq,
        );
    },
};

function handleRequests() {
    const request = readMessage();
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

try {
    for (;;) {
        if (!handleRequests()) break;
    }
} catch (error) {
    print(error);
    quit(1);
}

quit(0);
