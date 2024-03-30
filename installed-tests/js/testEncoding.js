// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2021 Evan Welsh <contact@evanwelsh.com>

// Some test inputs are derived from https://github.com/denoland/deno/blob/923214c53725651792f6d55c5401bf6b475622ea/op_crates/web/08_text_encoding.js
// Data originally from https://encoding.spec.whatwg.org/encodings.json

import GLib from 'gi://GLib';
import Gio from 'gi://Gio';

import {arrayLikeWithExactContents} from './matchers.js';

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
 * Encoded form of '𝓽𝓮𝔁𝓽'
 *
 * @returns {number[]}
 */
function encodedMultibyteCharArray() {
    return [
        0xf0, 0x9d, 0x93, 0xbd, 0xf0, 0x9d, 0x93, 0xae, 0xf0, 0x9d, 0x94, 0x81,
        0xf0, 0x9d, 0x93, 0xbd,
    ];
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

                expect(encoded).toEqual(
                    arrayLikeWithExactContents([...encodedMultibyteCharArray()])
                );
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

                expect(bytes).toEqual(
                    arrayLikeWithExactContents([0x74, 0x65, 0x78, 0x74, 0x00])
                );
            });

            it('can fully encode UTF8 (multi-byte chars) into a Uint8Array', function () {
                const input = '𝓽𝓮𝔁𝓽';
                const encoder = new TextEncoder();
                const bytes = new Uint8Array(17);
                const result = encoder.encodeInto(input, bytes);
                expect(result.read).toBe(8);
                expect(result.written).toBe(16);

                expect(bytes).toEqual(
                    arrayLikeWithExactContents([
                        ...encodedMultibyteCharArray(),
                        0x00,
                    ])
                );
            });

            it('can partially encode UTF8 into an under-allocated Uint8Array', function () {
                const input = '𝓽𝓮𝔁𝓽';
                const encoder = new TextEncoder();
                const bytes = new Uint8Array(5);
                const result = encoder.encodeInto(input, bytes);
                expect(result.read).toBe(2);
                expect(result.written).toBe(4);

                expect(bytes).toEqual(
                    arrayLikeWithExactContents([
                        ...encodedMultibyteCharArray().slice(0, 4),
                        0x00,
                    ])
                );
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

                expect(() => decoder.decode(input)).toThrowError(
                    'Provided input cannot be converted to ArrayBufferView or ArrayBuffer'
                );
            });

            it('throws on null input', function () {
                const decoder = new TextDecoder();
                const input = null;

                expect(() => decoder.decode(input)).toThrowError(
                    'Provided input cannot be converted to ArrayBufferView or ArrayBuffer'
                );
            });

            it('throws on invalid encoding label', function () {
                expect(() => new TextDecoder('bad')).toThrowError(
                    "Invalid encoding label: 'bad'"
                );
            });

            it('decodes undefined as an empty string', function () {
                const decoder = new TextDecoder();
                const input = undefined;

                expect(decoder.decode(input)).toBe('');
            });

            it('decodes UTF-8 byte array (Uint8Array)', function () {
                const decoder = new TextDecoder();
                const input = new Uint8Array([...encodedMultibyteCharArray()]);

                expect(decoder.decode(input)).toBe('𝓽𝓮𝔁𝓽');
            });

            it('decodes GLib.Bytes', function () {
                const decoder = new TextDecoder();
                const input = new GLib.Bytes(encodedMultibyteCharArray());

                expect(decoder.decode(input)).toBe('𝓽𝓮𝔁𝓽');
            });

            it('ignores byte order marker (BOM)', function () {
                const decoder = new TextDecoder('utf-8', {ignoreBOM: true});
                const input = new Uint8Array([
                    0xef,
                    0xbb,
                    0xbf,
                    ...encodedMultibyteCharArray(),
                ]);

                expect(decoder.decode(input)).toBe('𝓽𝓮𝔁𝓽');
            });

            it('handles invalid byte order marker (BOM)', function () {
                const decoder = new TextDecoder('utf-8', {ignoreBOM: true});
                const input = new Uint8Array([
                    0xef,
                    0xbb,
                    0x89,
                    ...encodedMultibyteCharArray(),
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
                }).toThrowError(
                    TypeError,
                    /malformed UTF-8 character sequence/
                );
            });
        });

        describe('Multi-byte Encoding Converter (iconv)', function () {
            it('can decode Big-5', function () {
                const decoder = new TextDecoder('big5');
                const bytes = [
                    164, 164, 177, 192, 183, 124, 177, 181, 168, 252, 184, 103,
                    192, 217, 179, 161, 188, 208, 183, 199, 192, 203, 197, 231,
                    167, 189, 169, 101, 176, 85,
                ];

                const decoded = decoder.decode(new Uint8Array(bytes));
                expect(decoded).toEqual('中推會接受經濟部標準檢驗局委託');
            });

            it('can decode Big-5 with incorrect input bytes', function () {
                const decoder = new TextDecoder('big5');
                const bytes = [
                    164, 164, 177, 192, 183, 124,
                    // Invalid byte...
                    0xa1,
                ];

                const decoded = decoder.decode(new Uint8Array(bytes));
                expect(decoded).toEqual('中推會�');
            });

            it('can decode Big-5 with long incorrect input bytes', function () {
                const decoder = new TextDecoder('big5');
                const bytes = [164, 164, 177, 192, 183, 124];
                const baseLength = 1000;
                const longBytes = new Array(baseLength)
                    .fill(bytes, 0, baseLength)
                    .flat();

                // Append invalid byte sequence...
                longBytes.push(0xa3);

                const decoded = decoder.decode(new Uint8Array(longBytes));

                const baseResult = '中推會';
                const longResult = [
                    ...new Array(baseLength).fill(baseResult, 0, baseLength),
                    '�',
                ].join('');

                expect(decoded).toEqual(longResult);
            });

            it('can decode Big-5 HKSCS with supplemental characters', function () {
                // The characters below roughly mean 'hard' or 'solid' and
                // 'rooster' respectively. They were chosen for their Unicode
                // and HKSCS positioning, not meaning.

                // Big5-HKSCS bytes for the supplemental character 𠕇
                const supplementalBytes = [250, 64];
                // Big5-HKSCS bytes for the non-supplemental characters 公雞
                const nonSupplementalBytes = [164, 189, 194, 251];

                const decoder = new TextDecoder('big5-hkscs');

                // We currently allocate 12 additional bytes of padding
                // and a minimum of 256...

                // This should produce 400 non-supplemental bytes (50 * 2 * 4)
                // and 16 supplemental bytes (4 * 4)
                const repeatedNonSupplementalBytes = new Array(50).fill(nonSupplementalBytes).flat();
                const bytes = [
                    ...repeatedNonSupplementalBytes,
                    ...supplementalBytes,
                    ...repeatedNonSupplementalBytes,
                    ...supplementalBytes,
                    ...repeatedNonSupplementalBytes,
                    ...supplementalBytes,
                    ...repeatedNonSupplementalBytes,
                    ...supplementalBytes,
                ];

                const expectedNonSupplemental  = new Array(50).fill('公雞');
                const expected = [
                    ...expectedNonSupplemental,
                    '𠕇',
                    ...expectedNonSupplemental,
                    '𠕇',
                    ...expectedNonSupplemental,
                    '𠕇',
                    ...expectedNonSupplemental,
                    '𠕇',
                ].join('');

                // Calculate the number of bytes the UTF-16 characters should
                // occupy.
                const expectedU16Bytes = [...expected].reduce((prev, next) => {
                    const utf16code = next.codePointAt(0);

                    // Test whether this unit is supplemental
                    const additionalBytes = utf16code > 0xFFFF ? 2 : 0;

                    return prev + 2 + additionalBytes;
                }, 0);


                // We set a minimum buffer allocation of 256 bytes,
                // this ensures that this test exceeds that.
                expect(expectedU16Bytes / 2).toBeGreaterThan(256);

                // The length of the input bytes should always be less
                // than the expected output because UTF-16 uses 4 bytes
                // to represent some characters HKSCS needs only 2 for.
                expect(bytes.length).toBeLessThan(expectedU16Bytes);
                // 4 supplemental characters, each with two additional bytes.
                expect(bytes.length + 4 * 2).toBe(expectedU16Bytes);

                const decoded = decoder.decode(new Uint8Array(bytes));

                expect(decoded).toBe(expected);
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
                }).toThrowError(TypeError);
            });

            it('can decode ASCII', function () {
                const input = new Uint8Array([0x89, 0x95, 0x9f, 0xbf]);
                const decoder = new TextDecoder('ascii');
                expect(decoder.decode(input)).toBe('‰•Ÿ¿');
            });

            // Straight from https://encoding.spec.whatwg.org/encodings.json
            const encodingsTable = loadJSONFromResource(
                'resource:///org/gjs/jsunit/modules/encodings.json'
            );

            const singleByteEncodings = encodingsTable.filter(group => {
                return group.heading === 'Legacy single-byte encodings';
            })[0].encodings;

            const buffer = new ArrayBuffer(255);
            const view = new Uint8Array(buffer);

            for (let i = 0, l = view.byteLength; i < l; i++)
                view[i] = i;

            for (let i = 0, l = singleByteEncodings.length; i < l; i++) {
                const encoding = singleByteEncodings[i];

                it(`${encoding.name} can be decoded.`, function () {
                    for (const label of encoding.labels) {
                        const decoder = new TextDecoder(label);
                        expect(() => decoder.decode(view)).not.toThrow();
                        expect(decoder.encoding).toBe(
                            encoding.name.toLowerCase()
                        );
                    }
                });
            }
        });
    });
});
