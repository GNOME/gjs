(function (exports) {
    'use strict';

    const {print, printerr, log, logError} = imports._print;

    Object.defineProperties(exports, {
        print: {
            configurable: false,
            enumerable: true,
            value: print,
        },
        printerr: {
            configurable: false,
            enumerable: true,
            value: printerr,
        },
        log: {
            configurable: false,
            enumerable: true,
            value: log,
        },
        logError: {
            configurable: false,
            enumerable: true,
            value: logError,
        },
    });
})(globalThis);
