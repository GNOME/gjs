/* exported ByteArray, fromArray, fromGBytes, fromString, toGBytes, toString */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2017 Philip Chimento <philip.chimento@gmail.com>

// Allow toString to be declared.
/* eslint no-redeclare: ["error", { "builtinGlobals": false }] */

var {fromGBytes, fromString, toString} = imports._byteArrayNative;

const {GLib} = imports.gi;

// For backwards compatibility

/**
 * @param {Iterable<number>} array an iterable to convert into a ByteArray
 *   wrapper
 * @returns {ByteArray}
 */
function fromArray(array) {
    return new ByteArray(Uint8Array.from(array));
}

/**
 * @param {Uint8Array} array the Uint8Array to convert to GLib.Bytes
 * @returns {GLib.Bytes}
 */
function toGBytes(array) {
    if (!(array instanceof Uint8Array))
        throw new Error('Argument to ByteArray.toGBytes() must be a Uint8Array');

    return new GLib.Bytes(array);
}

var ByteArray = class ByteArray {
    constructor(arg = 0) {
        if (arg instanceof Uint8Array)
            this._array = arg;
        else
            this._array = new Uint8Array(arg);
        return new Proxy(this, ByteArray);
    }

    static get(target, prop, receiver) {
        if (!Number.isNaN(Number.parseInt(prop)))
            return Reflect.get(target._array, prop);
        return Reflect.get(target, prop, receiver);
    }

    static set(target, prop, val, receiver) {
        let ix = Number.parseInt(prop);
        if (!Number.isNaN(ix)) {
            if (ix >= target._array.length) {
                let newArray = new Uint8Array(ix + 1);
                newArray.set(target._array);
                target._array = newArray;
            }
            return Reflect.set(target._array, prop, val);
        }
        return Reflect.set(target, prop, val, receiver);
    }

    get length() {
        return this._array.length;
    }

    set length(newLength) {
        if (newLength === this._array.length)
            return;
        if (newLength < this._array.length) {
            this._array = new Uint8Array(this._array.buffer, 0, newLength);
            return;
        }
        let newArray = new Uint8Array(newLength);
        newArray.set(this._array);
        this._array = newArray;
    }

    toString(encoding = 'UTF-8') {
        return toString(this._array, encoding);
    }

    toGBytes() {
        return toGBytes(this._array);
    }
};
