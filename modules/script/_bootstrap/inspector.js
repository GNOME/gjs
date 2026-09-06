// oxlint-disable no-unused-vars
/* -*- indent-tabs-mode: nil; js-indent-level: 4 -*- */
/* global debuggee, quit, loadNative, readline, uneval, launchFile */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
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

/**
 * @typedef {{ url: string, line: number, column: number } | null} Location
 * @typedef {"all" | "uncaught"} ExceptionBreakpointFilter
 */

const STATE = {
    messageIdSeq: 0,
    paused: false,
    /** @type {(Debugger.Object | Debugger.Environment)[]} */
    objects: [],
    /** @type {Debugger.Script[]} */
    scripts: [],
    breakpointIdSeq: 0,
    /** @type {Map<string, BreakpointEntry[]>} */
    pendingBreakpoints: new Map(),
    requestIdSeq: 0,
    /** @type {string | null} */
    pendingLaunchPath: null,
    /** @type {Array<() => void>} */
    cleanups: [],
    /** @type {Location | null} */
    lastLocation: null,
    /** @type {Array<ExceptionBreakpointFilter>} */
    exceptionBreakpointFilters: [],
    // Some clients (e.g. Zed) use an extra \r\n when sending/receiving messages
    extraCrlf: true
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
    let sawHeader = false;

    while (true) {
        const line = readLine(input);
        if (line == '' || line == '\r') {
            if (sawHeader) break;
            continue;
        }

        const match = /^Content-Length: (\d+)\r$/i.exec(line);
        if (match !== null) {
            contentLength = parseInt(match[1]);
            sawHeader = true;
            break;
        }
    }

    let body = readBytes(input, contentLength);

    if (body.startsWith('\r\n')) {
        STATE.extraCrlf = true;
        // remove the `\r\n` prefix if it was sent, and instead get the remaining body
        body = body.slice(2) + readBytes(input, 2);
    }

    return JSON.parse(body);
}

/**
 * @param {Omit<DAP.ProtocolMessage, 'seq'>} message
 */
function sendMessage(message) {
    const newSeq = ++STATE.requestIdSeq;

    const body = JSON.stringify({ seq: newSeq, ...message });
    // Add extra length bytes for \r\n at the end of body. print() auto-adds \n
    print(
        `Content-Length: ${encode(body).length + (STATE.extraCrlf ? 2 : 0)}\r\n\r\n${body}\r`,
    );
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
            // supportsSteppingGranularity: true,
            exceptionBreakpointFilters: [
                {
                    filter: 'all',
                    label: 'Caught Exceptions',
                    default: false,
                    description:
                        "Breaks on all throw errors, even if they're caught later.",
                },
                {
                    filter: 'uncaught',
                    label: 'Uncaught Exceptions',
                    default: false,
                    description:
                        'Breaks only on errors or promise rejections that are not handled.',
                },
            ],
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
            setUntilNextRequest(dbg, 'onEnterFrame', onInitialEnterFrame);
        }

        STATE.pendingLaunchPath = filePath;
        sendResponse(seq, 'launch');
    },
    /**
     * @param {{filters: Array<ExceptionBreakpointFilter>}} args
     */
    setExceptionBreakpoints(seq, args) {
        STATE.exceptionBreakpointFilters = args.filters ?? [];
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
        resume();
    },
    stackTrace(seq) {
        const newestFrame = dbg.getNewestFrame();
        const dapFrames = toDapStackFrames(newestFrame);
        sendResponse(seq, 'stackTrace', {
            stackFrames: dapFrames,
            totalFrames: dapFrames.length,
        });
    },
    pause(seq) {
        sendResponse(seq, 'pause');
        pause('explicit');
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

        const url = pathToUrl(args.source.path);

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
    scopes(seq, _args) {
        const newestFrame = dbg.getNewestFrame();
        const scopes = toDapScopes(newestFrame?.environment ?? null);
        sendResponse(seq, 'scopes', {
            scopes,
        });
    },
    /**
     * @param {{ variablesReference: number }} args
     */
    variables(seq, args) {
        let variables = [];

        if (!args.variablesReference) {
            let environment = dbg.getNewestFrame()?.environment ?? null;

            if (!environment) {
                variables = [];
            } else {
                variables = toDapVariables(environment);
            }
        } else {
            const obj = STATE.objects[args.variablesReference - 1];

            if (!obj) {
                variables = [];
            } else {
                variables = toDapVariables(obj);
            }
        }

        sendResponse(seq, 'variables', {
            variables: variables,
        });
    },
    /**
     *
     * @param {number} seq
     * @param {{granularity?: DAP.SteppingGranularity}} args
     */
    next(seq, args) {
        sendResponse(seq, 'next');

        resume();

        const newestFrame = dbg.getNewestFrame();

        if (!newestFrame) return;

        setUntilNextRequest(newestFrame, 'onStep', onStepped);

        setUntilNextRequest(newestFrame, 'onPop', function () {
            this.onPop = undefined;
            if (this.older) {
                setUntilNextRequest(this.older, 'onStep', onStepped);
            } else {
                resume();
            }
        });
    },
    /**
     *
     * @param {number} seq
     * @param {{granularity?: DAP.SteppingGranularity}} args
     */
    stepIn(seq, args) {
        sendResponse(seq, 'stepIn');

        resume();

        const newestFrame = dbg.getNewestFrame();

        if (!newestFrame) return;

        // whatever happens first

        setUntilNextRequest(
            dbg,
            'onEnterFrame',
            (/** @type {Debugger.Frame} */ newFrame) => {
                setUntilNextRequest(newFrame, 'onStep', onStepped);
            },
        );

        setUntilNextRequest(newestFrame, 'onStep', onStepped);

        setUntilNextRequest(newestFrame, 'onPop', function () {
            this.onPop = undefined;
            if (this.older) {
                setUntilNextRequest(this.older, 'onStep', onStepped);
            } else {
                resume();
            }
        });

        return;
    },
    /**
     * @param {number} seq
     * @param {{granularity?: DAP.SteppingGranularity}} args
     */
    stepOut(seq, args) {
        sendResponse(seq, 'stepOut');

        resume();

        const newestFrame = dbg.getNewestFrame();

        if (!newestFrame) return;

        setUntilNextRequest(newestFrame, 'onPop', function () {
            this.onPop = undefined;
            if (this.older) {
                setUntilNextRequest(this.older, 'onStep', onStepped);
            } else {
                resume();
            }
        });

        return;
    },
    /**
     * @param {number} seq
     * @param {{source?:{path?:string;sourceReference?:number};sourceReference:number}} args
     */
    source(seq, args) {
        const sourceReference =
            args.source?.sourceReference ?? args.sourceReference;

        /** @type {Debugger.Script | undefined} */
        let foundScript;

        if (sourceReference) {
            foundScript = STATE.scripts[sourceReference - 1];
        } else if (args.source?.path) {
            [foundScript] = dbg.findScripts({
                url: pathToUrl(args.source.path),
            });
        }

        if (foundScript) {
            sendResponse(seq, 'source', {
                content: foundScript.source.text.slice(
                    foundScript.sourceStart,
                    foundScript.sourceStart + foundScript.sourceLength,
                ),
            });

            return;
        }

        sendResponse(seq, 'source', {});
    },
};

/**
 * @param {Debugger.Frame} frame
 * @returns {Location | null}
 */
function getFrameLocation(frame) {
    if (!frame.script || !frame.offset) return null;
    const { lineNumber, columnNumber } = frame.script.getOffsetLocation(
        frame.offset,
    );
    return { url: frame.script.url, line: lineNumber, column: columnNumber };
}

/**
 * @param {Location} location1
 * @param {Location} location2
 * @returns {boolean}
 */
function isSameLocation(location1, location2) {
    return (
        location1?.url === location2?.url && location1?.line === location2?.line
        // TODO: here we are assuming line granularity
        // && location1?.column === location2?.column
    );
}

/**
 * @this {Debugger.Frame}
 */
function onStepped() {
    if (!isDebugeeFrame(this)) return;

    const location = getFrameLocation(this);

    if (isSameLocation(location, STATE.lastLocation)) return;
    STATE.lastLocation = location;

    pause('step');
}

/**
 * @param {Debugger.Frame} frame
 * @returns {boolean}
 */
function isDebugeeFrame(frame) {
    return !!frame.script && !!frame.offset;
}

/**
 * @param {Debugger.Frame} frame
 * @returns {number | null}
 */
function getFrameLine(frame) {
    if (!frame.script || !frame.offset) return null;
    // 1-based
    return frame.script.getOffsetLocation(frame.offset).lineNumber;
}

/**
 * @param {Debugger.Frame} frame
 * @returns {number | null}
 */
function getFrameColumn(frame) {
    if (!frame.script || !frame.offset) return null;
    // already 1-based
    return frame.script.getOffsetLocation(frame.offset).columnNumber;
}

/**
 * @param {Debugger.Environment | Debugger.Object} environment
 * @returns {any}
 */
function toDapVariables(environment) {
    if (environment instanceof Debugger.Environment) {
        return environment.names().map((name) => {
            let variable;
            try {
                variable = environment.getVariable(name);
            } catch {
                variable = undefined;
            }

            return toDapVariable(name, variable);
        });
    } else {
        const variables = [
            ...environment.getOwnPropertyNames(),
            ...environment.getOwnPropertySymbols(),
        ];
        return variables.map((name) => {
            const variable = environment.getProperty(name)?.return;

            return toDapVariable(name.toString(), variable);
        });
    }
}

/**
 * @param {string} name
 * @param {any} variable
 * @returns {{ name: string, type: string, value: any, evaluateName: string; variablesReference: number; }}
 */
function toDapVariable(name, variable) {
    /** @type {{type: string, value: any}} */
    let variableDescription = {
        type: 'unknown',
        value: '<unknown>',
    };
    /** @type {number} */
    let variablesReference = 0;

    switch (typeof variable) {
        case 'bigint':
            variableDescription = {
                type: 'bigint',
                value: variable.toString(),
            };
            break;
        case 'number':
            variableDescription = {
                type: 'number',
                value: variable.toString(),
            };
            break;
        case 'string':
            variableDescription = {
                type: 'string',
                value: variable,
            };
            break;
        case 'undefined':
            variableDescription = { type: 'undefined', value: 'undefined' };
            break;
        case 'boolean':
            variableDescription = {
                type: 'boolean',
                value: variable.toString(),
            };
            break;
        case 'symbol':
            variableDescription = {
                type: 'symbol',
                value: variable.toString(),
            };
            break;
        case 'object':
            if (variable === null)
                variableDescription = { type: 'null', value: 'null' };
            else if (variable instanceof Debugger.Object) {
                variablesReference = STATE.objects.push(variable);
                if (variable.isProxy) {
                    variableDescription = {
                        type: 'object',
                        value: '<proxy>',
                    };
                }
                if (variable.isPromise) {
                    variableDescription = {
                        type: 'object',
                        value: '<promise>',
                    };
                } else if (variable.isArrowFunction) {
                    variableDescription = {
                        type: 'object',
                        value: `(${variable.parameterNames}) => { ... }`,
                    };
                } else if (variable.isGeneratorFunction) {
                    variableDescription = {
                        type: 'object',
                        value: `f* (${variable.parameterNames}) { ... }`,
                    };
                } else if (variable.isAsyncFunction) {
                    variableDescription = {
                        type: 'object',
                        value: `async f (${variable.parameterNames}) { ... }`,
                    };
                } else if (variable.isBoundFunction || variable.callable) {
                    variableDescription = {
                        type: 'object',
                        value: `f (${variable.parameterNames}) { ... }`,
                    };
                } else {
                    variableDescription = {
                        type: 'object',
                        value:
                            variable.displayName ??
                            variable.name ??
                            '<unknown>',
                    };
                }
            } else if ('optimizedOut' in variable)
                variableDescription = {
                    type: 'object',
                    value: '<optimized out>',
                };
            else variableDescription = { type: 'object', value: '<unknown>' };
            break;
    }

    return {
        name,
        evaluateName: name,
        variablesReference,
        ...variableDescription,
    };
}

/**
 *
 * @param {Debugger.Environment | null} environment
 * @param {number} id
 */
function toDapScope(environment, id = 0) {
    if (!environment) return null;

    let script, object;
    if (environment.type !== 'declarative') {
        script ??= environment.object?.script;
        object ??= environment.object;
    }
    script ??= environment.calleeScript;

    let location;
    if (script) {
        const offsets = script.getLineOffsets(1);
        for (const o of offsets) {
            const c = script.getOffsetLocation(o);
            if (c) {
                location = c;
                break;
            }
        }
    }

    const name =
        script?.displayName ??
        object?.displayName ??
        object?.name ??
        object?.class;
    const kind = environment.scopeKind || environment.type;

    const displayName = kind ? (name ? `${kind}: ${name}` : kind) : 'Unknown';

    return {
        name: displayName,
        expensive: true,
        variablesReference: STATE.objects.push(environment),
        line: location?.lineNumber ?? 0,
        column: location?.columnNumber ?? 0,
    };
}

/**
 * @param {Debugger.Environment | null} environment
 */
function toDapScopes(environment) {
    if (!environment) return null;

    const scopes = [];

    let i = 1;
    while (environment) {
        const scope = toDapScope(environment, i);
        if (!scope) break;
        scopes.push(scope);
        environment = environment.parent;
        i++;
    }

    return scopes;
}

/**
 * @param {Debugger.Frame | null} frame
 */
function toDapStackFrame(frame) {
    if (!frame?.script) return null;

    const url = frame.script.url;

    let sourceReference = 0,
        path;

    if (!url.startsWith('file://')) {
        sourceReference = STATE.scripts.push(frame.script);
    } else {
        // converting from file:///path/to/script.js to /path/to/script.js
        path = url.slice(7);
    }

    return {
        id: frame.depth ?? 0,
        name: frame.script.displayName ?? '<anonymous>',
        line: getFrameLine(frame) ?? 2,
        column: getFrameColumn(frame) ?? 0,
        source: {
            name: url,
            path,
            sourceReference,
        },
        presentationHint: 'normal',
        // canRestart: frame.onStack,
        canRestart: false,
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

function handleRequests(shouldContinueHandlingRequests = () => true) {
    while (shouldContinueHandlingRequests()) {
        STATE.cleanups.forEach((cleanup) => cleanup());
        STATE.cleanups.length = 0;

        if (!_handleRequest()) break;
    }
}

function resume() {
    STATE.paused = false;
    STATE.objects.length = 0;
    STATE.scripts.length = 0;
    // STATE.lastLocation = null;
}

/**
 *
 * @param {string} reason
 * @param {Record<string, unknown>} stopArgs
 */
function pause(reason, stopArgs = {}) {
    sendEvent('stopped', {
        reason,
        threadId: 0,
        allThreadsStopped: true,
        ...stopArgs,
    });

    STATE.paused = true;
    handleRequests(() => {
        return STATE.paused;
    });
    sendEvent('continued', { threadId: 0, allThreadsContinued: true });
}

// DEBUGGER API handlers

function onInitialEnterFrame() {
    pause('entry');
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
 *
 * @template T
 * @param {T} obj
 * @param {keyof T} key
 * @param {T[keyof T]} handler
 */
function setUntilNextRequest(obj, key, handler) {
    const saved = obj[key];
    obj[key] = handler;
    STATE.cleanups.push(() => {
        obj[key] = saved;
    });
}

/**
 * @param {string} path
 */
function pathToUrl(path) {
    return path.includes('://') ? path : `file://${path}`;
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
        pause('breakpoint', { hitBreakpointIds: [this.entry.id] });
    }
}

// Debugger

const dbg = new Debugger();

dbg.onNewScript = (/** @type {Debugger.Script} */ script) => {
    resolveBreakpointsForUrl(script.url);
};

dbg.onDebuggerStatement = function () {
    pause('instruction breakpoint');
};

/**
 * @param {Debugger.Frame | null} frame
 * @returns {boolean}
 */
function willBeCaught(frame) {
    while (frame) {
        if (frame.script?.isInCatchScope(frame.offset ?? undefined))
            return true;
        frame = frame.older;
    }
    return false;
}

dbg.onExceptionUnwind = function (frame, value) {
    const willBeCaughtResult = willBeCaught(frame);

    /** @type {ExceptionBreakpointFilter} */
    const check = willBeCaughtResult ? 'all' : 'uncaught';

    printerr(
        'exception unwind',
        willBeCaughtResult,
        check,
        JSON.stringify(STATE.exceptionBreakpointFilters),
    );
    if (STATE.exceptionBreakpointFilters.includes(check)) {
        pause('exception');
    }
};

const debuggeeGlobalWrapper = dbg.addDebuggee(debuggee);

try {
    handleRequests();
} catch (error) {
    printerr(error);
    quit(1);
}

quit(0);
