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
        return nativeLog(args.map(arg => typeof arg === 'string' ? arg : prettyPrint(arg)).join(' '));
    }

    function logError(e, ...args) {
        if (args.length === 0)
            return nativeLogError(e);
        return nativeLogError(e, args.map(arg => typeof arg === 'string' ? arg : prettyPrint(arg)).join(' '));
    }

    function prettyPrint(value) {
        if (value.toString === Object.prototype.toString || value.toString === Array.prototype.toString || value.toString === Function.prototype.toString || value.toString === Date.prototype.toString) {
            const printedObjects = new WeakSet();
            switch (typeof value) {
            case 'object':
                return formatObject(value, printedObjects);
            case 'function':
                return formatFunction(value);
            default:
                return value.toString();
            }
        } else {
            if (typeof value === 'string')
                return JSON.stringify(value);
            return value.toString();
        }
    }

    function formatObject(obj, printedObjects) {
        printedObjects.add(obj);
        if (Array.isArray(obj))
            return formatArray(obj, printedObjects).toString();

        if (obj instanceof Date)
            return formatDate(obj);

        if (obj[Symbol.toStringTag] === 'GIRepositoryNamespace')
            return obj.toString();

        const formattedObject = [];
        for (const [key, value] of Object.entries(obj)) {
            switch (typeof value) {
            case 'object':
                if (printedObjects.has(value))
                    formattedObject.push(`${key}: [Circular]`);
                else
                    formattedObject.push(`${key}: ${formatObject(value, printedObjects)}`);
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
        return Object.keys(formattedObject).length === 0 ? '{}'
            : `{ ${formattedObject.join(', ')} }`;
    }

    function formatArray(arr, printedObjects) {
        const formattedArray = [];
        for (const [key, value] of arr.entries()) {
            if (printedObjects.has(value))
                formattedArray[key] = '[Circular]';
            else
                formattedArray[key] = prettyPrint(value);
        }
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
