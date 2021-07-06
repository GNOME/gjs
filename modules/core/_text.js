// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: Evan Welsh

const Encoding = imports._encodingNative;

const { getEncodingFromLabel } = imports._encodings;

var TextDecoder = class TextDecoder {
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

    get [Symbol.toStringTag]() {
        return 'TextDecoder';
    }

    /**
     * @param {string} encoding 
     * @param {object} [options]
     * @param {boolean=} options.fatal
     * @param {boolean=} options.ignoreBOM 
     */
    constructor(encoding = 'utf-8', options = {}) {
        const { fatal = false, ignoreBOM = false } = options;

        const encodingDefinition = getEncodingFromLabel(`${encoding}`);

        if (!encodingDefinition) {
            throw new RangeError(`Invalid encoding label: '${encoding}'`);
        }

        if (encodingDefinition.label === 'replacement') {
            throw new RangeError(`Unsupported replacement encoding: '${encoding}'`);
        }

        Object.defineProperty(this, '_internalEncoding', {
            value: encodingDefinition.internalLabel,
            enumerable: true,
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
     * @param {unknown} bytes 
     * @param {object} [options]
     * @param {boolean=} options.stream
     * @returns 
     */
    decode(bytes, options = {}) {
        const { stream = false } = options;

        if (stream) {
            throw new Error(`TextDecoder does not implement the 'stream' option.`);
        }

        /** @type {Uint8Array} */
        let input;

        if (bytes instanceof ArrayBuffer) {
            input = new Uint8Array(bytes);
        } else if (bytes instanceof Uint8Array) {
            input = bytes;
        } else if (bytes instanceof Object.getPrototypeOf(Uint8Array)) {
            let { buffer, byteLength, byteOffset } = /** @type {Uint32Array} */ (bytes);
            input = new Uint8Array(buffer, byteOffset, byteLength);
        } else if (
            typeof bytes === "object" &&
            bytes !== null &&
            "buffer" in bytes &&
            bytes.buffer instanceof ArrayBuffer
        ) {
            let { buffer, byteLength, byteOffset } = bytes;
            input = new Uint8Array(
                buffer,
                byteOffset,
                byteLength
            );
        } else if (bytes === undefined) {
            input = new Uint8Array(0);
        } else {
            throw new Error(`Provided input cannot be converted to ArrayBufferView or ArrayBuffer`);
        }

        if (this.ignoreBOM && input.length > 2 && input[0] === 0xEF && input[1] === 0xBB && input[2] === 0xBF) {
            if (this.encoding !== 'utf-8') {
                throw new Error(`Cannot ignore BOM for non-UTF8 encoding.`);
            }

            let { buffer, byteLength, byteOffset } = input;
            input = new Uint8Array(buffer, byteOffset + 3, byteLength - 3);
        }

        return Encoding.decode(input, this._internalEncoding);
    }
}

var TextEncoder = class TextEncoder {
    get [Symbol.toStringTag]() {
        return 'TextEncoder';
    }

    get encoding() {
        return 'utf-8';
    }

    encode(input = '') {
        // The TextEncoder specification only allows for UTF-8 encoding.
        return Encoding.encode(`${input}`, 'UTF-8');
    }

    encodeInto(input = '', output = new Uint8Array()) {
        // The TextEncoder specification only allows for UTF-8 encoding.
        return Encoding.encodeInto(`${input}`, output);
    }
}