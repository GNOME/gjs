/* -*- indent-tabs-mode: nil; js-indent-level: 4 -*- */
/* global debuggee, quit, loadNative, readline, uneval, launchFile */
// SPDX-License-Identifier: MPL-2.0
// SPDX-FileCopyrightText: 2026 Angelo Verlain

const { print, printerr } = loadNative('_print');
const Encoding = loadNative('_encodingNative');

function encode(str) {
    if (str == '') return new Uint8Array();
    return Encoding.encode(str, 'utf8');
}

const STDIN = 0;
const input = openInputStream(STDIN);

function readMessage() {
    let contentLength = null;

    while (true) {
        const line = readLine(input);
        if (line == '' || line == '\r') break;

        const match = /^Content-Length: (\d+)\r$/i.exec(line);
        if (match !== null) {
            contentLength = parseInt(match[1]);
            break;
        }
    }

    const body = readBytes(input, contentLength + 2);
    return JSON.parse(body);
}

let REQUEST_SEQ = 0;
let pendingLaunchPath = null;

function sendMessage(message) {
    const newSeq = ++REQUEST_SEQ;

    const body = JSON.stringify({ seq: newSeq, ...message });
    // Add extra length bytes for \r\n at the end of body. print() auto-adds \n
    print(`Content-Length: ${encode(body).length + 2}\r\n\r\n${body}\r`);
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
    configurationDone(args, seq) {
        sendResponse('configurationDone', undefined, seq);

        if (pendingLaunchPath) {
            try {
                launchFile(pendingLaunchPath);

                sendEvent('exited', { exitCode: 0 });
                sendEvent('terminated');
            } catch (e) {
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
    threads(args, seq) {
        sendResponse('threads', { threads: [{ id: 0, name: 'main' }] }, seq);
    },
    continue(args, seq) {
        sendResponse('continue', { allThreadsContinued: true }, seq);
        paused = false;
    },
    stackTrace(args, seq) {
        sendResponse('stackTrace', { stackFrames: [], totalFrames: 0 }, seq);
    },
    pause(args, seq) {
        sendResponse('pause', undefined, seq);
        pause();
    },
    setBreakpoints(args, seq) {
        /***
        "source": {
           "name": "test.js",
           "path": "/home/alien/sites/gsoc/gjs/test.js"
         },
         "breakpoints": [
           {
             "line": 7
           }
         ],
         "sourceModified": false
         */
        printerr(JSON.stringify(args));
        // const scripts = dbg.findScripts({ url: `file://${args.source.path}` });
        const scripts = dbg.findScripts();
        printerr("Found n scripts:" + JSON.stringify(scripts))

        scripts.forEach((script, i) => {
            printerr(i, script.url);
            printerr(i, script.startLine);
        })

        sendResponse('setBreakpoints', { breakpoints: [] }, seq);
    },
};

function _handleRequest() {
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

function handleRequests(shouldContinue = () => true) {
    while (shouldContinue()) {
        if (!_handleRequest()) break;
    }
}

function pause() {
    paused = true;
    handleRequests(() => paused);
}

// State

let paused = false;

function onInitialEnterFrame() {
    // printerr("entered frame", frame?.callee.name);
    dbg.onEnterFrame = undefined;

    sendEvent('stopped', {
        reason: 'entry',
        threadId: 0,
        allThreadsStopped: true,
    });

    pause();
}

// Debugger

const dbg = new Debugger();

const debuggeeGlobalWrapper = dbg.addDebuggee(debuggee);

try {
    handleRequests();
} catch (error) {
    printerr(error);
    quit(1);
}

quit(0);
