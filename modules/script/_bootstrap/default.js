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

    // compare against the %TypedArray% intrinsic object all typed array constructors inherit from
    function _isTypedArray(value) {
        return value instanceof Object.getPrototypeOf(Uint8Array);
    }

    function _hasStandardToString(value) {
        return value.toString === Object.prototype.toString ||
                value.toString === Array.prototype.toString ||
                // although TypedArrays have a standard Array.prototype.toString, we currently enforce an override to warn
                // for legacy behaviour, making the toString non-standard for
                // "any Uint8Array instances created in situations where previously a ByteArray would have been created"
                _isTypedArray(value) ||
                value.toString === Date.prototype.toString;
    }

    function prettyPrint(value, extra) {
        switch (typeof value) {
        case 'object':
            if (value === null)
                return 'null';

            if (_hasStandardToString(value))
                return formatObject(value, extra);

            if (!value.toString) {
                let str = formatObject(value, extra);
                // object has null prototype either from Object.create(null) or cases like the module namespace object
                if (Object.getPrototypeOf(value) === null)
                    str = `[Object: null prototype] ${str}`;

                return str;
            }

            // Prefer the non-standard toString
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

    function formatObject(obj, properties, printedObjects = new WeakSet()) {
        printedObjects.add(obj);
        if (Array.isArray(obj) || _isTypedArray(obj))
            return formatArray(obj, properties, printedObjects).toString();

        if (obj instanceof Date)
            return formatDate(obj);

        const formattedObject = [];
        let keys = Object.getOwnPropertyNames(obj).concat(Object.getOwnPropertySymbols(obj)).map(k => [k, obj[k]]);
        // properties is only passed to us in the debugger
        if (properties?.cur)
            keys = keys.concat(properties.cur);

        for (const [propertyKey, value] of keys) {
            const key = formatPropertyKey(propertyKey);
            switch (typeof value) {
            case 'object':
                if (printedObjects.has(value))
                    formattedObject.push(`${key}: [Circular]`);
                else if (value === null)
                    formattedObject.push(`${key}: null`);
                else if (_hasStandardToString(value))
                    formattedObject.push(`${key}: ${formatObject(value, properties?.children[propertyKey], printedObjects)}`);
                else
                    formattedObject.push(`${key}: ${value.toString()}`);
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

    function formatArray(arr, properties, printedObjects) {
        const formattedArray = [];
        for (const [key, value] of arr.entries()) {
            if (printedObjects.has(value))
                formattedArray[key] = '[Circular]';
            else
                formattedArray[key] = prettyPrint(value, properties?.children[key]);
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
