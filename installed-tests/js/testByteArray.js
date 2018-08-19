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

    it('can be converted to a string of ASCII characters', function() {
        let a = new Uint8Array(4);
        a[0] = 97;
        a[1] = 98;
        a[2] = 99;
        a[3] = 100;
        let s = ByteArray.toString(a);
        expect(s.length).toEqual(4);
        expect(s).toEqual('abcd');
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
            expect(() => a.toString()).toThrowError(TypeError,
                /malformed UTF-8 character sequence/);
        });

        afterEach(function () {
            GLib.test_assert_expected_messages_internal('Gjs',
                'testByteArray.js', 0, 'testToStringCompatibility');
        });
    });
});
