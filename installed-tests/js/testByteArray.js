const ByteArray = imports.byteArray;

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

    it('can be created from an array', function () {
        let a = ByteArray.fromArray([ 1, 2, 3, 4 ]);
        expect(a.length).toEqual(4);
        [1, 2, 3, 4].forEach((val, ix) => expect(a[ix]).toEqual(val));
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
});
