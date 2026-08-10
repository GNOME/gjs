// oxlint-disable no-unused-vars
/* -*- indent-tabs-mode: nil; js-indent-level: 4 -*- */
/* global debuggee, quit, loadNative, readline, uneval, launchFile */
// SPDX-License-Identifier: MPL-2.0
// SPDX-FileCopyrightText: 2026 Angelo Verlain

// @ts-check
/// <reference path="./types.d.ts" />

const { print, printerr } = loadNative('_print');
const Encoding = loadNative('_encodingNative');

// DEBUGGER STATE

/**
 * @typedef {Object} BreakpointEntry
 * @property {number} id
 * @property {number} line
 * @property {boolean} verified
 */

const STATE = {
    messageIdSeq: 0,
    paused: false,
    breakpointIdSeq: 0,
    /** @type {Map<string, BreakpointEntry[]>} */
    pendingBreakpoints: new Map(),
    requestIdSeq: 0,
    /** @type {string | null} */
    pendingLaunchPath: null,
};

// UTILITY FUNCTIONS

/**
 * @param {string} str
 */
function encode(str) {
    if (str == '') return new Uint8Array();
    return Encoding.encode(str, 'utf8');
}

const STDIN = 0;
const input = openInputStream(STDIN);

function readMessage() {
    let contentLength = 0;

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

/**
 * @param {Omit<DAP.ProtocolMessage, 'seq'>} message
 */
function sendMessage(message) {
    const newSeq = ++STATE.requestIdSeq;

    const body = JSON.stringify({ seq: newSeq, ...message });
    // Add extra length bytes for \r\n at the end of body. print() auto-adds \n
    print(`Content-Length: ${encode(body).length + 2}\r\n\r\n${body}\r`);
}

/**
 * @param {number} requestSeq
 * @param {string} command
 * @param {any} [body]
 */
function sendResponse(requestSeq, command, body) {
    /** @type {Omit<DAP.Response, 'seq'>} */
    const response = {
        type: 'response',
        success: true,
        command,
        request_seq: requestSeq,
        body,
    };

    sendMessage(response);
}

/**
 * @param {string} format
 * @returns {DAP.Message}
 */
function newMessage(format) {
    return {
        id: STATE.messageIdSeq++,
        format,
    };
}

/**
 * @param {any} requestSeq
 * @param {string} command
 * @param {ReturnType<typeof newMessage>} error
 */
function sendErrorResponse(requestSeq, command, error) {
    /** @type {Omit<DAP.ErrorResponse, 'seq'>} */
    const response = {
        type: 'response',
        success: false,
        command,
        request_seq: requestSeq,
        body: { error },
    };
    sendMessage(response);
}

/**
 * @param {string} type
 * @param {any} body
 */
function sendEvent(type, body = {}) {
    /** @type {Omit<DAP.Event, 'seq'>} */
    const event = {
        type: 'event',
        event: type,
        body,
    };

    sendMessage(event);
}

// COMMAND HANDLERS

/** @type {Record<string, (seq: number, args: any) => void>} */
const handlers = {
    initialize(seq) {
        sendResponse(seq, 'initialize', {
            supportsConfigurationDoneRequest: true,
        });
        sendEvent('initialized');
    },
    /**
     * @param {{ cwd: string; program: string; stopOnEntry: boolean; }} args
     */
    launch(seq, args) {
        const cwd = args.cwd || '.';
        const filePath = cwd + '/' + args.program;

        if (args.stopOnEntry) {
            dbg.onEnterFrame = onInitialEnterFrame;
        }

        STATE.pendingLaunchPath = filePath;
        sendResponse(seq, 'launch');
    },
    /**
     * @param {{filters: any[]; filterOptions: any[]}} _args
     */
    setExceptionBreakpoints(seq, _args) {
        sendResponse(seq, 'setExceptionBreakpoints');
    },
    configurationDone(seq) {
        sendResponse(seq, 'configurationDone');

        if (STATE.pendingLaunchPath) {
            try {
                launchFile(STATE.pendingLaunchPath);

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
            } finally {
                STATE.pendingLaunchPath = null;
            }
        }
    },
    disconnect(seq) {
        sendResponse(seq, 'disconnect');
        quit(0);
    },
    attach(seq) {
        sendErrorResponse(
            seq,
            'attach',
            newMessage('GJS does not support attach mode'),
        );
    },
    threads(seq) {
        sendResponse(seq, 'threads', { threads: [{ id: 0, name: 'main' }] });
    },
    continue(seq) {
        sendResponse(seq, 'continue', { allThreadsContinued: true });
        STATE.paused = false;
    },
    stackTrace(seq) {
        const newestFrame = dbg.getNewestFrame();
        const dapFrames = toDapStackFrames(newestFrame);
        printerr(JSON.stringify(dapFrames, null, 2));
        sendResponse(seq, 'stackTrace', {
            stackFrames: dapFrames,
            totalFrames: dapFrames.length,
        });
    },
    pause(seq) {
        sendResponse(seq, 'pause');
        pause();
    },
    /**
     * @param {{ source: { path: string; name: string; }; breakpoints: {line: number}[]; }} args
     */
    setBreakpoints(seq, args) {
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

        const url = `file://${args.source.path}`;

        clearBreakpointsForUrl(url);

        const entries = (args.breakpoints || []).map(({ line }) => {
            return {
                id: ++STATE.breakpointIdSeq,
                line,
                verified: false,
            };
        });

        STATE.pendingBreakpoints.set(url, entries);

        // in case the script is already loaded, resolve the breakpoints immediately
        resolveBreakpointsForUrl(url);

        sendResponse(seq, 'setBreakpoints', {
            breakpoints: entries,
        });
    },
};

/**
 * @param {Debugger.Frame | null} frame
 */
function toDapStackFrame(frame) {
    if (!frame) return null;

    if (!frame.script) return null;

    const offset = frame.offset
        ? frame.script.getOffsetLocation(frame.offset)
        : null;

    printerr('frame props', offset);

    return {
        id: frame.depth ?? 0,
        name: frame.script.displayName ?? "helloworld",
        line: offset?.lineNumber,
        column: offset?.columnNumber,
        source: {
            name: frame.script.url,
            path: frame.script.url,
            sourceReference: 0,
        },
        presentationHint: 'normal',
        canRestart: frame.onStack,
    };
}

/**
 * @param {Debugger.Frame | null} frame
 */
function toDapStackFrames(frame) {
    const dapFrames = [];

    while (frame) {
        dapFrames.push(toDapStackFrame(frame));
        frame = frame.older;
    }

    return dapFrames.filter(Boolean);
}

function _handleRequest() {
    /** @type {DAP.Request | null} */
    const request = readMessage();
    if (request === null) return false;

    const handler = handlers[request.command];
    if (handler === undefined) {
        // TODO: use the error event
        throw new Error(`Unknown request command: ${request.command}`);
    }

    handler(request.seq, request.arguments);
    return true;
}

function handleRequests(shouldContinue = () => true) {
    while (shouldContinue()) {
        if (!_handleRequest()) break;
    }
}

function pause() {
    STATE.paused = true;
    handleRequests(() => STATE.paused);
}

// DEBUGGER API handlers

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

/**
 * @param {string} url
 */
function resolveBreakpointsForUrl(url) {
    const entries = STATE.pendingBreakpoints.get(url);
    if (!entries) return;

    const scripts = dbg.findScripts({ url });
    if (scripts.length === 0) return;

    entries.forEach((entry) => {
        if (entry.verified) return;

        const matches = scripts
            .map((script) => {
                return {
                    script,
                    offsets: script.getLineOffsets(entry.line),
                };
            })
            .filter(({ offsets }) => offsets.length !== 0);

        if (matches.length === 0) return;

        matches.forEach(({ script, offsets }) => {
            entry.verified = true;

            offsets.forEach((offset) =>
                script.setBreakpoint(offset, new BreakpointHandler(entry)),
            );

            sendEvent('breakpoint', {
                reason: 'changed',
                breakpoint: entry,
            });
        });
    });
}

/**
 * @param {string} url
 */
function clearBreakpointsForUrl(url) {
    dbg.findScripts({ url }).forEach((script) => script.clearAllBreakpoints());
    STATE.pendingBreakpoints.delete(url);
}

class BreakpointHandler {
    /**
     * @param {BreakpointEntry} entry
     */
    constructor(entry) {
        this.entry = entry;
    }

    /**
     * @param {Debugger.Frame} frame
     */
    hit(frame) {
        sendEvent('stopped', {
            reason: 'breakpoint',
            threadId: 0,
            allThreadsStopped: true,
            hitBreakpointIds: [this.entry.id],
        });
        pause();
    }
}

// Debugger

const dbg = new Debugger();

dbg.onNewScript = (/** @type {Debugger.Script} */ script) => {
    resolveBreakpointsForUrl(script.url);
};

const debuggeeGlobalWrapper = dbg.addDebuggee(debuggee);

try {
    handleRequests();
} catch (error) {
    printerr(error);
    quit(1);
}

quit(0);
