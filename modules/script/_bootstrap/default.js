// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2013 Red Hat, Inc.
// SPDX-FileCopyrightText: 2020 Evan Welsh <contact@evanwelsh.com>

(function (exports) {
    'use strict';

    const {print, printerr, log, logError} = imports._print;
    const {setTimeout, setInterval, clearTimeout, clearInterval} = imports._timers;

    Object.defineProperties(exports, {
        ARGV: {
            configurable: false,
            enumerable: true,
            get() {
                // Wait until after bootstrap or programArgs won't be set.
                return imports.system.programArgs;
            },
        },
        setTimeout: {
            configurable: false,
            enumerable: true,
            writable: true,
            value: setTimeout,
        },
        setInterval: {
            configurable: false,
            enumerable: true,
            writable: true,
            value: setInterval,
        },
        clearTimeout: {
            configurable: false,
            enumerable: true,
            writable: true,
            value: clearTimeout,
        },
        clearInterval: {
            configurable: false,
            enumerable: true,
            writable: true,
            value: clearInterval,
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
})(globalThis);
