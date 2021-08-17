// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2021 Evan Welsh <contact@evanwelsh.com>

import GLib from 'gi://GLib';
import GjsPrivate from 'gi://GjsPrivate';

const sLogger = Symbol('Logger');
const sPrinter = Symbol('Printer');
const sFormatter = Symbol('Formatter');
const sGroupIndentation = Symbol('GroupIndentation');
const sTimeLabels = Symbol('Time Labels');
const sCountLabels = Symbol('Count Labels');
const sLogDomain = Symbol('Log Domain');

const DEFAULT_LOG_DOMAIN = 'Gjs-Console';

// A line-by-line implementation of https://console.spec.whatwg.org/.

// 2.2.1. Formatting specifiers
// https://console.spec.whatwg.org/#formatting-specifiers
//
// %s - string
// %d or %i - integer
// %f - float
// %o - "optimal" object formatting
// %O - "generic" object formatting
// %c - "CSS" formatting (unimplemented by GJS)

/**
 * A simple regex to capture formatting specifiers
 */
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
 * @returns {string}
 */
function formatOptimally(item) {
    // Handle optimal error formatting.
    if (item instanceof Error) {
        return `${item.toString()}${item.stack ? '\n' : ''}${item.stack
            ?.split('\n')
            // Pad each stacktrace line.
            .map(line => line.padStart(2, ' '))
            .join('\n')}`;
    }

    // TODO: Enhance 'optimal' formatting.
    // There is a current work on a better object formatter for GJS in
    // https://gitlab.gnome.org/GNOME/gjs/-/merge_requests/587
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
 * Implementation of the WHATWG Console object.
 *
 * @implements {ConsoleInternalProps}
 */
// @ts-expect-error Console does not actually implement ConsoleInternalProps,
// once private class fields are merged we will remove the interface.
class Console {
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

    get [Symbol.toStringTag]() {
        return 'Console';
    }

    // 1.1 Logging functions
    // https://console.spec.whatwg.org/#logging

    /**
     * Logs a critical message if the condition is not truthy.
     * {@see console.error()} for additional information.
     *
     * @param {boolean} condition a boolean condition which, if false, causes
     *   the log to print
     * @param  {...any} data formatting substitutions, if applicable
     * @returns {void}
     */
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

    /**
     * Resets grouping and clears the terminal on systems supporting ANSI
     * terminal control sequences.
     *
     * In file-based stdout or systems which do not support clearing,
     * console.clear() has no visual effect.
     *
     * @returns {void}
     */
    clear() {
        this[sGroupIndentation] = '';
        GjsPrivate.clear_terminal();
    }

    /**
     * Logs a message with severity equal to {@see GLib.LogLevelFlags.DEBUG}.
     *
     * @param  {...any} data formatting substitutions, if applicable
     */
    debug(...data) {
        this[sLogger]('debug', data);
    }

    /**
     * Logs a message with severity equal to {@see GLib.LogLevelFlags.CRITICAL}.
     * Does not use {@see GLib.LogLevelFlags.ERROR} to avoid asserting and
     * forcibly shutting down the application.
     *
     * @param  {...any} data formatting substitutions, if applicable
     */
    error(...data) {
        this[sLogger]('error', data);
    }

    /**
     * Logs a message with severity equal to {@see GLib.LogLevelFlags.INFO}.
     *
     * @param  {...any} data formatting substitutions, if applicable
     */
    info(...data) {
        this[sLogger]('info', data);
    }

    /**
     * Logs a message with severity equal to {@see GLib.LogLevelFlags.MESSAGE}.
     *
     * @param  {...any} data formatting substitutions, if applicable
     */
    log(...data) {
        this[sLogger]('log', data);
    }

    // 1.1.7 table(tabularData, properties)
    table(tabularData, _properties) {
        this.log(tabularData);
    }

    /**
     * @param  {...any} data formatting substitutions, if applicable
     * @returns {void}
     */
    trace(...data) {
        if (data.length === 0)
            data = ['Trace'];

        this[sPrinter]('trace', data, {
            stackTrace:
                // We remove the first line to avoid logging this line as part
                // of the trace.
                new Error().stack?.split('\n', 2)?.[1],
        });
    }

    /**
     * @param  {...any} data formatting substitutions, if applicable
     * @returns {void}
     */
    warn(...data) {
        const {[sLogger]: Logger} = this;
        Logger('warn', data);
    }

    /**
     * @param {object} item an item to format generically
     * @param {never} [options] any additional options for the formatter. Unused
     *   in our implementation.
     */
    dir(item, options) {
        const object = formatGenerically(item);

        this[sPrinter]('dir', [object], options);
    }

    /**
     * @param  {...any} data formatting substitutions, if applicable
     * @returns {void}
     */
    dirxml(...data) {
        this.log(...data);
    }

    // 1.2 Counting functions
    // https://console.spec.whatwg.org/#counting

    /**
     * Logs how many times console.count(label) has been called with a given
     * label.
     * {@see console.countReset()} for resetting a count.
     *
     * @param  {string} label unique identifier for this action
     * @returns {void}
     */
    count(label) {
        this[sCountLabels][label] = this[sCountLabels][label] ?? 0;
        const count = ++this[sCountLabels][label];
        const concat = `${label}: ${count}`;

        this[sLogger]('count', [concat]);
    }

    /**
     * @param  {string} label the unique label to reset the count for
     * @returns {void}
     */
    countReset(label) {
        const {[sPrinter]: Printer} = this;

        const count = this[sCountLabels][label];

        if (typeof count !== 'number')
            Printer('reportWarning', [`No count found for label: '${label}'.`]);
        else
            this[sCountLabels][label] = 0;
    }

    // 1.3 Grouping functions
    // https://console.spec.whatwg.org/#grouping

    /**
     * @param  {...any} data formatting substitutions, if applicable
     * @returns {void}
     */
    group(...data) {
        const {[sLogger]: Logger} = this;

        Logger('group', data);

        this[sGroupIndentation] += '  ';
    }

    /**
     * Alias for console.group()
     *
     * @param  {...any} data formatting substitutions, if applicable
     * @returns {void}
     */
    groupCollapsed(...data) {
        // We can't 'collapse' output in a terminal, so we alias to
        // group()
        this.group(...data);
    }

    /**
     * @returns {void}
     */
    groupEnd() {
        this[sGroupIndentation] = this[sGroupIndentation].slice(0, -2);
    }

    // 1.4 Timing functions
    // https://console.spec.whatwg.org/#timing

    /**
     * @param {string} label unique identifier for this action, pass to
     *   console.timeEnd() to complete
     * @returns {void}
     */
    time(label) {
        this[sTimeLabels][label] = GLib.get_monotonic_time();
    }

    /**
     * Logs the time since the last call to console.time(label) where label is
     * the same.
     *
     * @param {string} label unique identifier for this action, pass to
     *   console.timeEnd() to complete
     * @param {...any} data string substitutions, if applicable
     * @returns {void}
     */
    timeLog(label, ...data) {
        const {[sPrinter]: Printer} = this;

        const startTime = this[sTimeLabels][label];

        if (typeof startTime !== 'number') {
            Printer('reportWarning', [
                `No time log found for label: '${label}'.`,
            ]);
        } else {
            const durationMs = (GLib.get_monotonic_time() - startTime) / 1000;
            const concat = `${label}: ${durationMs.toFixed(3)} ms`;
            data.unshift(concat);

            Printer('timeLog', data);
        }
    }

    /**
     * Logs the time since the last call to console.time(label) and completes
     * the action.
     * Call console.time(label) again to re-measure.
     *
     * @param {string} label unique identifier for this action
     * @returns {void}
     */
    timeEnd(label) {
        const {[sPrinter]: Printer} = this;
        const startTime = this[sTimeLabels][label];

        if (typeof startTime !== 'number') {
            Printer('reportWarning', [
                `No time log found for label: '${label}'.`,
            ]);
        } else {
            delete this[sTimeLabels][label];

            const durationMs = (GLib.get_monotonic_time() - startTime) / 1000;
            const concat = `${label}: ${durationMs.toFixed(3)} ms`;

            Printer('timeEnd', [concat]);
        }
    }

    // Non-standard functions which are de-facto standards.
    // Similar to Node, we define these as no-ops for now.

    /**
     * @deprecated Not implemented in GJS
     *
     * @param {string} _label unique identifier for this action, pass to
     *   console.profileEnd to complete
     * @returns {void}
     */
    profile(_label) {}

    /**
     * @deprecated Not implemented in GJS
     *
     * @param {string} _label unique identifier for this action
     * @returns {void}
     */
    profileEnd(_label) {}

    /**
     * @deprecated Not implemented in GJS
     *
     * @param {string} _label unique identifier for this action
     * @returns {void}
     */
    timeStamp(_label) {}

    // GJS-specific extensions for integrating with GLib structured logging

    /**
     * @param {string} logDomain the GLib log domain this Console should print
     *   with. Defaults to 'Gjs-Console'.
     * @returns {void}
     */
    setLogDomain(logDomain) {
        this[sLogDomain] = String(logDomain);
    }

    /**
     * @returns {string}
     */
    get logDomain() {
        return this[sLogDomain];
    }

    // 2. Supporting abstract operations
    // https://console.spec.whatwg.org/#supporting-ops

    /**
     * 2.1. Logger
     * https://console.spec.whatwg.org/#logger
     *
     * Conditionally applies formatting based on the inputted arguments,
     * and prints at the provided severity (logLevel)
     *
     * @param {string} logLevel the severity (log level) the args should be
     *   emitted with
     * @param {unknown[]} args the arguments to pass to the printer
     * @returns {void}
     */
    [sLogger](logLevel, args) {
        const {[sFormatter]: Formatter, [sPrinter]: Printer} = this;

        if (args.length === 0)
            return;

        let [first, ...rest] = args;

        if (rest.length === 0) {
            Printer(logLevel, [first]);
            return undefined;
        }

        // If first does not contain any format specifiers, don't call Formatter
        if (typeof first !== 'string' || !hasFormatSpecifiers(first)) {
            Printer(logLevel, args);
            return undefined;
        }

        // Otherwise, perform print the result of Formatter.
        Printer(logLevel, Formatter([first, ...rest]));

        return undefined;
    }

    /**
     * 2.2. Formatter
     * https://console.spec.whatwg.org/#formatter
     *
     * @param {[string, ...any[]]} args an array of format strings followed by
     *   their arguments
     */
    [sFormatter](args) {
        const {[sFormatter]: Formatter} = this;

        // The initial formatting string is the first arg
        let target = args[0];

        if (args.length === 1)
            return target;

        let current = args[1];

        // Find the index of the first format specifier.
        const specifierIndex = specifierTest.exec(target).index;
        const specifier = target.slice(specifierIndex, specifierIndex + 2);
        let converted = null;
        switch (specifier) {
        case '%s':
            converted = String(current);
            break;
        case '%d':
        case '%i':
            if (typeof current === 'symbol')
                converted = Number.NaN;
            else
                converted = parseInt(current, 10);
            break;
        case '%f':
            if (typeof current === 'symbol')
                converted = Number.NaN;
            else
                converted = parseFloat(current);
            break;
        case '%o':
            converted = formatOptimally(current);
            break;
        case '%O':
            converted = formatGenerically(current);
            break;
        case '%c':
            converted = '';
            break;
        }
        // If any of the previous steps set converted, replace the specifier in
        // target with the converted value.
        if (converted !== null) {
            target =
                target.slice(0, specifierIndex) +
                converted +
                target.slice(specifierIndex + 2);
        }

        /**
         * Create the next format input...
         *
         * @type {[string, ...any[]]}
         */
        let result = [target, ...args.slice(2)];

        if (!hasFormatSpecifiers(target))
            return result;

        if (result.length === 1)
            return result;

        return Formatter(result);
    }

    /**
     * @typedef {object} PrinterOptions
     * @param {string} [stackTrace] an error stacktrace to append
     * @param {Record<string, any>} [fields] fields to include in the structured
     *   logging call
     */

    /**
     * 2.3. Printer
     * https://console.spec.whatwg.org/#printer
     *
     * This implementation of Printer maps WHATWG log severity to
     * {@see GLib.LogLevelFlags} and outputs using GLib structured logging.
     *
     * @param {string} logLevel the log level (log tag) the args should be
     *   emitted with
     * @param {unknown[]} args the arguments to print, either a format string
     *   with replacement args or multiple strings
     * @param {PrinterOptions} [options] additional options for the
     *   printer
     * @returns {void}
     */
    [sPrinter](logLevel, args, options) {
        let severity;

        switch (logLevel) {
        case 'log':
        case 'dir':
        case 'dirxml':
        case 'trace':
        case 'group':
        case 'groupCollapsed':
        case 'timeLog':
        case 'timeEnd':
            severity = GLib.LogLevelFlags.LEVEL_MESSAGE;
            break;
        case 'debug':
            severity = GLib.LogLevelFlags.LEVEL_DEBUG;
            break;
        case 'count':
        case 'info':
            severity = GLib.LogLevelFlags.LEVEL_INFO;
            break;
        case 'warn':
        case 'countReset':
        case 'reportWarning':
            severity = GLib.LogLevelFlags.LEVEL_WARNING;
            break;
        case 'error':
        case 'assert':
            severity = GLib.LogLevelFlags.LEVEL_CRITICAL;
            break;
        default:
            severity = GLib.LogLevelFlags.LEVEL_MESSAGE;
        }

        let output = args
            .map(a => {
                if (a === null)
                    return 'null';
                else if (typeof a === 'object')
                    return formatOptimally(a);
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

const console = new Console();

/**
 * @param {string} domain set the GLib log domain for the global console object.
 */
function setConsoleLogDomain(domain) {
    console.setLogDomain(domain);
}

/**
 * @returns {string}
 */
function getConsoleLogDomain() {
    return console.logDomain;
}

/**
 * For historical web-compatibility reasons, the namespace object for
 * console must have {} as its [[Prototype]].
 *
 * @type {Omit<Console, 'setLogDomain' | 'logDomain'>}
 */
const globalConsole = Object.create({});

const propertyNames =
    /** @type {['constructor', ...Array<string & keyof Console>]} */
    // eslint-disable-next-line no-extra-parens
    (Object.getOwnPropertyNames(Console.prototype));
const propertyDescriptors = Object.getOwnPropertyDescriptors(Console.prototype);
for (const key of propertyNames) {
    if (key === 'constructor')
        continue;

    // This non-standard function shouldn't be included.
    if (key === 'setLogDomain')
        continue;

    const descriptor = propertyDescriptors[key];
    if (typeof descriptor.value !== 'function')
        continue;

    Object.defineProperty(globalConsole, key, {
        ...descriptor,
        value: descriptor.value.bind(console),
    });
}
Object.defineProperties(globalConsole, {
    [Symbol.toStringTag]: {
        configurable: false,
        enumerable: true,
        get() {
            return 'console';
        },
    },
});
Object.freeze(globalConsole);

Object.defineProperty(globalThis, 'console', {
    configurable: false,
    enumerable: true,
    writable: false,
    value: globalConsole,
});

export {
    getConsoleLogDomain,
    setConsoleLogDomain,
    DEFAULT_LOG_DOMAIN
};

export default {
    getConsoleLogDomain,
    setConsoleLogDomain,
    DEFAULT_LOG_DOMAIN,
};
