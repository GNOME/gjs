// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2010 litl, LLC
// SPDX-FileCopyrightText: 2017 Philip Chimento <philip.chimento@gmail.com>

const ByteArray = imports.byteArray;
const {GIMarshallingTests, GjsTestTools, GLib} = imports.gi;

describe('Uint8Array with legacy ByteArray functions', function () {
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

    it('deals gracefully with a non-aligned GBytes', function () {
        const unalignedBytes = GjsTestTools.new_unaligned_bytes(48);
        const arr = ByteArray.fromGBytes(unalignedBytes);
        expect(arr.length).toEqual(48);
        expect(Array.prototype.slice.call(arr, 0, 4)).toEqual([1, 2, 3, 4]);
    });

    it('deals gracefully with a GBytes in static storage', function () {
        const staticBytes = GjsTestTools.new_static_bytes();
        const arr = ByteArray.fromGBytes(staticBytes);
        arr[2] = 42;
        expect(Array.from(arr)).toEqual([104, 101, 42, 108, 111, 0]);
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


describe('Legacy byte array object', function () {
    it('has length 0 for empty array', function () {
        let a = new ByteArray.ByteArray();
        expect(a.length).toEqual(0);
    });

    describe('initially sized to 10', function () {
        let a;
        beforeEach(function () {
            a = new ByteArray.ByteArray(10);
        });

        it('has length 10', function () {
            expect(a.length).toEqual(10);
        });

        it('is initialized to zeroes', function () {
            for (let i = 0; i < a.length; ++i)
                expect(a[i]).toEqual(0);
        });
    });

    it('assigns values correctly', function () {
        let a = new ByteArray.ByteArray(256);

        for (let i = 0; i < a.length; ++i)
            a[i] = 255 - i;

        for (let i = 0; i < a.length; ++i)
            expect(a[i]).toEqual(255 - i);
    });

    describe('assignment past end', function () {
        let a;
        beforeEach(function () {
            a = new ByteArray.ByteArray();
            a[2] = 5;
        });

        it('implicitly lengthens the array', function () {
            expect(a.length).toEqual(3);
            expect(a[2]).toEqual(5);
        });

        it('implicitly creates zero bytes', function () {
            expect(a[0]).toEqual(0);
            expect(a[1]).toEqual(0);
        });
    });

    it('changes the length when assigning to length property', function () {
        let a = new ByteArray.ByteArray(20);
        expect(a.length).toEqual(20);
        a.length = 5;
        expect(a.length).toEqual(5);
    });

    describe('conversions', function () {
        let a;
        beforeEach(function () {
            a = new ByteArray.ByteArray();
            a[0] = 255;
        });

        it('gives a byte 5 when assigning 5', function () {
            a[0] = 5;
            expect(a[0]).toEqual(5);
        });

        it('gives a byte 0 when assigning null', function () {
            a[0] = null;
            expect(a[0]).toEqual(0);
        });

        it('gives a byte 0 when assigning undefined', function () {
            a[0] = undefined;
            expect(a[0]).toEqual(0);
        });

        it('rounds off when assigning a double', function () {
            a[0] = 3.14;
            expect(a[0]).toEqual(3);
        });
    });

    it('can be created from an array', function () {
        let a = ByteArray.fromArray([1, 2, 3, 4]);
        expect(a.length).toEqual(4);
        [1, 2, 3, 4].forEach((val, ix) => expect(a[ix]).toEqual(val));
    });

    it('can be converted to a string of ASCII characters', function () {
        let a = new ByteArray.ByteArray(4);
        a[0] = 97;
        a[1] = 98;
        a[2] = 99;
        a[3] = 100;
        let s = a.toString();
        expect(s.length).toEqual(4);
        expect(s).toEqual('abcd');
    });

    it('can be passed in with transfer none', function () {
        const refByteArray = ByteArray.fromArray([0, 49, 0xFF, 51]);
        expect(() => GIMarshallingTests.bytearray_none_in(refByteArray)).not.toThrow();
    });
});
