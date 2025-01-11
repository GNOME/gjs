/* -*- indent-tabs-mode: nil; js-indent-level: 4 -*- */
/* global debuggee, quit, loadNative, readline, uneval, getSourceMapRegistry */
// SPDX-License-Identifier: MPL-2.0
// SPDX-FileCopyrightText: 2011 Mozilla Foundation and contributors

/*
 * This is a simple command-line debugger for GJS programs. It is based on
 * jorendb, which is a toy debugger for shell-js programs included in the
 * SpiderMonkey source.
 *
 * To run it: gjs -d path/to/file.js
 * Execution will stop at debugger statements, and you'll get a prompt before
 * the first frame is executed.
 */

const {print, logError} = loadNative('_print');

// Debugger state.
var focusedFrame = null;
var topFrame = null;
var debuggeeValues = {};
var nextDebuggeeValueIndex = 1;
var lastExc = null;
var options = {pretty: true, colors: true, ignoreCaughtExceptions: true};
var breakpoints = [undefined];  // Breakpoint numbers start at 1
var skipUnwindHandler = false;

// Cleanup functions to run when we next re-enter the repl.
var replCleanups = [];

// Convert a debuggee value v to a string.
function dvToString(v) {
    if (typeof v === 'undefined')
        return 'undefined';  // uneval(undefined) === '(void 0)', confusing
    if (typeof v === 'object' && v !== null)
        return `[object ${v.class}]`;
    const s = uneval(v);
    if (s.length > 400)
        return `${s.substr(0, 400)}...<${s.length - 400} more bytes>...`;
    return s;
}

// Build a nested tree of all private fields wherever they reside. Each level has KV tuples and their descendents:
// {cur: [[key1, value1], ...], children: {key1: {...next level}}}
function getProperties(dv, result, seen = new WeakSet()) {
    if (!dv || seen.has(dv))
        return;

    if (typeof dv === 'object')
        seen.add(dv);

    const privateKVs = dv.getOwnPrivateProperties?.().map(k => [k.description, dv.getProperty(k).return]) ?? [];
    const nonPrivateKVs = dv.getOwnPropertyNames?.().concat(dv.getOwnPropertySymbols()).map(k => [k, dv.getProperty(k).return]) ?? [];
    result.cur = privateKVs;

    result.children = {};
    // a private field can be under a non-private field
    privateKVs.concat(nonPrivateKVs).forEach(([k, v]) => {
        result.children[k] = {};
        getProperties(v, result.children[k], seen);
    });
    // prettyPrint in the debuggee compartment needs access to the original private field value and not Debugger.Object
    result.cur.forEach(kv => kv[1]?.unsafeDereference && (kv[1] = kv[1].unsafeDereference()));
}

function debuggeeValueToString(dv, style = {pretty: options.pretty}) {
    // Special sentinel values returned by Debugger.Environment.getVariable()
    if (typeof dv === 'object' && dv !== null) {
        if (dv.missingArguments)
            return ['<missing>', undefined];
        if (dv.optimizedOut)
            return ['<optimized out>', undefined];
        if (dv.uninitialized)
            return ['<uninitialized>', undefined];
        if (!(dv instanceof Debugger.Object))
            return ['<unexpected object>', JSON.stringify(dv, null, 4)];
    }

    const dvrepr = dvToString(dv);
    if (!style.pretty || (typeof dv !== 'object') || (dv === null))
        return [dvrepr, undefined];

    const exec = debuggeeGlobalWrapper.executeInGlobalWithBindings.bind(debuggeeGlobalWrapper);

    if (['TypeError', 'Error', 'GIRespositoryNamespace', 'GObject_Object'].includes(dv.class)) {
        const errval = exec('v.toString()', {v: dv});
        return [dvrepr, errval['return']];
    }

    if (style.brief)
        return [dvrepr, dvrepr];

    const properties = {};
    getProperties(dv, properties);
    const str = exec('imports._print.getPrettyPrintFunction(globalThis)(v, extra)', {v: dv, extra: dv.makeDebuggeeValue(properties)});
    if ('throw' in str) {
        if (style.noerror)
            return [dvrepr, undefined];

        const substyle = {...style, noerror: true};
        return [dvrepr, debuggeeValueToString(str.throw, substyle)];
    }

    return [dvrepr, str['return']];
}

function showDebuggeeValue(dv, style = {pretty: options.pretty}) {
    const i = nextDebuggeeValueIndex++;
    debuggeeValues[`$${i}`] = dv;
    debuggeeValues['$$'] = dv;
    const [brief, full] = debuggeeValueToString(dv, style);
    print(`$${i} = ${brief}`);
    if (full !== undefined)
        print(full);
}

Object.defineProperty(Debugger.Frame.prototype, 'num', {
    configurable: true,
    enumerable: false,
    get() {
        let i = 0;
        let f;
        for (f = topFrame; f && f !== this; f = f.older)
            i++;
        return f === null ? undefined : i;
    },
});

Debugger.Frame.prototype.describeFrame = function () {
    if (this.type === 'call') {
        return `${this.callee.name || '<anonymous>'}(${
            this.arguments.map(dvToString).join(', ')})`;
    } else if (this.type === 'global') {
        return 'toplevel';
    } else {
        return `${this.type} code`;
    }
};

Debugger.Frame.prototype.describePosition = function () {
    if (this.script)
        return this.script.describeOffset(this.offset);
    return null;
};

Debugger.Frame.prototype.describeFull = function () {
    const fr = this.describeFrame();
    const pos = this.describePosition();
    if (pos)
        return `${fr} at ${pos}`;
    return fr;
};

Object.defineProperty(Debugger.Frame.prototype, 'line', {
    configurable: true,
    enumerable: false,
    get() {
        if (this.script)
            return this.script.getOffsetLocation(this.offset).lineNumber;
        else
            return null;
    },
});

Object.defineProperty(Debugger.Frame.prototype, 'column', {
    configurable: true,
    enumerable: false,
    get() {
        return this.script?.getOffsetLocation(this.offset).columnNumber ?? null;
    },
});

Debugger.Script.prototype.describeOffset = function describeOffset(offset) {
    const {lineNumber, columnNumber} = this.getOffsetLocation(offset);
    const url = this.url || '<unknown>';
    const registry = getSourceMapRegistry();
    const consumer = registry.get(url);

    let description = `${url}:${lineNumber}:${columnNumber}`;
    const original = consumer?.originalPositionFor({line: lineNumber, column: columnNumber});

    if (original?.source || Number.isInteger(original?.line) || Number.isInteger(original?.column))
        description += ' -> ';

    if (original?.source)
        description += original.source;
    if (Number.isInteger(original?.line))
        description += `:${original.line}`;
    if (Number.isInteger(original?.column))
        description += `:${original.column + 1}`;

    return description;
};

function showFrame(f, n, option = {btCommand: false, fullOption: false}) {
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
    if (option.btCommand) {
        if (option.fullOption) {
            const variables = f.environment.names();
            for (let i = 0; i < variables.length; i++) {
                if (variables.length === 0)
                    print('No locals.');

                const value = f.environment.getVariable(variables[i]);
                const [brief] = debuggeeValueToString(value, {brief: false, pretty: false});
                print(`${variables[i]} = ${brief}`);
            }
        }
    } else {
        let lineNumber = f.line;
        print(`   ${lineNumber}\t${f.script.source.text.split('\n')[lineNumber - 1]}`);
    }
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

// Evaluate @expr in the current frame, logging and suppressing any exceptions
function evalInFrame(expr) {
    if (!focusedFrame) {
        print('No stack');
        return;
    }

    skipUnwindHandler = true;
    let cv;
    try {
        cv = saveExcursion(
            () => focusedFrame.evalWithBindings(`(${expr})`, debuggeeValues));
    } finally {
        skipUnwindHandler = false;
    }

    if (cv === null) {
        print(`Debuggee died while evaluating ${expr}`);
        return;
    }

    const {throw: exc, return: dv} = cv;
    if (exc) {
        print(`Exception caught while evaluating ${expr}: ${dvToString(exc)}`);
        return;
    }

    return {value: dv};
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
    dbg.removeAllDebuggees();
    quit(0);
}
quitCommand.summary = 'Quit the debugger';
quitCommand.helpText = `USAGE
    quit`;

function backtraceCommand(option) {
    if (topFrame === null)
        print('No stack.');
    if (option === '') {
        for (let i = 0, f = topFrame; f; i++, f = f.older)
            showFrame(f, i, {btCommand: true, fullOption: false});
    } else if (option === 'full') {
        for (let i = 0, f = topFrame; f; i++, f = f.older)
            showFrame(f, i, {btCommand: true, fullOption: true});
    } else {
        print('Invalid option');
    }
}
backtraceCommand.summary = 'Print backtrace of all stack frames and details of all local variables if the full option is added';
backtraceCommand.helpText = `USAGE
    bt <option>

PARAMETERS
    · option: option name. Allowed options are:
        · full: prints the local variables in a stack frame`;

function listCommand(option) {
    if (focusedFrame === null) {
        print('No frame to list from');
        return;
    }
    let lineNumber = focusedFrame.line, columnNumber = focusedFrame.column;
    if (option === '') {
        printSurroundingLines(lineNumber, columnNumber);
        return;
    }
    let currentLine = Number(option);
    if (Number.isNaN(currentLine) === false)
        printSurroundingLines(currentLine);

    else
        print('Unknown option');
}

function printSurroundingLines(currentLine = 1, columnNumber = 1) {
    const registry = getSourceMapRegistry();
    const sourceUrl = focusedFrame.script.source.url;
    const consumer = registry.get(sourceUrl);
    const originalObj = consumer?.originalPositionFor({line: currentLine, column: columnNumber - 1});
    const sourceMapContents = originalObj?.source ? consumer?.sourceContentFor(originalObj.source, true) : null;
    let sourceLines;
    if (sourceMapContents) {
        sourceLines = sourceMapContents.split('\n');
        currentLine = originalObj?.line ?? 1;
    } else {
        sourceLines = focusedFrame.script.source.text.split('\n');
    }
    const lastLine = sourceLines.length;
    let maxLineLimit = Math.min(lastLine, currentLine + 5);
    let minLineLimit = Math.max(1, currentLine - 5);

    for (let i = minLineLimit; i < maxLineLimit + 1; i++) {
        if (i === currentLine) {
            const code = colorCode('1');
            print(`  *${code[0]}${i}\t${sourceLines[i - 1]}${code[1]}`);
        } else {
            print(`   ${i}\t${sourceLines[i - 1]}`);
        }
    }
}

listCommand.summary = 'Prints five lines of code before and five lines after the current line of code on which the debugger is running';
listCommand.helpText = `USAGE
    list <option>
PARAMETERS
    -option : option name. Allowed options are: line number`;

function colorCode(codeNumber) {
    if (options.colors === true)
        return [`\x1b[${codeNumber}m`, '\x1b[0m'];
    else
        return ['', ''];
}
function setCommand(rest) {
    var space = rest.indexOf(' ');
    if (space === -1) {
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
        · colors: set printing with colors to true or false.
        · ignoreCaughtExceptions: do not stop on handled exceptions. Allowed value true or false
    · value: option value`;

function splitPrintOptions(s, style) {
    const m = /^\/(\w+)/.exec(s);
    if (!m)
        return [s, style];
    if (m[1].startsWith('p'))
        style.pretty = true;
    if (m[1].startsWith('b'))
        style.brief = true;
    return [s.substr(m[0].length).trimStart(), style];
}

function doPrint(expr, style) {
    // This is the real deal.
    expr = `(${expr})`;
    const cv = saveExcursion(
        () => focusedFrame === null
            ? debuggeeGlobalWrapper.executeInGlobalWithBindings(expr, debuggeeValues)
            : focusedFrame.evalWithBindings(expr, debuggeeValues));

    if (cv === null) {
        print('Debuggee died.');
    } else if ('return' in cv) {
        showDebuggeeValue(cv['return'], style);
    } else {
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
    · brief|b: brief output

expr may also reference the variables $1, $2, ... for already printed
expressions, or $$ for the most recently printed expression.`;

function keysCommand(rest) {
    if (!rest) {
        print("Missing argument. See 'help keys'");
        return;
    }

    const result = evalInFrame(rest);
    if (!result)
        return;

    const dv = result.value;
    if (!(dv instanceof Debugger.Object)) {
        print(`${rest} is ${dvToString(dv)}, not an object`);
        return;
    }
    const names = dv.getOwnPropertyNames();
    const symbols = dv.getOwnPropertySymbols();
    const privateFields = dv.getOwnPrivateProperties();
    const keys = [
        ...names.map(s => `"${s}"`),
        ...symbols.map(s => `Symbol("${s.description}")`),
        ...privateFields.map(s => `${s.description}`),
    ];
    if (keys.length === 0)
        print('No own properties');
    else
        print(keys.join(', '));
}
keysCommand.summary = 'Prints own properties of the given object';
keysCommand.helpText = `USAGE
    keys <obj>

PARAMETER
    · obj: object to get keys of`;

function detachCommand() {
    dbg.removeAllDebuggees();
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
        print(`To ${action}, you must select the newest frame (use 'frame 0')`);
        return;
    }
    if (rest === '')
        return [defaultCompletion];

    const result = evalInFrame(rest);
    if (result)
        return [{[action]: result.value}];
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
        n = Number(rest);
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
        if (topFrame === null)
            print('No stack.');
        else
            showFrame();
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
    if (focusedFrame === null) {
        print('No stack.');
    } else if (focusedFrame.older === null) {
        print('Initial frame selected; you cannot go up.');
    } else {
        focusedFrame.older.younger = focusedFrame;
        focusedFrame = focusedFrame.older;
        showFrame();
    }
}
upCommand.summary = 'Jump to the parent frame';
upCommand.helpText = `USAGE
    up`;

function downCommand() {
    if (focusedFrame === null) {
        print('No stack.');
    } else if (!focusedFrame.younger) {
        print('Youngest frame selected; you cannot go down.');
    } else {
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
        if (!kind.until || newFrame.line === kind.stopLine) {
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
            if (this.line === kind.stopLine)
                stop = true;
        } else if (this.line !== startLine || this !== startFrame) {
            // regular step; stop whenever the line number changes
            stop = true;
        }

        if (stop) {
            topFrame = focusedFrame = this;
            if (focusedFrame !== startFrame)
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
        .filter(({offsets: o}) => o.length !== 0);
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
    listCommand, 'li', 'l',
];
// clang-format on
var currentCmd = null;
for (var i = 0; i < commandArray.length; i++) {
    let cmd = commandArray[i];
    if (typeof cmd === 'string')
        commands[cmd] = currentCmd;
    else
        currentCmd = commands[cmd.name.replace(/Command$/, '')] = cmd;
}

function _printCommandsList() {
    print('Available commands:');

    function printcmd(cmd) {
        print(`  ${cmd.aliases.join(', ')} -- ${cmd.summary}`);
    }

    var cmdGroups = _groupCommands();

    for (var group of cmdGroups)
        printcmd(group);
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
                aliases: [cmd.name.replace(/Command$/, '')],
            });
        }
    }
    return groups;
}

function _printCommand(cmd) {
    print(`${cmd.summary}\n\n${cmd.helpText}`);

    if (cmd.aliases.length > 1) {
        print('\nALIASES');
        for (var alias of cmd.aliases)
            print(`    · ${alias}`);
    }
}

function helpCommand(cmd) {
    if (!cmd) {
        _printCommandsList();
    } else {
        var cmdGroups = _groupCommands();
        var command = cmdGroups.find(c => c.aliases.includes(cmd));

        if (command && command.helpText)
            _printCommand(command);
        else
            print(`No help found for ${cmd} command`);
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
    cmd = cmd.trimStart();
    if ("!@#$%^&*_+=/?.,<>:;'\"".includes(cmd.substr(0, 1)))
        return [cmd.substr(0, 1), cmd.substr(1).trimStart()];
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
    if (!Object.hasOwn(commands, first)) {
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
        if (cmd === null /* eof */) {
            quitCommand();
            return;
        } else if (cmd === '') {
            cmd = prevcmd;
        }

        try {
            prevcmd = cmd;
            var result = runcmd(cmd);
            if (result === undefined) {
                // do nothing, return to prompt
            } else if (Array.isArray(result)) {
                return result[0];
            } else if (result === null) {
                return null;
            } else {
                throw new Error(
                    `Internal error: result of runcmd wasn't array or undefined: ${result}`);
            }
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
dbg.onNewPromise = function ({promiseID, promiseAllocationSite}) {
    // if the promise was not allocated by the script, allocation site is null
    if (!promiseAllocationSite)
        return undefined;
    const site = promiseAllocationSite.toString().split('\n')[0];
    print(`Promise ${promiseID} started from ${site}`);
    return undefined;
};
dbg.onPromiseSettled = function (promise) {
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
dbg.onDebuggerStatement = function (frame) {
    return saveExcursion(() => {
        topFrame = focusedFrame = frame;
        print(`Debugger statement, ${frame.describeFull()}`);
        return repl();
    });
};
dbg.onExceptionUnwind = function (frame, value) {
    if (skipUnwindHandler)
        return undefined;

    const willBeCaught = currentFrame => {
        while (currentFrame) {
            if (currentFrame.script.isInCatchScope(currentFrame.offset))
                return true;
            currentFrame = currentFrame.older;
        }
        return false;
    };

    if (options.ignoreCaughtExceptions && willBeCaught(frame))
        return undefined;

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
