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
        setPrettyPrintFunction,
    } = imports._print;

    function log(...args) {
        return nativeLog(args.map(prettyPrint).join(' '));
    }

    function logError(e, ...args) {
        if (args.length === 0)
            return nativeLogError(e);
        return nativeLogError(e, args.map(prettyPrint).join(' '));
    }

    function prettyPrint(value) {
        switch (typeof value) {
        case 'object':
            return formatObject(value);
        case 'function':
            return formatFunction(value);
        default:
            return value.toString();
        }
    }

    function formatObject(obj) {
        if (Array.isArray(obj))
            return formatArray(obj).toString();
        if (obj instanceof Date)
            return formatDate(obj);

        const formattedObject = [];
        for (const [key, value] of Object.entries(obj)) {
            switch (typeof value) {
            case 'object':
                formattedObject.push(`${key}: ${formatObject(value)}`);
                break;
            case 'function':
                formattedObject.push(`${key}: ${formatFunction(value)}`);
                break;
            case 'string':
                formattedObject.push(`${key}: "${value}"`);
                break;
            default:
                formattedObject.push(`${key}: ${value}`);
                break;
            }
        }
        return `{ ${formattedObject.join(', ')} }`;
    }

    function formatArray(arr) {
        const formattedArray = [];
        for (const [key, value] of arr.entries())
            formattedArray[key] = prettyPrint(value);
        return `[${formattedArray.join(', ')}]`;
    }

    function formatDate(date) {
        return date.toISOString();
    }

    function formatFunction(func) {
        let funcOutput = `[ Function: ${func.name} ]`;
        return funcOutput;
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
    });
    setPrettyPrintFunction(exports, prettyPrint);
})(globalThis);
