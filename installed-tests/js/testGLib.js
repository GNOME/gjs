const ByteArray = imports.byteArray;
const GLib = imports.gi.GLib;

describe('GVariant constructor', function () {
    it('constructs a string variant', function () {
        let strVariant = new GLib.Variant('s', 'mystring');
        expect(strVariant.get_string()[0]).toEqual('mystring');
        expect(strVariant.deepUnpack()).toEqual('mystring');
    });

    it('constructs a string variant (backwards compatible API)', function () {
        let strVariant = new GLib.Variant('s', 'mystring');
        let strVariantOld = GLib.Variant.new('s', 'mystring');
        expect(strVariant.equal(strVariantOld)).toBeTruthy();
    });

    it('constructs a struct variant', function () {
        let structVariant = new GLib.Variant('(sogvau)', [
            'a string',
            '/a/object/path',
            'asig', //nature
            new GLib.Variant('s', 'variant'),
            [7, 3]
        ]);
        expect(structVariant.n_children()).toEqual(5);

        let unpacked = structVariant.deepUnpack();
        expect(unpacked[0]).toEqual('a string');
        expect(unpacked[1]).toEqual('/a/object/path');
        expect(unpacked[2]).toEqual('asig');
        expect(unpacked[3] instanceof GLib.Variant).toBeTruthy();
        expect(unpacked[3].deepUnpack()).toEqual('variant');
        expect(unpacked[4] instanceof Array).toBeTruthy();
        expect(unpacked[4].length).toEqual(2);
    });

    it('constructs a maybe variant', function () {
        let maybeVariant = new GLib.Variant('ms', null);
        expect(maybeVariant.deepUnpack()).toBeNull();

        maybeVariant = new GLib.Variant('ms', 'string');
        expect(maybeVariant.deepUnpack()).toEqual('string');
    });

    it('constructs a byte array variant', function () {
        const byteArray = Uint8Array.from('pizza', c => c.charCodeAt(0));
        const byteArrayVariant = new GLib.Variant('ay', byteArray);
        expect(ByteArray.toString(byteArrayVariant.deepUnpack()))
            .toEqual('pizza');
    });

    it('constructs a byte array variant from a string', function () {
        const byteArrayVariant = new GLib.Variant('ay', 'pizza');
        expect(ByteArray.toString(byteArrayVariant.deepUnpack()))
            .toEqual('pizza');
    });

    it('0-terminates a byte array variant constructed from a string', function () {
        const byteArrayVariant = new GLib.Variant('ay', 'pizza');
        const a = byteArrayVariant.deepUnpack();
        [112, 105, 122, 122, 97, 0].forEach((val, ix) =>
            expect(a[ix]).toEqual(val));
    });

    it('does not 0-terminate a byte array variant constructed from a Uint8Array', function () {
        const byteArray = Uint8Array.from('pizza', c => c.charCodeAt(0));
        const byteArrayVariant = new GLib.Variant('ay', byteArray);
        const a = byteArrayVariant.deepUnpack();
        [112, 105, 122, 122, 97].forEach((val, ix) =>
            expect(a[ix]).toEqual(val));
    });
});

describe('GVariant unpack', function () {
    let v;
    beforeEach(function () {
        v = new GLib.Variant('a{sv}', {foo: new GLib.Variant('s', 'bar')});
    });

    it('preserves type information if the unpacked object contains variants', function () {
        expect(v.deepUnpack().foo instanceof GLib.Variant).toBeTruthy();
        expect(v.deep_unpack().foo instanceof GLib.Variant).toBeTruthy();
    });

    it('recursive leaves no variants in the unpacked object', function () {
        expect(v.recursiveUnpack().foo instanceof GLib.Variant).toBeFalsy();
        expect(v.recursiveUnpack().foo).toEqual('bar');
    });
});

describe('GVariantDict lookup', function () {
    let variantDict;
    beforeEach(function () {
        variantDict = new GLib.VariantDict(null);
        variantDict.insert_value('foo', GLib.Variant.new_string('bar'));
    });

    it('returns the unpacked variant', function () {
        expect(variantDict.lookup('foo')).toEqual('bar');
        expect(variantDict.lookup('foo', null)).toEqual('bar');
        expect(variantDict.lookup('foo', 's')).toEqual('bar');
        expect(variantDict.lookup('foo', new GLib.VariantType('s'))).toEqual('bar');
    });

    it("returns null if the key isn't present", function () {
        expect(variantDict.lookup('bar')).toBeNull();
        expect(variantDict.lookup('bar', null)).toBeNull();
        expect(variantDict.lookup('bar', 's')).toBeNull();
        expect(variantDict.lookup('bar', new GLib.VariantType('s'))).toBeNull();
    });
});
