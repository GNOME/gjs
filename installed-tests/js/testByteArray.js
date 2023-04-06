// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2010 litl, LLC
// SPDX-FileCopyrightText: 2017 Philip Chimento <philip.chimento@gmail.com>

const ByteArray = imports.byteArray;
const {GIMarshallingTests, GLib} = imports.gi;

describe('Byte array', function () {
    it('can be created from a string', function () {
        let a = ByteArray.fromString('abcd');
        expect(a.length).toEqual(4);
        [97, 98, 99, 100].forEach((val, ix) => expect(a[ix]).toEqual(val));
    });

    it('can be encoded from a string', function () {
        // Pick a string likely to be stored internally as Latin1
        let a = ByteArray.fromString('äbcd', 'LATIN1');
        expect(a.length).toEqual(4);
        [228, 98, 99, 100].forEach((val, ix) => expect(a[ix]).toEqual(val));

        // Try again with a string not likely to be Latin1 internally
        a = ByteArray.fromString('⅜', 'UTF-8');
        expect(a.length).toEqual(3);
        [0xe2, 0x85, 0x9c].forEach((val, ix) => expect(a[ix]).toEqual(val));
    });

    it('encodes as UTF-8 by default', function () {
        let a = ByteArray.fromString('⅜');
        expect(a.length).toEqual(3);
        [0xe2, 0x85, 0x9c].forEach((val, ix) => expect(a[ix]).toEqual(val));
    });

    it('can be converted to a string of ASCII characters', function () {
        let a = new Uint8Array(4);
        a[0] = 97;
        a[1] = 98;
        a[2] = 99;
        a[3] = 100;
        let s = ByteArray.toString(a);
        expect(s.length).toEqual(4);
        expect(s).toEqual('abcd');
    });

    it('can be converted to a string of UTF-8 characters even if it ends with a 0', function () {
        const a = Uint8Array.of(97, 98, 99, 100, 0);
        const s = ByteArray.toString(a);
        expect(s.length).toEqual(4);
        expect(s).toEqual('abcd');
    });

    it('can be converted to a string of encoded characters even with a 0 byte', function () {
        const a = Uint8Array.of(97, 98, 99, 100, 0);
        const s = ByteArray.toString(a, 'LATIN1');
        expect(s.length).toEqual(4);
        expect(s).toEqual('abcd');
    });

    it('stops converting to a string at an embedded 0 byte', function () {
        const a = Uint8Array.of(97, 98, 0, 99, 100);
        const s = ByteArray.toString(a);
        expect(s.length).toEqual(2);
        expect(s).toEqual('ab');
    });

    it('deals gracefully with a 0-length array', function () {
        const a = new Uint8Array(0);
        expect(ByteArray.toString(a)).toEqual('');
        expect(ByteArray.toGBytes(a).get_size()).toEqual(0);
    });

    it('deals gracefully with a 0-length GLib.Bytes', function () {
        const noBytes = ByteArray.toGBytes(new Uint8Array(0));
        expect(ByteArray.fromGBytes(noBytes).length).toEqual(0);
    });

    it('deals gracefully with a 0-length string', function () {
        expect(ByteArray.fromString('').length).toEqual(0);
        expect(ByteArray.fromString('', 'LATIN1').length).toEqual(0);
    });

    it('deals gracefully with a non Uint8Array', function () {
        const a = [97, 98, 99, 100, 0];
        expect(() => ByteArray.toString(a)).toThrow();
        expect(() => ByteArray.toGBytes(a)).toThrow();
    });

    describe('legacy toString() behavior', function () {
        beforeEach(function () {
            GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_WARNING,
                'Some code called array.toString()*');
        });

        it('is preserved when created from a string', function () {
            let a = ByteArray.fromString('⅜');
            expect(a.toString()).toEqual('⅜');
        });

        it('is preserved when marshalled from GI', function () {
            let a = GIMarshallingTests.bytearray_full_return();
            expect(a.toString()).toEqual('');
        });

        afterEach(function () {
            GLib.test_assert_expected_messages_internal('Gjs',
                'testByteArray.js', 0, 'testToStringCompatibility');
        });
    });
});
