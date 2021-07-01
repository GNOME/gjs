// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2021 Evan Welsh <contact@evanwelsh.com>

// eslint-disable-next-line
/// <reference lib='es2019' />

// @ts-check

// @ts-expect-error
import GLib from 'gi://GLib';

const sLogger = Symbol('Logger');
const sPrinter = Symbol('Printer');
const sFormatter = Symbol('Formatter');
const sGroupIndentation = Symbol('GroupIndentation');
const sTimeLabels = Symbol('Time Labels');
const sCountLabels = Symbol('Count Labels');
const sLogDomain = Symbol('Log Domain');

export const DEFAULT_LOG_DOMAIN = 'Gjs-Console';

// A line-by-line implementation of https://console.spec.whatwg.org/.

// 2.2.1. Summary of formatting specifiers

// The following is an informative summary of the format specifiers processed by the above algorithm.
// Specifier     Purpose                                                            Type Conversion
// %s            Element which substitutes is converted to a string                 %String%(element)
// %d or %i      Element which substitutes is converted to an integer               %parseInt%(element, 10)
// %f            Element which substitutes is converted to a float                  %parseFloat%(element, 10)
// %o            Element is displayed with optimally useful formatting              n/a
// %O            Element is displayed with generic JavaScript object formatting     n/a
// %c            Applies provided CSS                                               n/a
const specifierTest = /%(d|i|s|f|o|O|c)/;

/**
 * @param {string} str a string to check for format specifiers like %s or %i
 * @returns {boolean}
 */
function hasFormatSpecifiers(str) {
    return specifierTest.test(str);
}

/**
 * @param {any} item an item to format
 */
function formatGenerically(item) {
    return JSON.stringify(item, null, 4);
}

/**
 * @param {any} item an item to format
 */
function formatOptimally(item) {
    // TODO: Consider 'optimal' formatting.
    return JSON.stringify(item, null, 4);
}

const propertyAttributes = {
    writable: true,
    enumerable: false,
    configurable: true,
};

/**
 * @typedef ConsoleInternalProps
 * @property {string} [sGroupIndentation]
 * @property {Record<string, number>} [sCountLabels]
 * @property {Record<string, number>} [sTimeLabels]
 * @property {string} [sLogDomain]
 */

/**
 * @implements {ConsoleInternalProps}
 */
// @ts-expect-error Console does not actually implement ConsoleInternalProps
export class Console {
    constructor() {
        // Redefine the internal functions as non-enumerable.
        Object.defineProperties(this, {
            [sLogger]: {
                ...propertyAttributes,
                value: this[sLogger].bind(this),
            },
            [sFormatter]: {
                ...propertyAttributes,
                value: this[sFormatter].bind(this),
            },
            [sPrinter]: {
                ...propertyAttributes,
                value: this[sPrinter].bind(this),
            },
        });
    }

    // 1.1 Logging functions
    // 1.1.1 assert(condition, ...data)
    assert(condition, ...data) {
        if (condition)
            return;

        let message = 'Assertion failed';

        if (data.length === 0)
            data.push(message);

        if (typeof data[0] !== 'string') {
            data.unshift(message);
        } else {
            const first = data.shift();
            data.unshift(`${message}: ${first}`);
        }
        this[sLogger]('assert', data);
    }

    // 1.1.2 clear()
    clear() {
        throw new Error('clear() is not implemented.');
    }

    // 1.1.3 debug(...data)
    debug(...data) {
        this[sLogger]('debug', data);
    }

    // 1.1.4 error(...data)
    error(...data) {
        this[sLogger]('error', data);
    }

    // 1.1.5 info(...data)
    info(...data) {
        this[sLogger]('info', data);
    }

    // 1.1.6 log(...data)
    log(...data) {
        this[sLogger]('log', data);
    }

    // 1.1.7 table(tabularData, properties)
    table(_tabularData, _properties) {
        throw new Error('table() is not implemented.');
    }

    // 1.1.8 trace(...data)
    trace(...data) {
        if (data.length === 0)
            data = ['Trace'];

        this[sPrinter]('trace', data, {
            stackTrace:
                // We remove the first line to avoid logging this line as part of the trace.
                new Error().stack?.split('\n', 2)?.[1],
        });
    }

    // 1.1.9 warn(...data)
    warn(...data) {
        const {[sLogger]: Logger} = this;
        Logger('warn', data);
    }

    // 1.1.10 dir(item, options)
    /**
     * @param {object} item an item to format generically
     * @param {never} [options] any additional options for the formatter. Unused in our implementation.
     */
    dir(item, options) {
        const object = formatGenerically(item);

        this[sPrinter]('dir', [object], options);
    }

    // 1.1.11 dirxml(...data)
    dirxml(...data) {
        this.log(...data);
    }

    // 1.2 Counting functions

    // 1.2.1 count(label)
    count(label) {
        this[sCountLabels][label] = this[sCountLabels][label] ?? 0;
        const count = ++this[sCountLabels][label];
        const concat = `${label}: ${String(count)}`;

        this[sLogger]('count', [concat]);
    }

    // 1.2.2 countReset(label)
    countReset(label) {
        const {[sPrinter]: Printer} = this;

        const count = this[sCountLabels][label];

        if (typeof count !== 'number')
            Printer('reportWarning', [`No count found for label: '${label}'.`]);
        else
            this[sCountLabels][label] = 0;
    }

    // 1.3 Grouping functions

    // 1.3.1 group(...data)
    group(...data) {
        const {[sLogger]: Logger} = this;

        Logger('group', data);

        this[sGroupIndentation] += '  ';
    }

    // 1.3.2 groupCollapsed(...data)
    groupCollapsed(...data) {
        // We can't 'collapse' output in a terminal, so we alias to
        // group()
        this.group(...data);
    }

    // 1.3.3 groupEnd()
    groupEnd() {
        this[sGroupIndentation] = this[sGroupIndentation].slice(0, -2);
    }

    // 1.4 Timing functions

    // 1.4.1 time(label)
    time(label) {
        this[sTimeLabels][label] = Date.now();
    }

    // 1.4.2 timeLog(label, ...data)
    timeLog(label, ...data) {
        const {[sPrinter]: Printer} = this;

        const startTime = this[sTimeLabels][label];

        if (typeof startTime !== 'number') {
            Printer('reportWarning', [
                `No time log found for label: '${label}'.`,
            ]);
        } else {
            const duration = Date.now() - startTime;
            const concat = `${label}: ${duration}ms`;
            data.unshift(concat);

            Printer('timeLog', data);
        }
    }

    // 1.4.3 timeEnd(label)
    timeEnd(label) {
        const {[sPrinter]: Printer} = this;
        const startTime = this[sTimeLabels][label];

        if (typeof startTime !== 'number') {
            Printer('reportWarning', [
                `No time log found for label: '${label}'.`,
            ]);
        } else {
            const duration = Date.now() - startTime;
            const concat = `${label}: ${duration}ms`;

            Printer('timeEnd', [concat]);
        }
    }

    /**
     * @param {string} logDomain the GLib log domain this Console should print with. Defaults to Gjs-Console.
     */
    setLogDomain(logDomain) {
        this[sLogDomain] = String(logDomain);
    }

    get logDomain() {
        return this[sLogDomain];
    }

    // 2. Supporting abstract operations

    /**
     * 2.1. Logger(logLevel, args)
     * The logger operation accepts a log level and a list of other arguments. Its main output is the implementation-defined side effect of printing the result to the console. This specification describes how it processes format specifiers while doing so.
     *
     * @param {string} logLevel the log level (log tag) the args should be emitted with
     * @param {unknown[]} args the arguments to pass to the printer
     * @returns {void}
     */
    [sLogger](logLevel, args) {
        const {[sFormatter]: Formatter, [sPrinter]: Printer} = this;

        // If args is empty, return.
        if (args.length === 0)
            return;
        // Let first be args[0].
        // Let rest be all elements following first in args.
        let [first, ...rest] = args;

        // If rest is empty, perform Printer(logLevel, « first ») and return.
        if (rest.length === 0) {
            Printer(logLevel, [first]);
            return undefined;
        }
        // If first does not contain any format specifiers, perform Printer(logLevel, args).
        if (typeof first !== 'string' || !hasFormatSpecifiers(first)) {
            Printer(logLevel, args);
            return undefined;
        }
        // Otherwise, perform Printer(logLevel, Formatter(args)).
        Printer(logLevel, Formatter([first, ...rest]));
        // Return undefined.
        return undefined;

        // It’s important that the printing occurs before returning from the algorithm. Many developer consoles print the result of the last operation entered into them. In such consoles, when a developer enters console.log('hello!'), this will first print 'hello!', then the undefined return value from the console.log call.
        // Indicating that printing is done before return
    }

    /**
     * 2.2. Formatter(args)
     *
     * @param {[string, ...any[]]} args an array of format strings followed by their arguments
     */
    [sFormatter](args) {
        const {[sFormatter]: Formatter} = this;
        // The formatter operation tries to format the first argument provided, using the other arguments. It will try to format the input until no formatting specifiers are left in the first argument, or no more arguments are left. It returns a list of objects suitable for printing.

        // Let target be the first element of args.
        let target = args[0];
        // Let current be the second element of args.
        let current = args[1];
        // Find the first possible format specifier specifier, from the left to the right in target.
        const specifierIndex = specifierTest.exec(target).index;
        const specifier = target.slice(specifierIndex, specifierIndex + 2);
        let converted = null;
        switch (specifier) {
        // If specifier is %s, let converted be the result of Call(%String%, undefined, « current »).
        case '%s':
            converted = String(current);
            break;
            // If specifier is %d or %i:
        case '%d':
        case '%i':
            //     If Type(current) is Symbol, let converted be NaN
            if (typeof current === 'symbol')
                converted = Number.NaN;
            //     Otherwise, let converted be the result of Call(%parseInt%, undefined, « current, 10 »).
            else
                converted = parseInt(current, 10);
            break;
            // If specifier is %f:
        case '%f':
            //     If Type(current) is Symbol, let converted be NaN
            if (typeof current === 'symbol')
                converted = Number.NaN;
            //     Otherwise, let converted be the result of Call(%parseFloat%, undefined, « current »).
            else
                converted = parseFloat(current);
            break;
            // If specifier is %o, optionally let converted be current with optimally useful formatting applied.
        case '%o':
            converted = formatOptimally(current);
            break;
            // If specifier is %O, optionally let converted be current with generic JavaScript object formatting applied.
        case '%O':
            converted = formatGenerically(current);
            break;
            // TODO: process %c
        case '%c':
            break;
        }
        // If any of the previous steps set converted, replace specifier in target with converted.
        if (converted !== null) {
            target =
                target.slice(0, specifierIndex) +
                converted +
                target.slice(specifierIndex + 2);
        }
        // Let result be a list containing target together with the elements of args starting from the third onward.
        /** @type {[string, ...any[]]} */
        let result = [target, ...args.slice(2)];
        // If target does not have any format specifiers left, return result.
        if (hasFormatSpecifiers(target))
            return result;
        // If result’s size is 1, return result.
        if (result.length === 1)
            return result;
        // Return Formatter(result).
        return Formatter(result);
    }

    /**
     * 2.3. Printer(logLevel, args[, options])
     * The printer operation is implementation-defined. It accepts a log level indicating severity, a List of arguments to print, and an optional object of implementation-specific formatting options.
     *
     * Elements appearing in args will be one of the following:
     * - JavaScript objects of any type.
     * - Implementation-specific representations of printable things such as a stack trace or group.
     * - Objects with either generic JavaScript object formatting or optimally useful formatting applied.
     * - If the options object is passed, and is not undefined or null, implementations may use options to apply implementation-specific formatting to the elements in args.
     *
     * @param {string} logLevel the log level (log tag) the args should be emitted with
     * @param {unknown[]} args the arguments to print, either a format string with replacement args or multiple strings
     * @param {{ stackTrace?: string }} [options] additional options for the printer
     * @returns {void}
     */
    [sPrinter](logLevel, args, options) {
        // How the implementation prints args is up to the implementation, but implementations should separate the objects by a space or something similar, as that has become a developer expectation.

        // By the time the printer operation is called, all format specifiers will have been taken into account, and any arguments that are meant to be consumed by format specifiers will not be present in args. The implementation’s job is simply to print the List. The output produced by calls to Printer should appear only within the last group on the appropriate group stack if the group stack is not empty, or elsewhere in the console otherwise.

        // If the console is not open when the printer operation is called, implementations should buffer messages to show them in the future up to an implementation-chosen limit (typically on the order of at least 100).
        // 2.3.1. Indicating logLevel severity

        // Each console function uses a unique value for the logLevel parameter when calling Printer, allowing implementations to customize each printed message depending on the function from which it originated. However, it is common practice to group together certain functions and treat their output similarly, in four broad categories. This table summarizes these common groupings:
        // Grouping     console functions                         Description
        // log             log(), trace(), dir(), dirxml(),        A generic log
        //          group(), groupCollapsed(), debug(),
        //          timeLog()
        // info         count(), info(), timeEnd()                 An informative log
        // warn         warn(), countReset()                     A log warning the user of something indicated by the message
        // error         error(), assert()                         A log indicating an error to the user
        let severity;

        switch (logLevel) {
        case 'log':
        case 'dir':
        case 'dirxml':
        case 'trace':
        case 'group':
        case 'groupCollapsed':
        case 'debug':
        case 'timeLog':
            severity = GLib.LogLevelFlags.LEVEL_MESSAGE;
            break;
        case 'count':
        case 'info':
        case 'timeEnd':
            severity = GLib.LogLevelFlags.LEVEL_INFO;
            break;
        case 'warn':
        case 'countReset':
            severity = GLib.LogLevelFlags.LEVEL_WARNING;
            break;
        case 'error':
        case 'assert':
            severity = GLib.LogLevelFlags.LEVEL_CRITICAL;
            break;
        default:
            severity = GLib.LogLevelFlags.LEVEL_MESSAGE;
        }

        // 2.4. Reporting warnings to the console

        // To report a warning to the console
        // given a generic description of a warning description, implementations must run these steps:

        // Let warning be an implementation-defined string derived from description.

        // Perform Printer('reportWarning', « warning »).
        if (logLevel === 'reportWarning')
            severity = GLib.LogLevelFlags.LEVEL_WARNING;

        let output = args
            .map(a => {
                if (a === null)
                    return 'null';
                // TODO: Use a better object printer
                else if (typeof a === 'object')
                    return JSON.stringify(a);
                else if (typeof a === 'function')
                    return a.toString();
                else if (typeof a === 'undefined')
                    return 'undefined';
                else if (typeof a === 'bigint')
                    return `${a}n`;
                else
                    return String(a);
            })
            .join(' ');

        let formattedOutput = this[sGroupIndentation] + output;

        if (logLevel === 'trace') {
            formattedOutput =
                `${output}\n${options?.stackTrace}` ?? 'No trace available';
        }

        GLib.log_structured(this[sLogDomain], severity, {
            MESSAGE: formattedOutput,
        });
    }
}

Object.defineProperties(Console.prototype, {
    [sGroupIndentation]: {
        ...propertyAttributes,
        value: '',
    },
    [sCountLabels]: {
        ...propertyAttributes,
        /** @type {Record<string, number>} */
        value: {},
    },
    [sTimeLabels]: {
        ...propertyAttributes,
        /** @type {Record<string, number>} */
        value: {},
    },
    [sLogDomain]: {
        ...propertyAttributes,
        value: DEFAULT_LOG_DOMAIN,
    },
});

export const console = new Console();

/**
 * @param {string} domain set the GLib log domain for the global console object.
 */
export function setConsoleLogDomain(domain) {
    console.setLogDomain(domain);
}

/**
 * @returns {string}
 */
export function getConsoleLogDomain() {
    return console.logDomain;
}

// 1 Namespace console
//
// For historical web-compatibility reasons, the namespace object for console must have as
// its [[Prototype]] an empty object, created as if by ObjectCreate(%ObjectPrototype%),
// instead of %ObjectPrototype%.
const globalConsole = Object.create({});

for (const [key, descriptor] of Object.entries(
    Object.getOwnPropertyDescriptors(Console.prototype)
)) {
    if (key === 'constructor')
        continue;
    // This non-standard function shouldn't be included.
    if (key === 'setLogDomain')
        continue;

    if (typeof descriptor.value !== 'function')
        continue;

    Object.defineProperty(globalConsole, key, {
        ...descriptor,
        value: descriptor.value.bind(console),
    });
}
Object.freeze(globalConsole);

Object.defineProperties(globalThis, {
    console: {
        configurable: false,
        enumerable: true,
        writable: false,
        value: globalConsole,
    },
});

export default console;
