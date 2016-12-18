const ByteArray = imports.byteArray;

describe('Byte array', function () {
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
            for (let i = 0; i < a.length; ++i) {
                expect(a[i]).toEqual(0);
            }
        });
    });

    it('assigns values correctly', function () {
        let a = new ByteArray.ByteArray(256);

        for (let i = 0; i < a.length; ++i) {
            a[i] = 255 - i;
        }

        for (let i = 0; i < a.length; ++i) {
            expect(a[i]).toEqual(255 - i);
        }
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

    it('can be created from a string', function () {
        let a = ByteArray.fromString('abcd');
        expect(a.length).toEqual(4);
        [97, 98, 99, 100].forEach((val, ix) => expect(a[ix]).toEqual(val));
    });

    it('can be created from an array', function () {
        let a = ByteArray.fromArray([ 1, 2, 3, 4 ]);
        expect(a.length).toEqual(4);
        [1, 2, 3, 4].forEach((val, ix) => expect(a[ix]).toEqual(val));
    });

    it('can be converted to a string of ASCII characters', function () {
        let a = new ByteArray.ByteArray();
        a[0] = 97;
        a[1] = 98;
        a[2] = 99;
        a[3] = 100;
        let s = a.toString();
        expect(s.length).toEqual(4);
        expect(s).toEqual('abcd');
    });
});
