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
        switch (typeof value) {
        case 'object':
            if (value === null)
                return 'null';

            if (value.toString === Object.prototype.toString ||
                value.toString === Array.prototype.toString ||
                value.toString === Date.prototype.toString) {
                const printedObjects = new WeakSet();
                return formatObject(value, printedObjects);
            }
            // If the object has a nonstandard toString, prefer that
            return value.toString();
        case 'function':
            if (value.toString === Function.prototype.toString)
                return formatFunction(value);
            return value.toString();
        case 'string':
            return JSON.stringify(value);
        case 'symbol':
            return formatSymbol(value);
        case 'undefined':
            return 'undefined';
        default:
            return value.toString();
        }
    }

    function formatPropertyKey(key) {
        if (typeof key === 'symbol')
            return `[${formatSymbol(key)}]`;
        return `${key}`;
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
        const keys = Object.getOwnPropertyNames(obj).concat(Object.getOwnPropertySymbols(obj));
        for (const propertyKey of keys) {
            const value = obj[propertyKey];
            const key = formatPropertyKey(propertyKey);
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
            case 'symbol':
                formattedObject.push(`${key}: ${formatSymbol(value)}`);
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

    function formatSymbol(sym) {
        // Try to format Symbols in the same way that they would be constructed.

        // First check if this is a global registered symbol
        const globalKey = Symbol.keyFor(sym);
        if (globalKey !== undefined)
            return `Symbol.for("${globalKey}")`;

        const descr = sym.description;
        // Special-case the 'well-known' (built-in) Symbols
        if (descr.startsWith('Symbol.'))
            return descr;

        // Otherwise, it's just a regular symbol
        return `Symbol("${descr}")`;
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
