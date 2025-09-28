// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2011 Giovanni Campagna <gcampagna@src.gnome.org>
// SPDX-FileCopyrightText: 2019, 2023 Philip Chimento <philip.chimento@gmail.com>

import GLib from 'gi://GLib';

let GLibUnix;
try {
    GLibUnix = (await import('gi://GLibUnix')).default;
} catch {}

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
        expect(Array.isArray(unpacked[4])).toBeTruthy();
        expect(unpacked[4].length).toEqual(2);
    });

    it('constructs a maybe variant', function () {
        let maybeVariant = new GLib.Variant('ms', null);
        expect(maybeVariant.deepUnpack()).toBeNull();

        maybeVariant = new GLib.Variant('ms', 'string');
        expect(maybeVariant.deepUnpack()).toEqual('string');
    });

    it('constructs a byte array variant', function () {
        const byteArray = new TextEncoder().encode('pizza');
        const byteArrayVariant = new GLib.Variant('ay', byteArray);
        expect(new TextDecoder().decode(byteArrayVariant.deepUnpack()))
            .toEqual('pizza');
    });

    it('constructs a byte array variant from a string', function () {
        const byteArrayVariant = new GLib.Variant('ay', 'pizza');
        expect(new TextDecoder().decode(byteArrayVariant.deepUnpack()))
            .toEqual('pizza\0');
    });

    it('0-terminates a byte array variant constructed from a string', function () {
        const byteArrayVariant = new GLib.Variant('ay', 'pizza');
        const a = byteArrayVariant.deepUnpack();
        [112, 105, 122, 122, 97, 0].forEach((val, ix) =>
            expect(a[ix]).toEqual(val));
    });

    it('does not 0-terminate a byte array variant constructed from a Uint8Array', function () {
        const byteArray = new TextEncoder().encode('pizza');
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

describe('GVariant strv', function () {
    let v;
    beforeEach(function () {
        v = new GLib.Variant('as', ['a', 'b', 'c', 'foo']);
    });

    it('unpacked matches constructed', function () {
        expect(v.deepUnpack()).toEqual(['a', 'b', 'c', 'foo']);
    });

    it('getter matches constructed', function () {
        expect(v.get_strv()).toEqual(['a', 'b', 'c', 'foo']);
    });

    it('getter (dup) matches constructed', function () {
        expect(v.dup_strv()).toEqual(['a', 'b', 'c', 'foo']);
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

describe('GLib spawn processes', function () {
    it('sync with null envp', function () {
        const [ret, stdout, stderr, exit_status] = GLib.spawn_sync(
            null, ['true'], null, GLib.SpawnFlags.SEARCH_PATH, null);
        expect(ret).toBe(true);
        expect(stdout).toEqual(new Uint8Array());
        expect(stderr).toEqual(new Uint8Array());
        expect(exit_status).toBe(0);
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

    // TODO: Add Regress.func_not_nullable_untyped_gpointer_in and move to testRegress.js
    it('GLib.str_hash errors when marshalling null to a not-nullable parameter', function () {
        // This tests that we don't marshal null to a not-nullable untyped gpointer.
        expect(() => GLib.str_hash(null)).toThrowError(
            /Argument [a-z]+ may not be null/
        );
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

    it('GLib.base64_encode', function () {
        const ascii = 'hello\0world';
        const base64 = 'aGVsbG8Ad29ybGQ=';

        expect(GLib.base64_encode(ascii)).toBe(base64);

        const encoded = new TextEncoder().encode(ascii);
        expect(GLib.base64_encode(encoded)).toBe(base64);
    });
});

describe('GLib.MatchInfo', function () {
    let shouldBePatchedProtoype;
    beforeAll(function () {
        shouldBePatchedProtoype = GLib.MatchInfo.prototype;
    });

    let regex;
    beforeEach(function () {
        regex = new GLib.Regex('h(?<foo>el)lo', 0, 0);
    });

    it('cannot be constructed', function () {
        expect(() => new GLib.MatchInfo()).toThrow();
        expect(() => new shouldBePatchedProtoype.constructor()).toThrow();
    });

    it('is returned from GLib.Regex.match', function () {
        const [, match] = regex.match('foo', 0);
        expect(match).toBeInstanceOf(GLib.MatchInfo);
        expect(match.toString()).toContain('GjsPrivate.MatchInfo');
    });

    it('stores the string that was matched', function () {
        const [, match] = regex.match('foo', 0);
        expect(match.get_string()).toEqual('foo');
    });

    it('truncates the string when it has zeroes as g_match_info_get_string() would', function () {
        const [, match] = regex.match_full('ab\0cd', 0, 0);
        expect(match.get_string()).toEqual('ab');
    });

    it('is returned from GLib.Regex.match_all', function () {
        const [, match] = regex.match_all('foo', 0);
        expect(match).toBeInstanceOf(GLib.MatchInfo);
        expect(match.toString()).toContain('GjsPrivate.MatchInfo');
    });

    it('is returned from GLib.Regex.match_all_full', function () {
        const [, match] = regex.match_all_full('foo', 0, 0);
        expect(match).toBeInstanceOf(GLib.MatchInfo);
        expect(match.toString()).toContain('GjsPrivate.MatchInfo');
    });

    it('is returned from GLib.Regex.match_full', function () {
        const [, match] = regex.match_full('foo', 0, 0);
        expect(match).toBeInstanceOf(GLib.MatchInfo);
        expect(match.toString()).toContain('GjsPrivate.MatchInfo');
    });

    describe('method', function () {
        let match;
        beforeEach(function () {
            [, match] = regex.match('hello hello world', 0);
        });

        it('expand_references', function () {
            expect(match.expand_references('\\0-\\1')).toBe('hello-el');
            expect(shouldBePatchedProtoype.expand_references.call(match, '\\0-\\1')).toBe('hello-el');
        });

        it('fetch', function () {
            expect(match.fetch(0)).toBe('hello');
            expect(shouldBePatchedProtoype.fetch.call(match, 0)).toBe('hello');
        });

        it('fetch_all', function () {
            expect(match.fetch_all()).toEqual(['hello', 'el']);
            expect(shouldBePatchedProtoype.fetch_all.call(match)).toEqual(['hello', 'el']);
        });

        it('fetch_named', function () {
            expect(match.fetch_named('foo')).toBe('el');
            expect(shouldBePatchedProtoype.fetch_named.call(match, 'foo')).toBe('el');
        });

        it('fetch_named_pos', function () {
            expect(match.fetch_named_pos('foo')).toEqual([true, 1, 3]);
            expect(shouldBePatchedProtoype.fetch_named_pos.call(match, 'foo')).toEqual([true, 1, 3]);
        });

        it('fetch_pos', function () {
            expect(match.fetch_pos(1)).toEqual([true, 1, 3]);
            expect(shouldBePatchedProtoype.fetch_pos.call(match, 1)).toEqual([true, 1, 3]);
        });

        it('get_match_count', function () {
            expect(match.get_match_count()).toBe(2);
            expect(shouldBePatchedProtoype.get_match_count.call(match)).toBe(2);
        });

        it('get_string', function () {
            expect(match.get_string()).toBe('hello hello world');
            expect(shouldBePatchedProtoype.get_string.call(match)).toBe('hello hello world');
        });

        it('is_partial_match', function () {
            expect(match.is_partial_match()).toBeFalse();
            expect(shouldBePatchedProtoype.is_partial_match.call(match)).toBeFalse();
        });

        it('matches', function () {
            expect(match.matches()).toBeTrue();
            expect(shouldBePatchedProtoype.matches.call(match)).toBeTrue();
        });

        it('next', function () {
            expect(match.next()).toBeTrue();
            expect(shouldBePatchedProtoype.next.call(match)).toBeFalse();
        });
    });
});

describe('GLibUnix functionality', function () {
    beforeEach(function () {
        if (!GLibUnix)
            pending('Not supported platform');
    });

    it('provides structs', function () {
        new GLibUnix.Pipe();
    });

    it('provides functions', function () {
        GLibUnix.fd_source_new(0, GLib.IOCondition.IN);
    });

    it('provides enums', function () {
        expect(GLibUnix.PipeEnd.READ).toBe(0);
        expect(GLibUnix.PipeEnd.WRITE).toBe(1);
    });
});

describe('GLibUnix compatibility fallback', function () {
    beforeEach(function () {
        if (!GLibUnix)
            pending('Not supported platform');
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_WARNING,
            '*GLib.* has been moved to a separate platform-specific library. ' +
            'Please update your code to use GLibUnix.* instead*');
    });

    it('provides structs', function () {
        new GLib.UnixPipe();
    });

    it('provides functions', function () {
        GLib.unix_fd_source_new(0, GLib.IOCondition.IN);
    });

    it('provides enums', function () {
        expect(GLib.UnixPipeEnd.READ).toBe(0);
        expect(GLib.UnixPipeEnd.WRITE).toBe(1);
    });

    afterEach(function () {
        GLib.test_assert_expected_messages_internal('Gjs', 'testGLib.js', 0,
            'GLib.Unix expect warning');
    });
});
