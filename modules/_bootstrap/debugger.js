/* global Debugger, debuggee, quit, readline, uneval */
/* -*- indent-tabs-mode: nil; js-indent-level: 4 -*-
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

/*
 * This is a simple command-line debugger for GJS programs. It is based on
 * jorendb, which is a toy debugger for shell-js programs included in the
 * SpiderMonkey source.
 *
 * To run it: gjs -d path/to/file.js
 * Execution will stop at debugger statements, and you'll get a prompt before
 * the first frame is executed.
 */

// Debugger state.
var focusedFrame = null;
var topFrame = null;
var debuggeeValues = {};
var nextDebuggeeValueIndex = 1;
var lastExc = null;
var options = {pretty: true};
var breakpoints = [undefined];  // Breakpoint numbers start at 1

// Cleanup functions to run when we next re-enter the repl.
var replCleanups = [];

// Convert a debuggee value v to a string.
function dvToString(v) {
    if (typeof v === 'undefined')
        return 'undefined';  // uneval(undefined) === '(void 0)', confusing
    if (v === null)
        return 'null';  // typeof null === 'object', so avoid that case
    return (typeof v !== 'object' || v === null) ? uneval(v) : `[object ${v.class}]`;
}

function summarizeObject(dv) {
    const obj = {};
    for (var name of dv.getOwnPropertyNames()) {
        var v = dv.getOwnPropertyDescriptor(name).value;
        if (v instanceof Debugger.Object) {
            v = '(...)';
        }
        obj[name] = v;
    }
    return obj;
}

function debuggeeValueToString(dv, style = {pretty: options.pretty}) {
    const dvrepr = dvToString(dv);
    if (!style.pretty || dv === null || typeof dv !== 'object')
        return [dvrepr, undefined];

    if (['Error', 'GIRespositoryNamespace', 'GObject_Object'].includes(dv.class)) {
        const errval = debuggeeGlobalWrapper.executeInGlobalWithBindings(
            'v.toString()', {v: dv});
        return [dvrepr, errval['return']];
    }

    if (style.brief)
        return [dvrepr, JSON.stringify(summarizeObject(dv), null, 4)];

    const str = debuggeeGlobalWrapper.executeInGlobalWithBindings(
        'JSON.stringify(v, null, 4)', {v: dv});
    if ('throw' in str) {
        if (style.noerror)
            return [dvrepr, undefined];

        const substyle = {};
        Object.assign(substyle, style);
        substyle.noerror = true;
        return [dvrepr, debuggeeValueToString(str.throw, substyle)];
    }

    return [dvrepr, str['return']];
}

function showDebuggeeValue(dv, style = {pretty: options.pretty}) {
    const i = nextDebuggeeValueIndex++;
    debuggeeValues[`$${i}`] = dv;
    const [brief, full] = debuggeeValueToString(dv, style);
    print(`$${i} = ${brief}`);
    if (full !== undefined)
        print(full);
}

Object.defineProperty(Debugger.Frame.prototype, 'num', {
    configurable: true,
    enumerable: false,
    get: function() {
        let i = 0;
        for (var f = topFrame; f && f !== this; f = f.older)
            i++;
        return f === null ? undefined : i;
    }
});

Debugger.Frame.prototype.describeFrame = function() {
    if (this.type == 'call')
        return `${this.callee.name || '<anonymous>'}(${
            this.arguments.map(dvToString).join(', ')})`;
    else if (this.type == 'global')
        return 'toplevel';
    else
        return `${this.type} code`;
};

Debugger.Frame.prototype.describePosition = function() {
    if (this.script)
        return this.script.describeOffset(this.offset);
    return null;
};

Debugger.Frame.prototype.describeFull = function() {
    const fr = this.describeFrame();
    const pos = this.describePosition();
    if (pos)
        return `${fr} at ${pos}`;
    return fr;
};

Object.defineProperty(Debugger.Frame.prototype, 'line', {
    configurable: true,
    enumerable: false,
    get: function() {
        if (this.script)
            return this.script.getOffsetLocation(this.offset).lineNumber;
        else
            return null;
    }
});

Debugger.Script.prototype.describeOffset = function describeOffset(offset) {
    const {lineNumber, columnNumber} = this.getOffsetLocation(offset);
    const url = this.url || '<unknown>';
    return `${url}:${lineNumber}:${columnNumber}`;
};

function showFrame(f, n) {
    if (f === undefined || f === null) {
        f = focusedFrame;
        if (f === null) {
            print('No stack.');
            return;
        }
    }
    if (n === undefined) {
        n = f.num;
        if (n === undefined)
            throw new Error('Internal error: frame not on stack');
    }

    print(`#${n.toString().padEnd(4)} ${f.describeFull()}`);
}

function saveExcursion(fn) {
    const tf = topFrame, ff = focusedFrame;
    try {
        return fn();
    } finally {
        topFrame = tf;
        focusedFrame = ff;
    }
}

// Accept debugger commands starting with '#' so that scripting the debugger
// can be annotated
function commentCommand(comment) {
    void comment;
}

// Evaluate an expression in the Debugger global - used for debugging the
// debugger
function evalCommand(expr) {
    eval(expr);
}

function quitCommand() {
    dbg.enabled = false;
    quit(0);
}
quitCommand.summary = 'Quit the debugger';
quitCommand.helpText = `USAGE
    quit`;

function backtraceCommand() {
    if (topFrame === null)
        print('No stack.');
    for (var i = 0, f = topFrame; f; i++, f = f.older)
        showFrame(f, i);
}
backtraceCommand.summary = 'Print backtrace of all stack frames';
backtraceCommand.helpText = `USAGE
    bt`;

function setCommand(rest) {
    var space = rest.indexOf(' ');
    if (space == -1) {
        print('Invalid set <option> <value> command');
    } else {
        var name = rest.substr(0, space);
        var value = rest.substr(space + 1);

        var yes = ['1', 'yes', 'true', 'on'];
        var no = ['0', 'no', 'false', 'off'];

        if (yes.includes(value))
            options[name] = true;
        else if (no.includes(value))
            options[name] = false;
        else
            options[name] = value;
    }
}
setCommand.summary = 'Sets the value of the given option';
setCommand.helpText = `USAGE
    set <option> <value>

PARAMETERS
    · option: option name. Allowed options are:
        · pretty: set print mode to pretty or brief. Allowed value true or false
    · value: option value`;

function splitPrintOptions(s, style) {
    const m = /^\/(\w+)/.exec(s);
    if (!m)
        return [s, style];
    if (m[1].indexOf('p') != -1)
        style.pretty = true;
    if (m[1].indexOf('b') != -1)
        style.brief = true;
    return [s.substr(m[0].length).trimLeft(), style];
}

function doPrint(expr, style) {
    // This is the real deal.
    const cv = saveExcursion(
        () => focusedFrame === null
            ? debuggeeGlobalWrapper.executeInGlobalWithBindings(expr, debuggeeValues)
            : focusedFrame.evalWithBindings(expr, debuggeeValues));
    if (cv === null) {
        if (!dbg.enabled)
            return [cv];
        print('Debuggee died.');
    } else if ('return' in cv) {
        if (!dbg.enabled)
            return [undefined];
        showDebuggeeValue(cv['return'], style);
    } else {
        if (!dbg.enabled)
            return [cv];
        print("Exception caught. (To rethrow it, type 'throw'.)");
        lastExc = cv.throw;
        showDebuggeeValue(lastExc, style);
    }
}

function printCommand(rest) {
    var [expr, style] = splitPrintOptions(rest, {pretty: options.pretty});
    return doPrint(expr, style);
}
printCommand.summary = 'Prints the given expression';
printCommand.helpText = `USAGE
    print[/pretty|p|brief|b] <expr>

PARAMETER
    · expr: expression to be printed
    · pretty|p: prettify the output
    · brief|b: brief output`;

function keysCommand(rest) {
    return doPrint(`Object.keys(${rest})`);
}
keysCommand.summary = 'Prints keys of the given object';
keysCommand.helpText = `USAGE
    keys <obj>

PARAMETER
    · obj: object to get keys of`;

function detachCommand() {
    dbg.enabled = false;
    return [undefined];
}
detachCommand.summary = 'Detach debugger from the script';
detachCommand.helpText = `USAGE
    detach`;

function continueCommand() {
    if (focusedFrame === null) {
        print('No stack.');
        return;
    }
    return [undefined];
}
continueCommand.summary = 'Continue program execution';
continueCommand.helpText = `USAGE
    cont`;

function throwOrReturn(rest, action, defaultCompletion) {
    if (focusedFrame !== topFrame) {
        print("To throw, you must select the newest frame (use 'frame 0').");
        return;
    }
    if (focusedFrame === null) {
        print('No stack.');
        return;
    }
    if (rest === '')
        return [defaultCompletion];

    const cv = saveExcursion(() => focusedFrame.eval(rest));
    if (cv === null) {
        if (!dbg.enabled)
            return [cv];
        print(`Debuggee died while determining what to ${action}. Stopped.`);
        return;
    }
    if ('return' in cv)
        return [{[action]: cv['return']}];
    if (!dbg.enabled)
        return [cv];
    print(`Exception determining what to ${action}. Stopped.`);
    showDebuggeeValue(cv.throw);
}

function throwCommand(rest) {
    return throwOrReturn(rest, 'throw', {throw: lastExc});
}
throwCommand.summary = 'Throws the given value';
throwCommand.helpText = `USAGE
    throw <expr>

PARAMETER
    · expr: expression to throw`;

function returnCommand(rest) {
    return throwOrReturn(rest, 'return', {return: undefined});
}
returnCommand.summary = 'Return the given value from the current frame';
returnCommand.helpText = `USAGE
    return <expr>

PARAMETER
    · expr: expression to return`;

function frameCommand(rest) {
    let n, f;
    if (rest.match(/[0-9]+/)) {
        n = +rest;
        f = topFrame;
        if (f === null) {
            print('No stack.');
            return;
        }
        for (let i = 0; i < n && f; i++) {
            if (!f.older) {
                print(`There is no frame ${rest}.`);
                return;
            }
            f.older.younger = f;
            f = f.older;
        }
        focusedFrame = f;
        showFrame(f, n);
    } else if (rest === '') {
        if (topFrame === null) {
            print('No stack.');
        } else {
            showFrame();
        }
    } else {
        print('do what now?');
    }
}
frameCommand.summary = 'Jump to specified frame or print current frame (if not specified)';
frameCommand.helpText = `USAGE
    frame [frame_num]

PARAMETER
    · frame_num: frame to jump to`;

function upCommand() {
    if (focusedFrame === null)
        print('No stack.');
    else if (focusedFrame.older === null)
        print('Initial frame selected; you cannot go up.');
    else {
        focusedFrame.older.younger = focusedFrame;
        focusedFrame = focusedFrame.older;
        showFrame();
    }
}
upCommand.summary = 'Jump to the parent frame';
upCommand.helpText = `USAGE
    up`;

function downCommand() {
    if (focusedFrame === null)
        print('No stack.');
    else if (!focusedFrame.younger)
        print('Youngest frame selected; you cannot go down.');
    else {
        focusedFrame = focusedFrame.younger;
        showFrame();
    }
}
downCommand.summary = 'Jump to the younger frame';
downCommand.helpText = `USAGE
    down`;

function printPop(c) {
    if (c['return']) {
        print('Value returned is:');
        showDebuggeeValue(c['return'], {brief: true});
    } else if (c['throw']) {
        print('Frame terminated by exception:');
        showDebuggeeValue(c['throw']);
        print("(To rethrow it, type 'throw'.)");
        lastExc = c['throw'];
    } else {
        print('No value returned.');
    }
}

// Set |prop| on |obj| to |value|, but then restore its current value
// when we next enter the repl.
function setUntilRepl(obj, prop, value) {
    var saved = obj[prop];
    obj[prop] = value;
    replCleanups.push(() => {
        obj[prop] = saved;
    });
}

function doStepOrNext(kind) {
    if (topFrame === null) {
        print('Program not running.');
        return;
    }

    // TODO: step or finish from any frame in the stack, not just the top one
    var startFrame = topFrame;
    var startLine = startFrame.line;
    if (kind.finish)
        print(`Run till exit from ${startFrame.describeFull()}`);
    else
        print(startFrame.describeFull());

    function stepPopped(completion) {
        // Note that we're popping this frame; we need to watch for
        // subsequent step events on its caller.
        this.reportedPop = true;
        printPop(completion);
        topFrame = focusedFrame = this;
        if (kind.finish || kind.until) {
            // We want to continue, but this frame is going to be invalid as
            // soon as this function returns, which will make the replCleanups
            // assert when it tries to access the dead frame's 'onPop'
            // property. So clear it out now while the frame is still valid,
            // and trade it for an 'onStep' callback on the frame we're popping to.
            preReplCleanups();
            setUntilRepl(this.older, 'onStep', stepStepped);
            return undefined;
        }
        return repl();
    }

    function stepEntered(newFrame) {
        print(`entered frame: ${newFrame.describeFull()}`);
        if (!kind.until || newFrame.line == kind.stopLine) {
            topFrame = focusedFrame = newFrame;
            return repl();
        }
        if (kind.until)
            setUntilRepl(newFrame, 'onStep', stepStepped);
    }

    function stepStepped() {
        // print('stepStepped: ' + this.describeFull());
        var stop = false;

        if (kind.finish) {
            // 'finish' set a one-time onStep for stopping at the frame it
            // wants to return to
            stop = true;
        } else if (kind.until) {
            // running until a given line is reached
            if (this.line == kind.stopLine)
                stop = true;
        } else {
            // regular step; stop whenever the line number changes
            if ((this.line != startLine) || (this != startFrame))
                stop = true;
        }

        if (stop) {
            topFrame = focusedFrame = this;
            if (focusedFrame != startFrame)
                print(focusedFrame.describeFull());
            return repl();
        }

        // Otherwise, let execution continue.
        return undefined;
    }

    if (kind.step || kind.until)
        setUntilRepl(dbg, 'onEnterFrame', stepEntered);

    // If we're stepping after an onPop, watch for steps and pops in the
    // next-older frame; this one is done.
    var stepFrame = startFrame.reportedPop ? startFrame.older : startFrame;
    if (!stepFrame || !stepFrame.script)
        stepFrame = null;
    if (stepFrame) {
        if (!kind.finish)
            setUntilRepl(stepFrame, 'onStep', stepStepped);
        setUntilRepl(stepFrame, 'onPop', stepPopped);
    }

    // Let the program continue!
    return [undefined];
}

function stepCommand() {
    return doStepOrNext({step: true});
}
stepCommand.summary = 'Step to next command';
stepCommand.helpText = `USAGE
    step`;

function nextCommand() {
    return doStepOrNext({next: true});
}
nextCommand.summary = 'Jump to next line';
nextCommand.helpText = `USAGE
    next`;

function finishCommand() {
    return doStepOrNext({finish: true});
}
finishCommand.summary = 'Run until the current frame is finished also prints the returned value';
finishCommand.helpText = `USAGE
    finish`;

function untilCommand(line) {
    return doStepOrNext({until: true, stopLine: Number(line)});
}
untilCommand.summary = 'Continue until given line';
untilCommand.helpText = `USAGE
    until <line_num>

PARAMETER
    · line_num: line_num to continue until`;

function findBreakpointOffsets(line, currentScript) {
    const offsets = currentScript.getLineOffsets(line);
    if (offsets.length !== 0)
        return [{script: currentScript, offsets}];

    const scripts = dbg.findScripts({line, url: currentScript.url});
    if (scripts.length === 0)
        return [];

    return scripts
        .map(script => ({script, offsets: script.getLineOffsets(line)}))
        .filter(({offsets}) => offsets.length !== 0);
}

class BreakpointHandler {
    constructor(num, script, offset) {
        this.num = num;
        this.script = script;
        this.offset = offset;
    }

    hit(frame) {
        return saveExcursion(() => {
            topFrame = focusedFrame = frame;
            print(`Breakpoint ${this.num}, ${frame.describeFull()}`);
            return repl();
        });
    }

    toString() {
        return `Breakpoint ${this.num} at ${this.script.describeOffset(this.offset)}`;
    }
}

function breakpointCommand(where) {
    // Only handles line numbers of the current file
    // TODO: make it handle function names and other files
    const line = Number(where);
    const possibleOffsets = findBreakpointOffsets(line, focusedFrame.script);

    if (possibleOffsets.length === 0) {
        print(`Unable to break at line ${where}`);
        return;
    }

    possibleOffsets.forEach(({script, offsets}) => {
        offsets.forEach(offset => {
            const bp = new BreakpointHandler(breakpoints.length, script, offset);
            script.setBreakpoint(offset, bp);
            breakpoints.push(bp);
            print(bp);
        });
    });
}
breakpointCommand.summary = 'Set breakpoint at the specified location.';
breakpointCommand.helpText = `USAGE
    break <line_num>

PARAMETERS
    · line_num: line number to place a breakpoint at.`;

function deleteCommand(breaknum) {
    const bp = breakpoints[breaknum];

    if (bp === undefined) {
        print(`Breakpoint ${breaknum} already deleted.`);
        return;
    }

    const {script, offset} = bp;
    script.clearBreakpoint(bp, offset);
    breakpoints[breaknum] = undefined;
    print(`${bp} deleted`);
}
deleteCommand.summary = 'Deletes breakpoint';
deleteCommand.helpText = `USAGE
    del <breakpoint_num>

PARAMETERS
    · breakpoint_num: breakpoint number to be removed.`;

// Build the table of commands.
var commands = {};
// clang-format off
var commandArray = [
    backtraceCommand, 'bt', 'where',
    breakpointCommand, 'b', 'break',
    commentCommand, '#',
    continueCommand, 'c', 'cont',
    deleteCommand, 'd', 'del',
    detachCommand,
    downCommand, 'dn',
    evalCommand, '!',
    finishCommand, 'fin',
    frameCommand, 'f',
    helpCommand, 'h',
    keysCommand, 'k',
    nextCommand, 'n',
    printCommand, 'p',
    quitCommand, 'q',
    returnCommand, 'ret',
    setCommand,
    stepCommand, 's',
    throwCommand, 't',
    untilCommand, 'u', 'upto',
    upCommand,
];
// clang-format on
var currentCmd = null;
for (var i = 0; i < commandArray.length; i++) {
    var cmd = commandArray[i];
    if (typeof cmd === 'string')
        commands[cmd] = currentCmd;
    else
        currentCmd = commands[cmd.name.replace(/Command$/, '')] = cmd;
}

function _printCommandsList() {
    print('Available commands:');

    var printcmd = function (group) {
        var summary = group.find((cmd) => !!cmd.summary);
        print(`  ${group.map((c) => c.name).join(', ')} -- ${(summary || {}).summary}`);
    };

    var cmdGroups = _groupCommands();

    for (var group of cmdGroups) {
        printcmd(group);
    }
}

function _groupCommands() {
    var groups = [];

    for (var cmd of commandArray) {
        // Don't print commands for debugging the debugger
        if ([commentCommand, evalCommand].includes(cmd) ||
            ['#', '!'].includes(cmd))
            continue;
        
        if (typeof cmd === 'string') {
            groups[groups.length - 1]['aliases'].push(cmd);
        } else {
            groups.push({
                summary: cmd.summary,
                helpText: cmd.helpText,
                aliases: [cmd.name.replace(/Command$/, '')]
            });
        }
    }
    return groups;
}

function _printCommand(cmd) {
    print(`${cmd.summary}\n\n${cmd.helpText}`);

    if (cmd.aliases.length > 1) {
        print('\nALIASES');
        for (var alias of cmd.aliases) {
            print(`    · ${alias}`);
        }
    }
}

function helpCommand(cmd) {
    if (!cmd) {
        _printCommandsList();
    } else {
        var cmdGroups = _groupCommands();
        var command = cmdGroups.find((c) => c.aliases.includes(cmd));

        if (command && command.helpText) {
            _printCommand(command);
        } else {
            print(`No help found for ${cmd} command`);
        }
    }
}
helpCommand.summary = 'Show help for the specified command else list all commands';
helpCommand.helpText = `USAGE
    help [command]

PARAMETERS
    · command: command to show help for`;

// Break cmd into two parts: its first word and everything else. If it begins
// with punctuation, treat that as a separate word. The first word is
// terminated with whitespace or the '/' character. So:
//
//   print x         => ['print', 'x']
//   print           => ['print', '']
//   !print x        => ['!', 'print x']
//   ?!wtf!?         => ['?', '!wtf!?']
//   print/b x       => ['print', '/b x']
//
function breakcmd(cmd) {
    cmd = cmd.trimLeft();
    if ("!@#$%^&*_+=/?.,<>:;'\"".indexOf(cmd.substr(0, 1)) != -1)
        return [cmd.substr(0, 1), cmd.substr(1).trimLeft()];
    var m = /\s+|(?=\/)/.exec(cmd);
    if (m === null)
        return [cmd, ''];
    return [cmd.slice(0, m.index), cmd.slice(m.index + m[0].length)];
}

function runcmd(cmd) {
    var pieces = breakcmd(cmd);
    if (pieces[0] === '')
        return undefined;

    var first = pieces[0], rest = pieces[1];
    if (!commands.hasOwnProperty(first)) {
        print(`unrecognized command '${first}'`);
        return undefined;
    }

    cmd = commands[first];
    if (cmd.length === 0 && rest !== '') {
        print('this command cannot take an argument');
        return undefined;
    }

    return cmd(rest);
}

function preReplCleanups() {
    while (replCleanups.length > 0)
        replCleanups.pop()();
}

var prevcmd;
function repl() {
    preReplCleanups();

    var cmd;
    for (;;) {
        cmd = readline();
        if (cmd === null)
            return null;
        else if (cmd === '')
            cmd = prevcmd;

        try {
            prevcmd = cmd;
            var result = runcmd(cmd);
            if (result === undefined)
                void result;  // do nothing, return to prompt
            else if (Array.isArray(result))
                return result[0];
            else if (result === null)
                return null;
            else
                throw new Error(
                    `Internal error: result of runcmd wasn't array or undefined: ${result}`);
        } catch (exc) {
            logError(exc, '*** Internal error: exception in the debugger code');
        }
    }
}

function onInitialEnterFrame(frame) {
    print('GJS debugger. Type "help" for help');
    topFrame = focusedFrame = frame;
    return repl();
}

var dbg = new Debugger();
dbg.onNewPromise = function({promiseID, promiseAllocationSite}) {
    const site = promiseAllocationSite.toString().split('\n')[0];
    print(`Promise ${promiseID} started from ${site}`);
    return undefined;
};
dbg.onPromiseSettled = function(promise) {
    let message = `Promise ${promise.promiseID} ${promise.promiseState} `;
    message += `after ${promise.promiseTimeToResolution.toFixed(3)} ms`;
    let brief, full;
    if (promise.promiseState === 'fulfilled' && typeof promise.promiseValue !== 'undefined') {
        [brief, full] = debuggeeValueToString(promise.promiseValue);
        message += ` with ${brief}`;
    } else if (promise.promiseState === 'rejected' &&
               typeof promise.promiseReason !== 'undefined') {
        [brief, full] = debuggeeValueToString(promise.promiseReason);
        message += ` with ${brief}`;
    }
    print(message);
    if (full !== undefined)
        print(full);
    return undefined;
};
dbg.onDebuggerStatement = function(frame) {
    return saveExcursion(() => {
        topFrame = focusedFrame = frame;
        print(`Debugger statement, ${frame.describeFull()}`);
        return repl();
    });
};
dbg.onExceptionUnwind = function(frame, value) {
    return saveExcursion(() => {
        topFrame = focusedFrame = frame;
        print("Unwinding due to exception. (Type 'c' to continue unwinding.)");
        showFrame();
        print('Exception value is:');
        showDebuggeeValue(value);
        return repl();
    });
};

var debuggeeGlobalWrapper = dbg.addDebuggee(debuggee);

setUntilRepl(dbg, 'onEnterFrame', onInitialEnterFrame);
