// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2021 Evan Welsh <contact@evanwelsh.com>

// Some test inputs are derived from https://github.com/denoland/deno/blob/923214c53725651792f6d55c5401bf6b475622ea/op_crates/web/08_text_encoding.js
// Data originally from https://encoding.spec.whatwg.org/encodings.json

const {Gio} = imports.gi;

/**
 * Loads a JSON file from a URI and parses it.
 *
 * @param {string} src the URI to load from
 * @returns {any}
 */
function loadJSONFromResource(src) {
    const file = Gio.File.new_for_uri(src);
    const [, bytes] = file.load_contents(null);

    const decoder = new TextDecoder();
    const jsonRaw = decoder.decode(bytes);
    const json = JSON.parse(jsonRaw);

    return json;
}

/**
 * A jasmine asymmetric matcher which expects an array-like object
 * to contain the given element array in the same order with the
 * same length. Useful for testing typed arrays.
 *
 * @template T
 * @param {T[]} elements an array of elements to compare with
 * @returns
 */
function withElements(elements) {
    return {
        /**
         * @param {ArrayLike<T>} compareTo an array-like object to compare to
         * @returns {boolean}
         */
        asymmetricMatch(compareTo) {
            return compareTo.length === elements.length && elements.every((e, i) => e === compareTo[i]);
        },
        /**
         * @returns {string}
         */
        jasmineToString() {
            return `${JSON.stringify(elements)}`;
        },
    };
}

describe('Text Encoding', function () {
    it('toString() uses spec-compliant tags', function () {
        const encoder = new TextEncoder();

        expect(encoder.toString()).toBe('[object TextEncoder]');

        const decoder = new TextDecoder();
        expect(decoder.toString()).toBe('[object TextDecoder]');
    });

    describe('TextEncoder', function () {
        describe('encode()', function () {
            it('can encode UTF8 (multi-byte chars)', function () {
                const input = '𝓽𝓮𝔁𝓽';
                const encoder = new TextEncoder();
                const encoded = encoder.encode(input);

                expect(encoded).toEqual(withElements([
                    0xf0, 0x9d, 0x93, 0xbd, 0xf0, 0x9d, 0x93, 0xae, 0xf0, 0x9d, 0x94, 0x81, 0xf0, 0x9d, 0x93, 0xbd,
                ]));
            });
        });

        describe('encodeInto()', function () {
            it('can encode UTF8 (Latin chars) into a Uint8Array', function () {
                const input = 'text';
                const encoder = new TextEncoder();
                const bytes = new Uint8Array(5);
                const result = encoder.encodeInto(input, bytes);
                expect(result.read).toBe(4);
                expect(result.written).toBe(4);

                expect(bytes).toEqual(withElements([0x74, 0x65, 0x78, 0x74, 0x00]));
            });

            it('can fully encode UTF8 (multi-byte chars) into a Uint8Array', function () {
                const input = '𝓽𝓮𝔁𝓽';
                const encoder = new TextEncoder();
                const bytes = new Uint8Array(17);
                const result = encoder.encodeInto(input, bytes);
                expect(result.read).toBe(8);
                expect(result.written).toBe(16);

                expect(bytes).toEqual(withElements([
                    0xf0, 0x9d, 0x93, 0xbd, 0xf0, 0x9d, 0x93, 0xae, 0xf0, 0x9d, 0x94, 0x81, 0xf0, 0x9d, 0x93, 0xbd, 0x00,
                ]));
            });

            it('can partially encode UTF8 into an under-allocated Uint8Array', function () {
                const input = '𝓽𝓮𝔁𝓽';
                const encoder = new TextEncoder();
                const bytes = new Uint8Array(5);
                const result = encoder.encodeInto(input, bytes);
                expect(result.read).toBe(2);
                expect(result.written).toBe(4);

                expect(bytes).toEqual(withElements([0xf0, 0x9d, 0x93, 0xbd, 0x00]));
            });
        });
    });

    describe('TextDecoder', function () {
        describe('decode()', function () {
            it('fatal is false by default', function () {
                const decoder = new TextDecoder();

                expect(decoder.fatal).toBeFalse();
            });

            it('ignoreBOM is false by default', function () {
                const decoder = new TextDecoder();

                expect(decoder.ignoreBOM).toBeFalse();
            });

            it('fatal is true when passed', function () {
                const decoder = new TextDecoder(undefined, {fatal: true});

                expect(decoder.fatal).toBeTrue();
            });

            it('ignoreBOM is true when passed', function () {
                const decoder = new TextDecoder(undefined, {ignoreBOM: true});

                expect(decoder.ignoreBOM).toBeTrue();
            });

            it('fatal is coerced to a boolean value', function () {
                const decoder = new TextDecoder(undefined, {fatal: 1});

                expect(decoder.fatal).toBeTrue();
            });

            it('ignoreBOM is coerced to a boolean value', function () {
                const decoder = new TextDecoder(undefined, {ignoreBOM: ''});

                expect(decoder.ignoreBOM).toBeFalse();
            });

            it('throws on empty input', function () {
                const decoder = new TextDecoder();
                const input = '';

                expect(() => decoder.decode(input))
                    .toThrowError('Provided input cannot be converted to ArrayBufferView or ArrayBuffer');
            });

            it('throws on null input', function () {
                const decoder = new TextDecoder();
                const input = null;

                expect(() => decoder.decode(input))
                    .toThrowError('Provided input cannot be converted to ArrayBufferView or ArrayBuffer');
            });

            it('throws on invalid encoding label', function () {
                expect(() => new TextDecoder('bad')).toThrowError("Invalid encoding label: 'bad'");
            });

            it('decodes undefined as an empty string', function () {
                const decoder = new TextDecoder();
                const input = undefined;

                expect(decoder.decode(input)).toBe('');
            });

            it('decodes UTF-8 byte array (Uint8Array)', function () {
                const decoder = new TextDecoder();
                const input = new Uint8Array([
                    0xf0, 0x9d, 0x93, 0xbd, 0xf0, 0x9d, 0x93, 0xae, 0xf0, 0x9d, 0x94, 0x81, 0xf0, 0x9d, 0x93, 0xbd,
                ]);

                expect(decoder.decode(input)).toBe('𝓽𝓮𝔁𝓽');
            });

            it('ignores byte order marker (BOM)', function () {
                const decoder = new TextDecoder('utf-8', {ignoreBOM: true});
                const input = new Uint8Array([
                    0xef, 0xbb, 0xbf, 0xf0, 0x9d, 0x93, 0xbd, 0xf0, 0x9d, 0x93, 0xae, 0xf0, 0x9d, 0x94, 0x81, 0xf0, 0x9d, 0x93, 0xbd,
                ]);

                expect(decoder.decode(input)).toBe('𝓽𝓮𝔁𝓽');
            });

            it('handles invalid byte order marker (BOM)', function () {
                const decoder = new TextDecoder('utf-8', {ignoreBOM: true});
                const input = new Uint8Array([
                    0xef, 0xbb, 0x89, 0xf0, 0x9d, 0x93, 0xbd, 0xf0, 0x9d, 0x93, 0xae, 0xf0, 0x9d, 0x94, 0x81, 0xf0, 0x9d, 0x93, 0xbd,
                ]);

                expect(decoder.decode(input)).toBe('ﻉ𝓽𝓮𝔁𝓽');
            });
        });

        describe('UTF-8 Encoding Converter', function () {
            it('can decode (not fatal)', function () {
                const decoder = new TextDecoder();

                const decoded = decoder.decode(new Uint8Array([120, 193, 120]));
                expect(decoded).toEqual('x�x');
            });

            it('can decode (fatal)', function () {
                const decoder = new TextDecoder(undefined, {
                    fatal: true,
                });

                expect(() => {
                    decoder.decode(new Uint8Array([120, 193, 120]));
                }).toThrowError(TypeError, /malformed UTF-8 character sequence/);
            });
        });

        describe('Multi-byte Encoding Converter (iconv)', function () {
            it('can decode Big-5', function () {
                const decoder = new TextDecoder('big5');
                const bytes = [164, 164, 177, 192, 183, 124, 177, 181, 168, 252, 184, 103, 192, 217, 179, 161, 188, 208, 183, 199, 192, 203, 197, 231, 167, 189, 169, 101, 176, 85];

                const decoded = decoder.decode(new Uint8Array(bytes));
                expect(decoded).toEqual('中推會接受經濟部標準檢驗局委託');
            });

            it('can decode Big-5 with incorrect input bytes', function () {
                const decoder = new TextDecoder('big5');
                const bytes = [
                    164, 164, 177, 192, 183, 124,
                    // Invalid byte...
                    0xA1,
                ];

                const decoded = decoder.decode(new Uint8Array(bytes));
                expect(decoded).toEqual('中推會�');
            });

            it('can decode Big-5 with long incorrect input bytes', function () {
                const decoder = new TextDecoder('big5');
                const bytes = [
                    164, 164, 177, 192, 183, 124,
                ];
                const baseLength = 1000;
                const longBytes = new Array(baseLength).fill(bytes, 0, baseLength).flat();

                // Append invalid byte sequence...
                longBytes.push(0xA3);

                const decoded = decoder.decode(new Uint8Array(longBytes));

                const baseResult = '中推會';
                const longResult = [...new Array(baseLength).fill(baseResult, 0, baseLength), '�'].join('');

                expect(decoded).toEqual(longResult);
            });
        });

        describe('Single Byte Encoding Converter', function () {
            it('can decode legacy single byte encoding (not fatal)', function () {
                const decoder = new TextDecoder('iso-8859-6');

                const decoded = decoder.decode(new Uint8Array([161, 200, 200]));
                expect(decoded).toEqual('�بب');
            });

            it('can decode legacy single byte encoding (fatal)', function () {
                const decoder = new TextDecoder('iso-8859-6', {
                    fatal: true,
                });

                expect(() => {
                    decoder.decode(new Uint8Array([161, 200, 200]));
                }).toThrowError(TypeError, 'Invalid character in decode.');
            });

            it('can decode ASCII', function () {
                const input = new Uint8Array([0x89, 0x95, 0x9f, 0xbf]);
                const decoder = new TextDecoder('ascii');
                expect(decoder.decode(input)).toBe('‰•Ÿ¿');
            });

            // Straight from https://encoding.spec.whatwg.org/encodings.json
            const encodingsTable = loadJSONFromResource('resource:///org/gjs/jsunit/modules/encodings.json');

            const singleByteEncodings = encodingsTable.filter(group => {
                return group.heading === 'Legacy single-byte encodings';
            })[0].encodings;

            // Straight from https://encoding.spec.whatwg.org/indexes.json
            const singleByteIndexes = loadJSONFromResource('resource:///org/gjs/jsunit/modules/indexes.json');

            function assertDecode(data, encoding) {
                if (encoding === 'ISO-8859-8-I')
                    encoding = 'ISO-8859-8';

                for (let i = 0, l = data.length; i < l; i++) {
                    const cp = data.charCodeAt(i);
                    let expectedCp = i < 0x80 ? i : singleByteIndexes[encoding.toLowerCase()][i - 0x80];
                    if (typeof expectedCp === 'undefined' || expectedCp === null)
                        expectedCp = 0xfffd;

                    expect(cp).toBe(expectedCp);
                }
            }
            const buffer = new ArrayBuffer(255);
            const view = new Uint8Array(buffer);

            for (let i = 0, l = view.byteLength; i < l; i++)
                view[i] = i;


            for (let i = 0, l = singleByteEncodings.length; i < l; i++) {
                const encoding = singleByteEncodings[i];

                it(`${encoding.name} has correct code table for all labels.`, function () {
                    for (let i2 = 0, l2 = encoding.labels.length; i2 < l2; i2++) {
                        const label = encoding.labels[i2];
                        const decoder = new TextDecoder(label);
                        const data = decoder.decode(view);

                        expect(decoder.encoding).toBe(encoding.name.toLowerCase());
                        assertDecode(data, encoding.name);
                    }
                });
            }
        });
    });
});
