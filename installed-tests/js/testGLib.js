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
            'asig', // nature
            new GLib.Variant('s', 'variant'),
            [7, 3],
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

describe('GLib string function overrides', function () {
    let numExpectedWarnings;

    function expectWarnings(count) {
        numExpectedWarnings = count;
        for (let c = 0; c < count; c++) {
            GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_WARNING,
                '*not introspectable*');
        }
    }

    function assertWarnings(testName) {
        for (let c = 0; c < numExpectedWarnings; c++) {
            GLib.test_assert_expected_messages_internal('Gjs', 'testGLib.js', 0,
                `test GLib.${testName}`);
        }
        numExpectedWarnings = 0;
    }

    beforeEach(function () {
        numExpectedWarnings = 0;
    });

    it('GLib.stpcpy', function () {
        expect(() => GLib.stpcpy('dest', 'src')).toThrowError(/not introspectable/);
    });

    it('GLib.strstr_len', function () {
        expectWarnings(4);
        expect(GLib.strstr_len('haystack', -1, 'needle')).toBeNull();
        expect(GLib.strstr_len('haystacks', -1, 'stack')).toEqual('stacks');
        expect(GLib.strstr_len('haystacks', 4, 'stack')).toBeNull();
        expect(GLib.strstr_len('haystack', 4, 'ays')).toEqual('aystack');
        assertWarnings('strstr_len');
    });

    it('GLib.strrstr', function () {
        expectWarnings(2);
        expect(GLib.strrstr('haystack', 'needle')).toBeNull();
        expect(GLib.strrstr('hackstacks', 'ack')).toEqual('acks');
        assertWarnings('strrstr');
    });

    it('GLib.strrstr_len', function () {
        expectWarnings(3);
        expect(GLib.strrstr_len('haystack', -1, 'needle')).toBeNull();
        expect(GLib.strrstr_len('hackstacks', -1, 'ack')).toEqual('acks');
        expect(GLib.strrstr_len('hackstacks', 4, 'ack')).toEqual('ackstacks');
        assertWarnings('strrstr_len');
    });

    it('GLib.strup', function () {
        expectWarnings(1);
        expect(GLib.strup('string')).toEqual('STRING');
        assertWarnings('strup');
    });

    it('GLib.strdown', function () {
        expectWarnings(1);
        expect(GLib.strdown('STRING')).toEqual('string');
        assertWarnings('strdown');
    });

    it('GLib.strreverse', function () {
        expectWarnings(1);
        expect(GLib.strreverse('abcdef')).toEqual('fedcba');
        assertWarnings('strreverse');
    });

    it('GLib.ascii_dtostr', function () {
        expectWarnings(2);
        expect(GLib.ascii_dtostr('', GLib.ASCII_DTOSTR_BUF_SIZE, Math.PI))
            .toEqual('3.141592653589793');
        expect(GLib.ascii_dtostr('', 4, Math.PI)).toEqual('3.14');
        assertWarnings('ascii_dtostr');
    });

    it('GLib.ascii_formatd', function () {
        expect(() => GLib.ascii_formatd('', 8, '%e', Math.PI)).toThrowError(/not introspectable/);
    });

    it('GLib.strchug', function () {
        expectWarnings(2);
        expect(GLib.strchug('text')).toEqual('text');
        expect(GLib.strchug('   text')).toEqual('text');
        assertWarnings('strchug');
    });

    it('GLib.strchomp', function () {
        expectWarnings(2);
        expect(GLib.strchomp('text')).toEqual('text');
        expect(GLib.strchomp('text   ')).toEqual('text');
        assertWarnings('strchomp');
    });

    it('GLib.strstrip', function () {
        expectWarnings(4);
        expect(GLib.strstrip('text')).toEqual('text');
        expect(GLib.strstrip('   text')).toEqual('text');
        expect(GLib.strstrip('text   ')).toEqual('text');
        expect(GLib.strstrip('   text   ')).toEqual('text');
        assertWarnings('strstrip');
    });

    it('GLib.strdelimit', function () {
        expectWarnings(4);
        expect(GLib.strdelimit('1a2b3c4', 'abc', '_'.charCodeAt())).toEqual('1_2_3_4');
        expect(GLib.strdelimit('1-2_3<4', null, '|'.charCodeAt())).toEqual('1|2|3|4');
        expect(GLib.strdelimit('1a2b3c4', 'abc', '_')).toEqual('1_2_3_4');
        expect(GLib.strdelimit('1-2_3<4', null, '|')).toEqual('1|2|3|4');
        assertWarnings('strdelimit');
    });

    it('GLib.strcanon', function () {
        expectWarnings(2);
        expect(GLib.strcanon('1a2b3c4', 'abc', '?'.charCodeAt())).toEqual('?a?b?c?');
        expect(GLib.strcanon('1a2b3c4', 'abc', '?')).toEqual('?a?b?c?');
        assertWarnings('strcanon');
    });
});
