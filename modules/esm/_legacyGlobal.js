import { prettyPrint } from './_prettyPrint.js';

const {
    print,
    printerr,
    log: nativeLog,
    logError: nativeLogError,
} = import.meta.importSync('_print');

function log(...args) {
    return nativeLog(args.map(arg => typeof arg === 'string' ? arg : prettyPrint(arg)).join(' '));
}

function logError(e, ...args) {
    if (args.length === 0)
        return nativeLogError(e);
    return nativeLogError(e, args.map(arg => typeof arg === 'string' ? arg : prettyPrint(arg)).join(' '));
}

Object.defineProperties(globalThis, {
    ARGV: {
        configurable: false,
        enumerable: true,
        get() {
            // Wait until after bootstrap or programArgs won't be set.
            return imports.system.programArgs;
        },
    },
    print: {
        configurable: false,
        enumerable: true,
        writable: true,
        value: print,
    },
    printerr: {
        configurable: false,
        enumerable: true,
        writable: true,
        value: printerr,
    },
    log: {
        configurable: false,
        enumerable: true,
        writable: true,
        value: log,
    },
    logError: {
        configurable: false,
        enumerable: true,
        writable: true,
        value: logError,
    },
});
