// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2021 Evan Welsh <contact@evanwelsh.com>

import {getEncodingFromLabel} from './encodingMap.js';

class TextDecoder {
    /**
     * @type {string}
     */
    encoding;

    /**
     * @type {boolean}
     */
    ignoreBOM;

    /**
     * @type {boolean}
     */
    fatal;

    /**
     * @private
     * @type {string}
     */
    _internalEncoding;

    get [Symbol.toStringTag]() {
        return 'TextDecoder';
    }

    /**
     * @param {string} encoding The encoding to decode into
     * @param {object} [options] Decoding options
     * @param {boolean=} options.fatal Whether to throw or substitute when invalid characters are encountered
     * @param {boolean=} options.ignoreBOM Whether to ignore the byte order for UTF-8 arrays
     */
    constructor(encoding = 'utf-8', options = {}) {
        const {fatal = false, ignoreBOM = false} = options;

        const encodingDefinition = getEncodingFromLabel(`${encoding}`);

        if (!encodingDefinition)
            throw new RangeError(`Invalid encoding label: '${encoding}'`);

        if (encodingDefinition.label === 'replacement') {
            throw new RangeError(
                `Unsupported replacement encoding: '${encoding}'`
            );
        }

        Object.defineProperty(this, '_internalEncoding', {
            value: encodingDefinition.internalLabel,
            enumerable: false,
            writable: false,
            configurable: false,
        });

        Object.defineProperty(this, 'encoding', {
            value: encodingDefinition.label,
            enumerable: true,
            writable: false,
            configurable: false,
        });

        Object.defineProperty(this, 'ignoreBOM', {
            value: Boolean(ignoreBOM),
            enumerable: true,
            writable: false,
            configurable: false,
        });

        Object.defineProperty(this, 'fatal', {
            value: Boolean(fatal),
            enumerable: true,
            writable: false,
            configurable: false,
        });
    }

    /**
     * @param {unknown} bytes a typed array of bytes to decode
     * @param {object} [options] Decoding options
     * @param {boolean=} options.stream Unsupported option. Whether to stream the decoded bytes.
     * @returns
     */
    decode(bytes, options = {}) {
        const {stream = false} = options;

        if (stream) {
            throw new Error(
                'TextDecoder does not implement the \'stream\' option.'
            );
        }

        /** @type {Uint8Array} */
        let input;

        if (bytes instanceof ArrayBuffer) {
            input = new Uint8Array(bytes);
        } else if (bytes instanceof Uint8Array) {
            input = bytes;
        } else if (ArrayBuffer.isView(bytes)) {
            let {buffer, byteLength, byteOffset} = bytes;

            input = new Uint8Array(buffer, byteOffset, byteLength);
        } else if (bytes === undefined) {
            input = new Uint8Array(0);
        } else if (bytes instanceof import.meta.importSync('gi').GLib.Bytes) {
            input = bytes.toArray();
        } else {
            throw new Error(
                'Provided input cannot be converted to ArrayBufferView or ArrayBuffer'
            );
        }

        if (
            this.ignoreBOM &&
            input.length > 2 &&
            input[0] === 0xef &&
            input[1] === 0xbb &&
            input[2] === 0xbf
        ) {
            if (this.encoding !== 'utf-8')
                throw new Error('Cannot ignore BOM for non-UTF8 encoding.');

            let {buffer, byteLength, byteOffset} = input;
            input = new Uint8Array(buffer, byteOffset + 3, byteLength - 3);
        }

        const Encoding = import.meta.importSync('_encodingNative');
        return Encoding.decode(input, this._internalEncoding, this.fatal);
    }
}

class TextEncoder {
    get [Symbol.toStringTag]() {
        return 'TextEncoder';
    }

    get encoding() {
        return 'utf-8';
    }

    encode(input = '') {
        const Encoding = import.meta.importSync('_encodingNative');
        // The TextEncoder specification only allows for UTF-8 encoding.
        return Encoding.encode(`${input}`, 'utf-8');
    }

    encodeInto(input = '', output = new Uint8Array()) {
        const Encoding = import.meta.importSync('_encodingNative');
        // The TextEncoder specification only allows for UTF-8 encoding.
        return Encoding.encodeInto(`${input}`, output);
    }
}

Object.defineProperty(globalThis, 'TextEncoder', {
    configurable: false,
    enumerable: true,
    writable: false,
    value: TextEncoder,
});

Object.defineProperty(globalThis, 'TextDecoder', {
    configurable: false,
    enumerable: true,
    writable: false,
    value: TextDecoder,
});
