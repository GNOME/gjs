// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2013 Red Hat, Inc.
// SPDX-FileCopyrightText: 2020 Evan Welsh <contact@evanwelsh.com>

(function (exports) {
    'use strict';

    const {
        print,
        printerr,
        log: nativeLog,
        logError: nativeLogError,
        prettyPrintSymbol,
    } = imports._print;

    function prettyPrint(value) {
        // to be filled in
    }

    function log(...args) {
        return nativeLog(args.map(prettyPrint).join(' '));
    }

    function logError(e, ...args) {
        return nativeLogError(e, args.map(prettyPrint).join(' '));
    }

    Object.defineProperties(exports, {
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
        [prettyPrintSymbol]: {
            configurable: false,
            enumerable: true,
            writable: true,
            value: prettyPrint,
        },
    });
})(globalThis);
