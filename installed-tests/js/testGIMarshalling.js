// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2010 Collabora, Ltd.
// SPDX-FileCopyrightText: 2010 litl, LLC
// SPDX-FileCopyrightText: 2010 Giovanni Campagna <gcampagna@src.gnome.org>
// SPDX-FileCopyrightText: 2011 Red Hat, Inc.
// SPDX-FileCopyrightText: 2016 Endless Mobile, Inc.
// SPDX-FileCopyrightText: 2019, 2024 Philip Chimento <philip.chimento@gmail.com>

// We use Gio and GLib to have some objects that we know exist
import GIMarshallingTests from 'gi://GIMarshallingTests';
import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import GObject from 'gi://GObject';

// Some helpers to cut down on repetitive marshalling tests.
// - options.omit: the test doesn't exist, don't create a test case
// - options.skip: the test does exist, but doesn't pass, either unsupported or
//   a bug in GJS. Create the test case and mark it pending

function testReturnValue(root, value, {omit, skip, funcName = `${root}_return`} = {}) {
    if (omit)
        return;
    it('marshals as a return value', function () {
        if (skip)
            pending(skip);
        expect(GIMarshallingTests[funcName]()).toEqual(value);
    });
}

function testInParameter(root, value, {omit, skip, funcName = `${root}_in`} = {}) {
    if (omit)
        return;
    it('marshals as an in parameter', function () {
        if (skip)
            pending(skip);
        expect(() => GIMarshallingTests[funcName](value)).not.toThrow();
    });
}

function testOutParameter(root, value, {omit, skip, funcName = `${root}_out`} = {}) {
    if (omit)
        return;
    it('marshals as an out parameter', function () {
        if (skip)
            pending(skip);
        expect(GIMarshallingTests[funcName]()).toEqual(value);
    });
}

function testUninitializedOutParameter(root, defaultValue, {omit, skip, funcName = `${root}_out_uninitialized`} = {}) {
    if (omit)
        return;
    it("picks a reasonable default value when the function doesn't set the out parameter", function () {
        if (skip)
            pending(skip);
        const [success, defaultVal] = GIMarshallingTests[funcName]();
        expect(success).toBeFalse();
        expect(defaultVal).toEqual(defaultValue);
    });
}

function testInoutParameter(root, inValue, outValue,
    {omit, skip, funcName = `${root}_inout`} = {}) {
    if (omit)
        return;
    it('marshals as an inout parameter', function () {
        if (skip)
            pending(skip);
        expect(GIMarshallingTests[funcName](inValue)).toEqual(outValue);
    });
}

function testSimpleMarshalling(root, value, inoutValue, defaultValue, options = {}) {
    testReturnValue(root, value, options.returnv);
    testInParameter(root, value, options.in);
    testOutParameter(root, value, options.out);
    testUninitializedOutParameter(root, defaultValue, options.uninitOut);
    testInoutParameter(root, value, inoutValue, options.inout);
}

function testTransferMarshalling(root, value, inoutValue, defaultValue, options = {}) {
    describe('with transfer none', function () {
        testSimpleMarshalling(`${root}_none`, value, inoutValue, defaultValue, options.none);
    });
    describe('with transfer full', function () {
        const fullOptions = {
            inout: {
                skip: 'https://gitlab.gnome.org/GNOME/gobject-introspection/issues/192',
            },
        };
        Object.assign(fullOptions, options.full);
        testSimpleMarshalling(`${root}_full`, value, inoutValue, defaultValue, fullOptions);
    });
}

function testContainerMarshalling(root, value, inoutValue, defaultValue, options = {}) {
    testTransferMarshalling(root, value, inoutValue, defaultValue, options);
    describe('with transfer container', function () {
        const containerOptions = {
            in: {
                skip: 'https://gitlab.gnome.org/GNOME/gjs/issues/44',
            },
            inout: {
                skip: 'https://gitlab.gnome.org/GNOME/gjs/issues/44',
            },
        };
        Object.assign(containerOptions, options.container);
        testSimpleMarshalling(`${root}_container`, value, inoutValue, defaultValue, containerOptions);
    });
}

// Integer limits, defined without reference to GLib (because the GLib.MAXINT8
// etc. constants are also subject to marshalling)
const Limits = {
    int8: {
        min: -(2 ** 7),
        max: 2 ** 7 - 1,
        umax: 2 ** 8 - 1,
    },
    int16: {
        min: -(2 ** 15),
        max: 2 ** 15 - 1,
        umax: 2 ** 16 - 1,
    },
    int32: {
        min: -(2 ** 31),
        max: 2 ** 31 - 1,
        umax: 2 ** 32 - 1,
    },
    int64: {
        min: -(2 ** 63),
        max: 2 ** 63 - 1,
        umax: 2 ** 64 - 1,
        bit64: true,  // note: unsafe, values will not be accurate!
    },
    short: {},
    int: {},
    long: {},
    ssize: {
        utype: 'size',
    },
};
const BigIntLimits = {
    int64: {
        min: -(2n ** 63n),
        max: 2n ** 63n - 1n,
        umax: 2n ** 64n - 1n,
    },
};

Object.assign(Limits.short, Limits.int16);
Object.assign(Limits.int, Limits.int32);
// Platform dependent sizes; expand definitions as needed
if (GLib.SIZEOF_LONG === 8) {
    Object.assign(Limits.long, Limits.int64);
    BigIntLimits.long = Object.assign({}, BigIntLimits.int64);
} else {
    Object.assign(Limits.long, Limits.int32);
}
if (GLib.SIZEOF_SSIZE_T === 8) {
    Object.assign(Limits.ssize, Limits.int64);
    BigIntLimits.ssize = Object.assign({utype: 'size'}, BigIntLimits.int64);
} else {
    Object.assign(Limits.ssize, Limits.int32);
}

// Functions for dealing with tests that require or return unsafe 64-bit ints,
// until we get BigInts.

// Sometimes tests pass if we are comparing two inaccurate values in JS with
// each other. That's fine for now. Then we just have to suppress the warnings.
function warn64(is64bit, func, ...args) {
    if (is64bit) {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_WARNING,
            '*cannot be safely stored*');
    }
    const retval = func(...args);
    if (is64bit) {
        GLib.test_assert_expected_messages_internal('Gjs',
            'testGIMarshalling.js', 0, 'Ignore message');
    }
    return retval;
}

// Other times we compare an inaccurate value marshalled from JS into C, with an
// accurate value in C. Those tests we have to skip.
function skip64(is64bit) {
    if (is64bit)
        pending('https://gitlab.gnome.org/GNOME/gjs/issues/271');
}

describe('Boolean', function () {
    [true, false].forEach(bool => {
        describe(`${bool}`, function () {
            testSimpleMarshalling('boolean', bool, !bool, false, {
                returnv: {
                    funcName: `boolean_return_${bool}`,
                },
                in: {
                    funcName: `boolean_in_${bool}`,
                },
                out: {
                    funcName: `boolean_out_${bool}`,
                },
                uninitOut: {
                    omit: true,
                },
                inout: {
                    funcName: `boolean_inout_${bool}_${!bool}`,
                },
            });
        });
    });

    testUninitializedOutParameter('boolean', false);
});

describe('Integer', function () {
    Object.entries(Limits).forEach(([type, {min, max, umax, bit64, utype = `u${type}`}]) => {
        describe(`${type}-typed`, function () {
            it('marshals signed value as a return value', function () {
                expect(warn64(bit64, GIMarshallingTests[`${type}_return_max`])).toEqual(max);
                expect(warn64(bit64, GIMarshallingTests[`${type}_return_min`])).toEqual(min);
            });

            it('marshals signed value as an in parameter', function () {
                skip64(bit64);
                expect(() => GIMarshallingTests[`${type}_in_max`](max)).not.toThrow();
                expect(() => GIMarshallingTests[`${type}_in_min`](min)).not.toThrow();
            });

            it('marshals signed value as an out parameter', function () {
                expect(warn64(bit64, GIMarshallingTests[`${type}_out_max`])).toEqual(max);
                expect(warn64(bit64, GIMarshallingTests[`${type}_out_min`])).toEqual(min);
            });

            testUninitializedOutParameter(type, 0);

            it('marshals as an inout parameter', function () {
                skip64(bit64);
                expect(GIMarshallingTests[`${type}_inout_max_min`](max)).toEqual(min);
                expect(GIMarshallingTests[`${type}_inout_min_max`](min)).toEqual(max);
            });

            it('marshals unsigned value as a return value', function () {
                expect(warn64(bit64, GIMarshallingTests[`${utype}_return`])).toEqual(umax);
            });

            it('marshals unsigned value as an in parameter', function () {
                skip64(bit64);
                expect(() => GIMarshallingTests[`${utype}_in`](umax)).not.toThrow();
            });

            it('marshals unsigned value as an out parameter', function () {
                expect(warn64(bit64, GIMarshallingTests[`${utype}_out`])).toEqual(umax);
            });

            testUninitializedOutParameter(utype, 0);

            it('marshals unsigned value as an inout parameter', function () {
                skip64(bit64);
                expect(GIMarshallingTests[`${utype}_inout`](umax)).toEqual(0);
            });
        });
    });
});

describe('BigInt', function () {
    Object.entries(BigIntLimits).forEach(([type, {min, max, umax, utype = `u${type}`}]) => {
        describe(`${type}-typed`, function () {
            it('marshals signed value as an in parameter', function () {
                expect(() => GIMarshallingTests[`${type}_in_max`](max)).not.toThrow();
                expect(() => GIMarshallingTests[`${type}_in_min`](min)).not.toThrow();
            });

            it('marshals unsigned value as an in parameter', function () {
                expect(() => GIMarshallingTests[`${utype}_in`](umax)).not.toThrow();
            });
        });
    });
});

describe('Floating point', function () {
    const FloatLimits = {
        float: {
            min: 2 ** -126,
            max: (2 - 2 ** -23) * 2 ** 127,
        },
        double: {
            // GLib.MINDOUBLE is the minimum normal value, which is not the same
            // as the minimum denormal value Number.MIN_VALUE
            min: 2 ** -1022,
            max: Number.MAX_VALUE,
        },
    };

    Object.entries(FloatLimits).forEach(([type, {min, max}]) => {
        describe(`${type}-typed`, function () {
            it('marshals value as a return value', function () {
                expect(GIMarshallingTests[`${type}_return`]()).toBeCloseTo(max, 10);
            });

            testInParameter(type, max);

            it('marshals value as an out parameter', function () {
                expect(GIMarshallingTests[`${type}_out`]()).toBeCloseTo(max, 10);
            });

            testUninitializedOutParameter(type, 0);

            it('marshals value as an inout parameter', function () {
                expect(GIMarshallingTests[`${type}_inout`](max)).toBeCloseTo(min, 10);
            });

            it('can handle noncanonical NaN', function () {
                expect(GIMarshallingTests[`${type}_noncanonical_nan_out`]()).toBeNaN();
            });
        });
    });
});

describe('time_t', function () {
    testSimpleMarshalling('time_t', 1234567890, 0, 0);
});

describe('off_t', function () {
    testSimpleMarshalling('off_t', 1234567890, 0, 0);
});

function testUnixIntegerTypedefMarshalling(type, inValue, skipAny = {}) {
    describe(type, function () {
        const skip = GIMarshallingTests[`${type}_in`] ? false : 'Only supported on Unix';
        testSimpleMarshalling(type, inValue, 0, 0, {
            returnv: {skip: skip || skipAny.skipReturn},
            in: {skip: skip || skipAny.skipIn},
            out: {skip: skip || skipAny.skipOut},
            uninitOut: {skip: skip || skipAny.skipUninitOut},
            inout: {skip: skip || skipAny.skipInOut},
        });
    });
}

// https://gitlab.gnome.org/GNOME/gjs/-/issues/673
testUnixIntegerTypedefMarshalling('dev_t', 1234567890, {skipInOut: true});
testUnixIntegerTypedefMarshalling('gid_t', 65534);
testUnixIntegerTypedefMarshalling('pid_t', 12345);
testUnixIntegerTypedefMarshalling('socklen_t', 123);
testUnixIntegerTypedefMarshalling('uid_t', 65534);

describe('GType', function () {
    describe('void', function () {
        testSimpleMarshalling('gtype', GObject.TYPE_NONE, GObject.TYPE_INT, null);
    });

    describe('string', function () {
        testSimpleMarshalling('gtype_string', GObject.TYPE_STRING, null, null, {
            inout: {omit: true},
            uninitOut: {omit: true},
        });
    });

    it('can be implicitly converted from a GObject type alias', function () {
        expect(() => GIMarshallingTests.gtype_in(GObject.VoidType)).not.toThrow();
    });

    it('can be implicitly converted from a JS type', function () {
        expect(() => GIMarshallingTests.gtype_string_in(String)).not.toThrow();
    });
});

describe('UTF-8 string', function () {
    testTransferMarshalling('utf8', 'const ♥ utf8', '', null, {
        full: {
            uninitOut: {omit: true}, // covered by utf8_dangling_out() test below
        },
    });

    it('marshals value as a byte array', function () {
        expect(() => GIMarshallingTests.utf8_as_uint8array_in('const ♥ utf8')).not.toThrow();
    });

    it('makes a default out value for a broken C function', function () {
        expect(GIMarshallingTests.utf8_dangling_out()).toBeNull();
    });
});

describe('In-out array in the style of gtk_init()', function () {
    it('marshals null', function () {
        const [, newArray] = GIMarshallingTests.init_function(null);
        expect(newArray).toEqual([]);
    });

    it('marshals an inout empty array', function () {
        const [ret, newArray] = GIMarshallingTests.init_function([]);
        expect(ret).toBeTrue();
        expect(newArray).toEqual([]);
    });

    it('marshals an inout array', function () {
        const [ret, newArray] = GIMarshallingTests.init_function(['--foo', '--bar']);
        expect(ret).toBeTrue();
        expect(newArray).toEqual(['--foo']);
    });
});

describe('Fixed-size C array', function () {
    describe('of ints', function () {
        testReturnValue('array_fixed_int', [-1, 0, 1, 2]);
        testInParameter('array_fixed_int', [-1, 0, 1, 2]);
        testOutParameter('array_fixed', [-1, 0, 1, 2]);
        testUninitializedOutParameter('array_fixed', null);
        testOutParameter('array_fixed_caller_allocated', [-1, 0, 1, 2]);
        testInoutParameter('array_fixed', [-1, 0, 1, 2], [2, 1, 0, -1]);
    });

    describe('of shorts', function () {
        testReturnValue('array_fixed_short', [-1, 0, 1, 2]);
        testInParameter('array_fixed_short', [-1, 0, 1, 2]);
    });

    it('marshals a struct array as an out parameter', function () {
        expect(GIMarshallingTests.array_fixed_out_struct()).toEqual([
            jasmine.objectContaining({long_: 7, int8: 6}),
            jasmine.objectContaining({long_: 6, int8: 7}),
        ]);
    });

    it('picks a reasonable default for struct array out param when uninitialized', function () {
        expect(GIMarshallingTests.array_fixed_out_struct_uninitialized()).toEqual([false, null]);
    });

    it('marshals a fixed-size struct array as caller allocated out param', function () {
        expect(GIMarshallingTests.array_fixed_caller_allocated_struct_out()).toEqual([
            jasmine.objectContaining({long_: -2, int8: -1}),
            jasmine.objectContaining({long_: 1, int8: 2}),
            jasmine.objectContaining({long_: 3, int8: 4}),
            jasmine.objectContaining({long_: 5, int8: 6}),
        ]);
    });

    for (const marshal of ['return', 'out']) {
        it(`handles a ${marshal} array with odd alignment`, function () {
            const arr = GIMarshallingTests[`array_fixed_${marshal}_unaligned`]();
            expect(arr.length).toEqual(32);
            expect(Array.prototype.slice.call(arr, 0, 4)).toEqual([1, 2, 3, 4]);
            GIMarshallingTests.cleanup_unaligned_buffer();
        });
    }
});

describe('C array with length', function () {
    function createStructArray(StructType = GIMarshallingTests.BoxedStruct) {
        return [1, 2, 3].map(num => {
            let struct = new StructType();
            struct.long_ = num;
            return struct;
        });
    }

    testSimpleMarshalling('array', [-1, 0, 1, 2], [-2, -1, 0, 1, 2], []);

    it('can be returned along with other arguments', function () {
        let [array, sum] = GIMarshallingTests.array_return_etc(9, 5);
        expect(sum).toEqual(14);
        expect(array).toEqual([9, 0, 1, 5]);
    });

    it('can be passed to a function with its length parameter before it', function () {
        expect(() => GIMarshallingTests.array_in_len_before([-1, 0, 1, 2]))
            .not.toThrow();
    });

    it('can be passed to a function with zero terminator', function () {
        expect(() => GIMarshallingTests.array_in_len_zero_terminated([-1, 0, 1, 2]))
            .not.toThrow();
    });

    describe('of strings', function () {
        testInParameter('array_string', ['foo', 'bar']);
    });

    it('marshals a byte array as an in parameter', function () {
        expect(() => GIMarshallingTests.array_uint8_in('abcd')).not.toThrow();
        expect(() => GIMarshallingTests.array_uint8_in([97, 98, 99, 100])).not.toThrow();
        expect(() => GIMarshallingTests.array_uint8_in(new TextEncoder().encode('abcd')))
            .not.toThrow();
    });

    describe('of signed 64-bit ints', function () {
        testInParameter('array_int64', [-1, 0, 1, 2]);
    });

    describe('of unsigned 64-bit ints', function () {
        testInParameter('array_uint64', [-1, 0, 1, 2]);
    });

    describe('of unichars', function () {
        testInParameter('array_unichar', 'const ♥ utf8');
        testOutParameter('array_unichar', 'const ♥ utf8');

        it('marshals from an array of codepoints', function () {
            const codepoints = [...'const ♥ utf8'].map(c => c.codePointAt(0));
            expect(() => GIMarshallingTests.array_unichar_in(codepoints)).not.toThrow();
        });
    });

    describe('of booleans', function () {
        testInParameter('array_bool', [true, false, true, true]);
        testOutParameter('array_bool', [true, false, true, true]);

        it('marshals from an array of numbers', function () {
            expect(() => GIMarshallingTests.array_bool_in([-1, 0, 1, 2])).not.toThrow();
        });
    });

    describe('of boxed structs', function () {
        testInParameter('array_struct', createStructArray());

        describe('passed by value', function () {
            testInParameter('array_struct_value', createStructArray(), {
                skip: 'https://gitlab.gnome.org/GNOME/gjs/issues/44',
            });
        });
    });

    describe('of simple structs', function () {
        testInParameter('array_simple_struct',
            createStructArray(GIMarshallingTests.SimpleStruct), {
                skip: 'https://gitlab.gnome.org/GNOME/gjs/issues/44',
            });
    });

    it('marshals two arrays with the same length parameter', function () {
        const keys = ['one', 'two', 'three'];
        const values = [1, 2, 3];
        expect(() => GIMarshallingTests.multi_array_key_value_in(keys, values)).not.toThrow();
    });

    // Run twice to ensure that copies are correctly made for (transfer full)
    it('copies correctly on transfer full', function () {
        let array = createStructArray();
        expect(() => {
            GIMarshallingTests.array_struct_take_in(array);
            GIMarshallingTests.array_struct_take_in(array);
        }).not.toThrow();
    });

    describe('of enums', function () {
        testInParameter('array_enum', [
            GIMarshallingTests.Enum.VALUE1,
            GIMarshallingTests.Enum.VALUE2,
            GIMarshallingTests.Enum.VALUE3,
        ]);
    });

    describe('of flags', function () {
        testInParameter('array_flags', [
            GIMarshallingTests.Flags.VALUE1,
            GIMarshallingTests.Flags.VALUE2,
            GIMarshallingTests.Flags.VALUE3,
        ]);
    });

    it('marshals an array with a 64-bit length parameter', function () {
        expect(() => GIMarshallingTests.array_in_guint64_len([-1, 0, 1, 2])).not.toThrow();
    });

    it('marshals an array with an 8-bit length parameter', function () {
        expect(() => GIMarshallingTests.array_in_guint8_len([-1, 0, 1, 2])).not.toThrow();
    });

    it('can be an in-out argument', function () {
        const array = GIMarshallingTests.array_inout([-1, 0, 1, 2]);
        expect(array).toEqual([-2, -1, 0, 1, 2]);
    });

    it('can be an out argument along with other arguments', function () {
        let [array, sum] = GIMarshallingTests.array_out_etc(9, 5);
        expect(sum).toEqual(14);
        expect(array).toEqual([9, 0, 1, 5]);
    });

    it('can be an in-out argument along with other arguments', function () {
        let [array, sum] = GIMarshallingTests.array_inout_etc(9, [-1, 0, 1, 2], 5);
        expect(sum).toEqual(14);
        expect(array).toEqual([9, -1, 0, 1, 5]);
    });

    it('does not interpret an unannotated integer as a length parameter', function () {
        expect(() => GIMarshallingTests.array_in_nonzero_nonlen(42, 'abcd')).not.toThrow();
    });

    for (const marshal of ['return', 'out']) {
        it(`handles a ${marshal} array with odd alignment`, function () {
            const arr = GIMarshallingTests[`array_${marshal}_unaligned`]();
            expect(arr.length).toEqual(32);
            expect(Array.prototype.slice.call(arr, 0, 4)).toEqual([1, 2, 3, 4]);
            GIMarshallingTests.cleanup_unaligned_buffer();
        });
    }

    it('supports optional inout array with length', function () {
        expect(GIMarshallingTests.length_array_utf8_optional_inout(['🅰', 'β', 'c', 'd']))
            .toEqual(['a', 'b', '¢', '🔠']);
        expect(GIMarshallingTests.length_array_utf8_optional_inout([])).toEqual(['a', 'b']);
    });
});

describe('Zero-terminated C array', function () {
    describe('of strings', function () {
        testSimpleMarshalling('array_zero_terminated', ['0', '1', '2'],
            ['-1', '0', '1', '2'], null);
    });

    it('marshals null as a zero-terminated array return value', function () {
        expect(GIMarshallingTests.array_zero_terminated_return_null()).toEqual(null);
    });

    it('marshals an array of structs as a return value', function () {
        let structArray = GIMarshallingTests.array_zero_terminated_return_struct();
        expect(structArray.map(e => e.long_)).toEqual([42, 43, 44]);
    });

    it('marshals an array of sequential structs as a return value', function () {
        let structArray = GIMarshallingTests.array_zero_terminated_return_sequential_struct();
        expect(structArray.map(e => e.long_)).toEqual([42, 43, 44]);
    });

    it('marshals an array of unichars as a return value', function () {
        expect(GIMarshallingTests.array_zero_terminated_return_unichar())
            .toEqual('const ♥ utf8');
    });

    describe('of GLib.Variants', function () {
        let variantArray;

        beforeEach(function () {
            variantArray = [
                new GLib.Variant('i', 27),
                new GLib.Variant('s', 'Hello'),
            ];
        });

        ['none', 'container', 'full'].forEach(transfer => {
            it(`marshals as a transfer-${transfer} in and out parameter`, function () {
                const returnedArray =
                    GIMarshallingTests[`array_gvariant_${transfer}_in`](variantArray);
                expect(returnedArray.map(v => v.deepUnpack())).toEqual([27, 'Hello']);
            });
        });
    });

    for (const marshal of ['return', 'out']) {
        it(`handles a ${marshal} array with odd alignment`, function () {
            const arr = GIMarshallingTests[`array_zero_terminated_${marshal}_unaligned`]();
            expect(Array.from(arr)).toEqual([1, 2, 3, 4, 5, 6, 7]);
            GIMarshallingTests.cleanup_unaligned_buffer();
        });
    }
});

describe('Exhaustive test of UTF-8 sequences', function () {
    ['length', 'fixed', 'zero_terminated'].forEach(arrayKind =>
        ['none', 'container', 'full'].forEach(transfer => {
            const testFunction = returnMode => {
                const commonName = 'array_utf8';
                const funcName = [arrayKind, commonName, transfer, returnMode].join('_');
                return GIMarshallingTests[funcName];
            };

            ['out', 'return'].forEach(returnMode =>
                it(`${arrayKind} ${returnMode} transfer ${transfer}`, function () {
                    const func = testFunction(returnMode);
                    expect(func()).toEqual(['a', 'b', '¢', '🔠']);
                }));

            it(`${arrayKind} in transfer ${transfer}`, function () {
                const func = testFunction('in');
                if (transfer === 'container')
                    pending('https://gitlab.gnome.org/GNOME/gjs/-/issues/44');

                expect(() => func(['🅰', 'β', 'c', 'd'])).not.toThrow();
            });

            it(`${arrayKind} inout transfer ${transfer}`, function () {
                const func = testFunction('inout');
                if (transfer === 'container')
                    pending('https://gitlab.gnome.org/GNOME/gjs/-/issues/44');

                expect(func(['🅰', 'β', 'c', 'd'])).toEqual(['a', 'b', '¢', '🔠']);
            });
        }));
});

describe('GArray', function () {
    describe('of ints with transfer none', function () {
        testReturnValue('garray_int_none', [-1, 0, 1, 2]);
        testInParameter('garray_int_none', [-1, 0, 1, 2]);
    });

    it('marshals BigInt int64s as a transfer-none in value', function () {
        GIMarshallingTests.garray_uint64_none_in([0, BigIntLimits.int64.umax]);
    });

    it('marshals int64s as a transfer-none return value', function () {
        expect(warn64(true, GIMarshallingTests.garray_uint64_none_return))
            .toEqual([0, Limits.int64.umax]);
    });

    describe('of strings', function () {
        testContainerMarshalling('garray_utf8', ['0', '1', '2'], ['-2', '-1', '0', '1'], null);

        it('marshals as a transfer-full caller-allocated out parameter', function () {
            expect(GIMarshallingTests.garray_utf8_full_out_caller_allocated())
                .toEqual(['0', '1', '2']);
        }).pend('https://gitlab.gnome.org/GNOME/gjs/issues/106');

        // https://gitlab.gnome.org/GNOME/gjs/-/issues/344
        // the test should be replaced with the one above when issue
        // https://gitlab.gnome.org/GNOME/gjs/issues/106 is fixed.
        it('marshals as a transfer-full caller-allocated out parameter throws errors', function () {
            // should throw when called, not when the function object is created
            expect(() => GIMarshallingTests.garray_utf8_full_out_caller_allocated).not.toThrow();
            expect(() => GIMarshallingTests.garray_utf8_full_out_caller_allocated()).toThrow();
        });
    });

    it('marshals boxed structs as a transfer-full return value', function () {
        expect(GIMarshallingTests.garray_boxed_struct_full_return().map(e => e.long_))
            .toEqual([42, 43, 44]);
    });

    describe('of booleans with transfer none', function () {
        testInParameter('garray_bool_none', [-1, 0, 1, 2]);
    });

    describe('of unichars', function () {
        it('can be passed in with transfer none', function () {
            expect(() => GIMarshallingTests.garray_unichar_none_in('const \u2665 utf8'))
                .not.toThrow();
            expect(() => GIMarshallingTests.garray_unichar_none_in([0x63, 0x6f, 0x6e,
                0x73, 0x74, 0x20, 0x2665, 0x20, 0x75, 0x74, 0x66, 0x38])).not.toThrow();
        });
    });
});

describe('GPtrArray', function () {
    describe('of strings', function () {
        testContainerMarshalling('gptrarray_utf8', ['0', '1', '2'], ['-2', '-1', '0', '1'], null);
    });

    describe('of structs', function () {
        it('can be returned with transfer full', function () {
            expect(GIMarshallingTests.gptrarray_boxed_struct_full_return().map(e => e.long_))
                .toEqual([42, 43, 44]);
        });
    });
});

describe('GByteArray', function () {
    const refByteArray = Uint8Array.from([0, 49, 0xFF, 51]);

    testReturnValue('bytearray_full', refByteArray);
    testOutParameter('bytearray_full', refByteArray);
    testInoutParameter('bytearray_full', refByteArray, Uint8Array.from([104, 101, 108, 0, 0xFF]));

    it('can be passed in with transfer none', function () {
        expect(() => GIMarshallingTests.bytearray_none_in(refByteArray))
            .not.toThrow();
        expect(() => GIMarshallingTests.bytearray_none_in([0, 49, 0xFF, 51]))
            .not.toThrow();
    });
});

describe('GBytes', function () {
    const refByteArray = Uint8Array.from([0, 49, 0xFF, 51]);

    it('marshals as a transfer-full return value', function () {
        expect(GIMarshallingTests.gbytes_full_return().toArray()).toEqual(refByteArray);
    });

    it('can be created from an array and passed in', function () {
        let bytes = GLib.Bytes.new([0, 49, 0xFF, 51]);
        expect(() => GIMarshallingTests.gbytes_none_in(bytes)).not.toThrow();
    });

    it('can be created by returning from a function and passed in', function () {
        var bytes = GIMarshallingTests.gbytes_full_return();
        expect(() => GIMarshallingTests.gbytes_none_in(bytes)).not.toThrow();
        expect(bytes.toArray()).toEqual(refByteArray);
    });

    it('can be implicitly converted from a Uint8Array', function () {
        expect(() => GIMarshallingTests.gbytes_none_in(refByteArray))
            .not.toThrow();
    });

    it('can be created from a string and is encoded in UTF-8', function () {
        let bytes = GLib.Bytes.new('const \u2665 utf8');
        expect(() => GIMarshallingTests.utf8_as_uint8array_in(bytes.toArray()))
            .not.toThrow();
    });

    it('cannot be passed to a function expecting a byte array', function () {
        let bytes = GLib.Bytes.new([97, 98, 99, 100]);
        expect(() => GIMarshallingTests.array_uint8_in(bytes.toArray())).not.toThrow();
        expect(() => GIMarshallingTests.array_uint8_in(bytes)).toThrow();
    });
});

describe('GStrv', function () {
    testSimpleMarshalling('gstrv', ['0', '1', '2'], ['-1', '0', '1', '2'], null);
});

describe('Array of GStrv', function () {
    ['length', 'fixed', 'zero_terminated'].forEach(arrayKind =>
        ['none', 'container', 'full'].forEach(transfer => {
            const testFunction = returnMode => {
                const commonName = 'array_of_gstrv_transfer';
                const funcName = [arrayKind, commonName, transfer, returnMode].join('_');
                return GIMarshallingTests[funcName];
            };

            ['out', 'return'].forEach(returnMode =>
                it(`${arrayKind} ${returnMode} transfer ${transfer}`, function () {
                    const func = testFunction(returnMode);
                    expect(func()).toEqual([
                        ['0', '1', '2'], ['3', '4', '5'], ['6', '7', '8'],
                    ]);
                }));

            it(`${arrayKind} in transfer ${transfer}`, function () {
                const func = testFunction('in');
                if (transfer === 'container')
                    pending('https://gitlab.gnome.org/GNOME/gjs/-/issues/44');

                expect(() => func([
                    ['0', '1', '2'], ['3', '4', '5'], ['6', '7', '8'],
                ])).not.toThrow();
            });

            it(`${arrayKind} inout transfer ${transfer}`, function () {
                const func = testFunction('inout');

                if (transfer === 'container')
                    pending('https://gitlab.gnome.org/GNOME/gjs/-/issues/44');

                const expectedReturn = [
                    ['-1', '0', '1', '2'], ['-1', '3', '4', '5'], ['-1', '6', '7', '8'],
                ];

                if (arrayKind !== 'fixed')
                    expectedReturn.push(['-1', '9', '10', '11']);

                expect(func([
                    ['0', '1', '2'], ['3', '4', '5'], ['6', '7', '8'],
                ])).toEqual(expectedReturn);
            });
        }));
});

['GList', 'GSList'].forEach(listKind => {
    const list = listKind.toLowerCase();

    describe(listKind, function () {
        describe('of ints with transfer none', function () {
            testReturnValue(`${list}_int_none`, [-1, 0, 1, 2]);
            testInParameter(`${list}_int_none`, [-1, 0, 1, 2]);
        });

        if (listKind === 'GList') {
            describe('of unsigned 32-bit ints with transfer none', function () {
                testReturnValue('glist_uint32_none', [0, Limits.int32.umax]);
                testInParameter('glist_uint32_none', [0, Limits.int32.umax]);
            });
        }

        describe('of strings', function () {
            testContainerMarshalling(`${list}_utf8`, ['0', '1', '2'],
                ['-2', '-1', '0', '1'], []);
        });
    });
});

describe('GHashTable', function () {
    const numberDict = {
        '-1': -0.1,
        0: 0,
        1: 0.1,
        2: 0.2,
    };

    describe('with integer values', function () {
        const intDict = {
            '-1': 1,
            0: 0,
            1: -1,
            2: -2,
        };
        testReturnValue('ghashtable_int_none', intDict);
        testInParameter('ghashtable_int_none', intDict);
    });

    describe('with string values', function () {
        const stringDict = {
            '-1': '1',
            0: '0',
            1: '-1',
            2: '-2',
        };
        const stringDictOut = {
            '-1': '1',
            0: '0',
            1: '1',
        };
        testContainerMarshalling('ghashtable_utf8', stringDict, stringDictOut, null);
    });

    describe('with double values', function () {
        testInParameter('ghashtable_double', numberDict);
    });

    describe('with float values', function () {
        testInParameter('ghashtable_float', numberDict);
    });

    describe('with 64-bit int values', function () {
        const int64Dict = {
            '-1': -1,
            0: 0,
            1: 1,
            2: 0x100000000,
        };
        testInParameter('ghashtable_int64', int64Dict);
    });

    describe('with unsigned 64-bit int values', function () {
        const uint64Dict = {
            '-1': 0x100000000,
            0: 0,
            1: 1,
            2: 2,
        };
        testInParameter('ghashtable_uint64', uint64Dict);
    });

    it('symbol keys are ignored', function () {
        const symbolDict = {
            [Symbol('foo')]: 2,
            '-1': 1,
            0: 0,
            1: -1,
            2: -2,
        };
        expect(() => GIMarshallingTests.ghashtable_int_none_in(symbolDict)).not.toThrow();
    });
});

describe('GValue', function () {
    testSimpleMarshalling('gvalue', 42, '42', null, {
        inout: {
            skip: 'https://gitlab.gnome.org/GNOME/gobject-introspection/issues/192',
        },
    });

    it('can handle noncanonical float NaN', function () {
        expect(GIMarshallingTests.gvalue_noncanonical_nan_float()).toBeNaN();
    });

    it('can handle noncanonical double NaN', function () {
        expect(GIMarshallingTests.gvalue_noncanonical_nan_double()).toBeNaN();
    });

    it('marshals as an int64 in parameter', function () {
        expect(() => GIMarshallingTests.gvalue_int64_in(BigIntLimits.int64.max))
            .not.toThrow();
    });

    it('type objects can be converted from primitive-like types', function () {
        expect(() => GIMarshallingTests.gvalue_in_with_type(42, GObject.Int))
            .not.toThrow();
        expect(() => GIMarshallingTests.gvalue_in_with_type(42.5, GObject.Double))
            .not.toThrow();
        expect(() => GIMarshallingTests.gvalue_in_with_type(42.5, Number))
            .not.toThrow();
    });

    it('can be passed into a function and modified', function () {
        expect(() => GIMarshallingTests.gvalue_in_with_modification(42)).not.toThrow();
        // Let's assume this test doesn't expect that the modified number makes
        // it back to the caller; it is not possible to "modify" a JS primitive.
        //
        // See the "as a boxed type" test below for passing an explicit GObject.Value
    });

    it('can be passed into a function as a boxed type and modified', function () {
        const value = new GObject.Value();
        value.init(GObject.TYPE_INT);
        value.set_int(42);

        expect(() => GIMarshallingTests.gvalue_in_with_modification(value)).not.toThrow();
        expect(value.get_int()).toBe(24);
    });

    xit('enum can be passed into a function and packed', function () {
        expect(() => GIMarshallingTests.gvalue_in_enum(GIMarshallingTests.Enum.VALUE3))
            .not.toThrow();
    }).pend("we don't know to pack enums in a GValue as enum and not int");

    it('enum can be passed into a function as a boxed type and packed', function () {
        const value = new GObject.Value();
        // GIMarshallingTests.Enum is a native enum.
        value.init(GObject.TYPE_ENUM);
        value.set_enum(GIMarshallingTests.Enum.VALUE3);
        expect(() => GIMarshallingTests.gvalue_in_enum(value))
            .not.toThrow();
    });

    xit('flags can be passed into a function and packed', function () {
        expect(() => GIMarshallingTests.gvalue_in_flags(GIMarshallingTests.Flags.VALUE3))
            .not.toThrow();
    }).pend("we don't know to pack flags in a GValue as flags and not gint");

    it('flags can be passed into a function as a boxed type and packed', function () {
        const value = new GObject.Value();
        value.init(GIMarshallingTests.Flags);
        value.set_flags(GIMarshallingTests.Flags.VALUE3);
        expect(() => GIMarshallingTests.gvalue_in_flags(value))
            .not.toThrow();
    });

    it('marshals as an int64 out parameter', function () {
        expect(warn64(true, GIMarshallingTests.gvalue_int64_out)).toEqual(
            Limits.int64.max);
    });

    it('marshals as a caller-allocated out parameter', function () {
        expect(GIMarshallingTests.gvalue_out_caller_allocates()).toEqual(42);
    });

    it('array can be passed into a function and packed', function () {
        expect(() => GIMarshallingTests.gvalue_flat_array([42, '42', true]))
            .not.toThrow();
    });

    it('array of boxed type GValues can be passed into a function', function () {
        const value0 = new GObject.Value();
        value0.init(GObject.TYPE_INT);
        value0.set_int(42);
        const value1 = new GObject.Value();
        value1.init(String);
        value1.set_string('42');
        const value2 = new GObject.Value();
        value2.init(Boolean);
        value2.set_boolean(true);

        const values = [value0, value1, value2];
        expect(() => GIMarshallingTests.gvalue_flat_array(values))
            .not.toThrow();
    });

    it('array of uninitialized boxed GValues', function () {
        const values = Array(3).fill().map(() => new GObject.Value());
        expect(() => GIMarshallingTests.gvalue_flat_array(values)).toThrow();
    });

    it('array can be passed as an out argument and unpacked', function () {
        expect(GIMarshallingTests.return_gvalue_flat_array())
            .toEqual([42, '42', true]);
    });

    it('array can be passed as an out argument and unpacked when zero-terminated', function () {
        expect(GIMarshallingTests.return_gvalue_zero_terminated_array())
            .toEqual([42, '42', true]);
    });

    xit('array can roundtrip with GValues intact', function () {
        expect(GIMarshallingTests.gvalue_flat_array_round_trip(42, '42', true))
            .toEqual([42, '42', true]);
    }).pend('https://gitlab.gnome.org/GNOME/gjs/issues/272');

    it('can have its type inferred from primitive values', function () {
        expect(() => GIMarshallingTests.gvalue_in_with_type(42, GObject.TYPE_INT))
            .not.toThrow();
        expect(() => GIMarshallingTests.gvalue_in_with_type(42.5, GObject.TYPE_DOUBLE))
            .not.toThrow();
        expect(() => GIMarshallingTests.gvalue_in_with_type('42', GObject.TYPE_STRING))
            .not.toThrow();
        expect(() => GIMarshallingTests.gvalue_in_with_type(GObject.TYPE_GTYPE, GObject.TYPE_GTYPE))
            .not.toThrow();
    });

    // supplementary tests for gvalue_in_with_type()

    it('can have its type inferred as a GObject type', function () {
        expect(() => GIMarshallingTests.gvalue_in_with_type(new Gio.SimpleAction(), Gio.SimpleAction))
            .not.toThrow();
    });

    it('can have its type inferred as a superclass', function () {
        let action = new Gio.SimpleAction();
        expect(() => GIMarshallingTests.gvalue_in_with_type(action, GObject.Object))
            .not.toThrow();
        expect(() => GIMarshallingTests.gvalue_in_with_type(action, GObject.TYPE_OBJECT))
            .not.toThrow();
    });

    it('can have its type inferred as an interface that it implements', function () {
        expect(() => GIMarshallingTests.gvalue_in_with_type(new Gio.SimpleAction(), Gio.SimpleAction))
            .not.toThrow();
    });

    it('can have its type inferred as a boxed type', function () {
        let keyfile = new GLib.KeyFile();
        expect(() => GIMarshallingTests.gvalue_in_with_type(keyfile, GLib.KeyFile))
            .not.toThrow();
        expect(() => GIMarshallingTests.gvalue_in_with_type(keyfile, GObject.TYPE_BOXED))
            .not.toThrow();
        let struct = new GIMarshallingTests.BoxedStruct();
        expect(() => GIMarshallingTests.gvalue_in_with_type(struct, GIMarshallingTests.BoxedStruct))
            .not.toThrow();
    });

    it('can have its type inferred as GVariant', function () {
        let variant = GLib.Variant.new('u', 42);
        expect(() => GIMarshallingTests.gvalue_in_with_type(variant, GLib.Variant))
            .not.toThrow();
        expect(() => GIMarshallingTests.gvalue_in_with_type(variant, GObject.TYPE_VARIANT))
            .not.toThrow();
    });

    it('can have its type inferred as a union type', function () {
        let union = GIMarshallingTests.union_returnv();
        expect(() => GIMarshallingTests.gvalue_in_with_type(union, GIMarshallingTests.Union))
            .not.toThrow();
    });

    it('can have its type inferred as a GParamSpec', function () {
        let paramSpec = GObject.ParamSpec.string('my-param', '', '',
            GObject.ParamFlags.READABLE, '');
        expect(() => GIMarshallingTests.gvalue_in_with_type(paramSpec, GObject.TYPE_PARAM))
            .not.toThrow();
    });

    it('can deal with a GValue packed in a GValue', function () {
        const innerValue = new GObject.Value();
        innerValue.init(Number);
        innerValue.set_double(42);

        expect(() => GIMarshallingTests.gvalue_in_with_type(innerValue, Number))
            .not.toThrow();

        const value = new GObject.Value();
        value.init(GObject.Value);
        value.set_boxed(innerValue);

        expect(() => GIMarshallingTests.gvalue_in_with_type(value, GObject.Value))
            .not.toThrow();
    });

    it('separates float from double correctly', function () {
        // Passing a Number infers the type 'double'. To pass a float GValue, we
        // need to construct it manually.
        const floatValue = new GObject.Value();
        floatValue.init(GObject.TYPE_FLOAT);
        floatValue.set_float(3.14);
        expect(() => GIMarshallingTests.gvalue_float(floatValue, 3.14)).not.toThrow();
    });

    // See testCairo.js for a test of GIMarshallingTests.gvalue_in_with_type()
    // on Cairo foreign structs, since it will be skipped if compiling without
    // Cairo support.
});

describe('Callback', function () {
    describe('GClosure', function () {
        testInParameter('gclosure', () => 42);

        xit('marshals a GClosure as a return value', function () {
            // Currently a GObject.Closure instance is returned, upon which it's
            // not possible to call invoke() because that method takes a bare
            // pointer as an argument.
            expect(GIMarshallingTests.gclosure_return()()).toEqual(42);
        }).pend('https://gitlab.gnome.org/GNOME/gjs/issues/80');
    });

    it('marshals a return value', function () {
        expect(GIMarshallingTests.callback_return_value_only(() => 42))
            .toEqual(42);
    });

    it('marshals one out parameter', function () {
        expect(GIMarshallingTests.callback_one_out_parameter(() => 43))
            .toEqual(43);
    });

    it('marshals multiple out parameters', function () {
        expect(GIMarshallingTests.callback_multiple_out_parameters(() => [44, 45]))
            .toEqual([44, 45]);
    });

    it('marshals a return value and one out parameter', function () {
        expect(GIMarshallingTests.callback_return_value_and_one_out_parameter(() => [46, 47]))
            .toEqual([46, 47]);
    });

    it('marshals a return value and multiple out parameters', function () {
        expect(GIMarshallingTests.callback_return_value_and_multiple_out_parameters(() => [48, 49, 50]))
            .toEqual([48, 49, 50]);
    });

    xit('marshals an array out parameter', function () {
        expect(GIMarshallingTests.callback_array_out_parameter(() => [50, 51]))
            .toEqual([50, 51]);
    }).pend('Function not added to gobject-introspection test suite yet');

    it('marshals a callback parameter that can be called from C', function () {
        expect(GIMarshallingTests.callback_owned_boxed(box => {
            expect(box.long_).toEqual(1);
            box.long_ = 52;
        })).toEqual(52);
    });
});

describe('Raw pointers', function () {
    it('gets an allocated return value', function () {
        expect(GIMarshallingTests.pointer_in_return(null)).toBeFalsy();
    });

    it('can be roundtripped at least if the pointer is null', function () {
        expect(GIMarshallingTests.pointer_in_return(null)).toBeNull();
    });
});

describe('Registered enum type', function () {
    testSimpleMarshalling('genum', GIMarshallingTests.GEnum.VALUE3,
        GIMarshallingTests.GEnum.VALUE1, 0, {
            returnv: {
                funcName: 'genum_returnv',
            },
        });
});

describe('Bare enum type', function () {
    testSimpleMarshalling('enum', GIMarshallingTests.Enum.VALUE3,
        GIMarshallingTests.Enum.VALUE1, 0, {
            returnv: {
                funcName: 'enum_returnv',
            },
        });
});

describe('Registered flags type', function () {
    testSimpleMarshalling('flags', GIMarshallingTests.Flags.VALUE2,
        GIMarshallingTests.Flags.VALUE1, 0, {
            returnv: {
                funcName: 'flags_returnv',
            },
        });

    it('accepts zero', function () {
        expect(() => GIMarshallingTests.flags_in_zero(0)).not.toThrow();
    });
});

describe('Bare flags type', function () {
    testSimpleMarshalling('no_type_flags', GIMarshallingTests.NoTypeFlags.VALUE2,
        GIMarshallingTests.NoTypeFlags.VALUE1, 0, {
            returnv: {
                funcName: 'no_type_flags_returnv',
            },
        });

    it('accepts zero', function () {
        expect(() => GIMarshallingTests.no_type_flags_in_zero(0)).not.toThrow();
    });
});

describe('Simple struct', function () {
    it('marshals as a return value', function () {
        expect(GIMarshallingTests.simple_struct_returnv()).toEqual(jasmine.objectContaining({
            long_: 6,
            int8: 7,
        }));
    });

    it('marshals as the this-argument of a method', function () {
        const struct = new GIMarshallingTests.SimpleStruct({
            long_: 6,
            int8: 7,
        });
        expect(() => struct.inv()).not.toThrow();  // was this supposed to be static?
        expect(() => struct.method()).not.toThrow();
    });
});

describe('Pointer struct', function () {
    it('marshals as a return value', function () {
        expect(GIMarshallingTests.pointer_struct_returnv()).toEqual(jasmine.objectContaining({
            long_: 42,
        }));
    });

    it('marshals as the this-argument of a method', function () {
        const struct = new GIMarshallingTests.PointerStruct({
            long_: 42,
        });
        expect(() => struct.inv()).not.toThrow();
    });
});

describe('Boxed struct', function () {
    it('marshals as a return value', function () {
        expect(GIMarshallingTests.boxed_struct_returnv()).toEqual(jasmine.objectContaining({
            long_: 42,
            string_: 'hello',
            g_strv: ['0', '1', '2'],
        }));
    });

    it('marshals as the this-argument of a method', function () {
        const struct = new GIMarshallingTests.BoxedStruct({
            long_: 42,
        });
        expect(() => struct.inv()).not.toThrow();
    });

    it('marshals as an out parameter', function () {
        expect(GIMarshallingTests.boxed_struct_out()).toEqual(jasmine.objectContaining({
            long_: 42,
        }));
    });

    testUninitializedOutParameter('boxed_struct', null);

    it('marshals as an inout parameter', function () {
        const struct = new GIMarshallingTests.BoxedStruct({
            long_: 42,
        });
        expect(GIMarshallingTests.boxed_struct_inout(struct)).toEqual(jasmine.objectContaining({
            long_: 0,
        }));
    });
});

describe('Union', function () {
    let union;
    beforeEach(function () {
        union = GIMarshallingTests.union_returnv();
    });

    it('can be constructed empty', function () {
        const constructedUnion = new GIMarshallingTests.Union();
        expect(constructedUnion.long_).toBe(0);
    });

    it('can be constructed with properties', function () {
        const constructedUnion = new GIMarshallingTests.Union({long_: 55});
        expect(constructedUnion.long_).toBe(55);
    });

    it('cannot be constructed with unknown properties', function () {
        expect(() => new GIMarshallingTests.Union({invalidProperty: 55})).toThrowError(
            /No field.*invalidProperty.*Union.*/);
    });

    xit('cannot be constructed with wrong-type properties', function () {
        expect(() => new GIMarshallingTests.Union({long_: 'long_'})).toThrow();
        expect(() => new GIMarshallingTests.Union({long_: union})).toThrow();
    }).pend('We implicitly convert wrong typed values');

    it('marshals as a return value', function () {
        expect(union.long_).toBe(42);
    });

    it('marshals as a settable property', function () {
        union.long_ = 5555;
        expect(union.long_).toBe(5555);
    });

    it('marshals as the this-argument of a method', function () {
        expect(() => union.inv()).not.toThrow();  // was this supposed to be static?
        expect(() => union.method()).not.toThrow();
    });

    it('marshals as the this-argument of a method when constructed', function () {
        expect(() => new GIMarshallingTests.Union({long_: 42}).inv()).not.toThrow();
        expect(() => new GIMarshallingTests.Union({long_: 42}).method()).not.toThrow();
    });

    it('marshals unregistered union', function () {
        const u = new GIMarshallingTests.UnregisteredUnion();
        expect(u.long_).toBe(0);
        expect(u.size).toBe(0);
        expect(u.str).toBe(null);

        u.long_ = 1;
        expect(u.long_).toBe(1);

        u.size = 2;
        expect(u.size).toBe(2);

        expect(() => (u.str = 'three')).toThrow();
        expect(u.size).toBe(2);
    });

    it('marshals unregistered initialized union', function () {
        expect(new GIMarshallingTests.UnregisteredUnion({long_: 123}).long_).toBe(123);
        expect(new GIMarshallingTests.UnregisteredUnion({size: 321}).size).toBe(321);
        expect(() => new GIMarshallingTests.UnregisteredUnion({str: '123'})).toThrow();
    });
});

describe('Structured union', function () {
    it('cannot be constructed with empty default constructor', function () {
        expect(() => new GIMarshallingTests.StructuredUnion()).toThrow();
    });

    Object.entries(GIMarshallingTests.StructuredUnionType).forEach(([unionTypeName, unionType]) => {
        const memberType = unionTypeName.toLowerCase().replace(
            /(^[a-z]|[-_][a-z])/g, group => group.toUpperCase().replace('_', ''));

        function getMember(union) {
            let member = union[unionTypeName.toLowerCase()];

            switch (union.type()) {
            case GIMarshallingTests.StructuredUnionType.SINGLE_UNION:
                member = member.parent;
                break;
            }

            return member;
        }

        it(`can be constructed with default constructor ${memberType}`, function () {
            const union = new GIMarshallingTests.StructuredUnion(unionType);
            expect(union.type()).toBe(unionType);
            expect(union._type).toBe(unionType);
        });

        it(`cannot change a private member ${memberType}`, function () {
            const union = new GIMarshallingTests.StructuredUnion(unionType);
            expect(() => (union._type = GIMarshallingTests.StructuredUnionType.NONE)).toThrow();
        });

        it(`can be constructed and has valid member ${memberType}`, function () {
            const union = new GIMarshallingTests.StructuredUnion(unionType);
            const member = getMember(union);
            let invObj;
            switch (union.type()) {
            case GIMarshallingTests.StructuredUnionType.NONE:
                expect(member).toBeUndefined();
                return;
            case GIMarshallingTests.StructuredUnionType.NESTED_STRUCT:
                invObj = member.parent.simple_struct;
                break;
            case GIMarshallingTests.StructuredUnionType.SINGLE_UNION:
                invObj = member.union_;
                break;
            }

            expect(member.type).toBe(union.type());
            expect(() => (invObj ?? member.parent).inv()).not.toThrow();
        });

        it('cannot be constructed with private field', function () {
            expect(() => new GIMarshallingTests.StructuredUnion({
                type: unionType,
            })).toThrow();
            expect(() => new GIMarshallingTests.StructuredUnion({
                _type: unionType,
            })).toThrow();
        });

        it(`can be constructed with member value ${memberType}`, function () {
            if (unionType === GIMarshallingTests.StructuredUnionType.NONE)
                return;

            const member = new GIMarshallingTests[`StructuredUnion${memberType}`]();
            if (unionType === GIMarshallingTests.StructuredUnionType.SINGLE_UNION) {
                expect(member.parent.type).toBe(GIMarshallingTests.StructuredUnionType.NONE);
                member.parent.type = unionType;
            } else {
                expect(member.type).toBe(GIMarshallingTests.StructuredUnionType.NONE);
                member.type = unionType;
            }

            const union = new GIMarshallingTests.StructuredUnion({
                [unionTypeName.toLowerCase()]: member,
            });
            expect(union.type()).toBe(unionType);
            expect(union._type).toBe(unionType);
        });

        it(`can be constructed from constructed member ${memberType}`, function () {
            if (unionType === GIMarshallingTests.StructuredUnionType.NONE)
                return;

            const baseUnion = new GIMarshallingTests.StructuredUnion(unionType);
            const prop = unionTypeName.toLowerCase();
            const member = baseUnion[prop];

            const union = new GIMarshallingTests.StructuredUnion({[prop]: member});
            expect(union.type()).toBe(baseUnion.type());
            expect(union.type()).toBe(member.type ?? member.parent.type);
            // expect(union._type).toBe(baseUnion._type);
        });
    });

    it('can be constructed from boxed struct property', function () {
        const member = new GIMarshallingTests.StructuredUnionBoxedStruct();
        member.parent = GIMarshallingTests.boxed_struct_returnv();
        const union = new GIMarshallingTests.StructuredUnion({
            'boxed_struct': member,
        });
        expect(union.boxed_struct.parent.long_).toBe(42);
        expect(union.boxed_struct.parent.string_).toBe('hello');
        expect(union.boxed_struct.parent.g_strv).toEqual(['0', '1', '2']);
    });

    it('can be created with a default constructor', function () {
        const u = new GIMarshallingTests.StructuredUnion(GIMarshallingTests.StructuredUnionType.NONE);
        expect(u).toBeInstanceOf(GIMarshallingTests.StructuredUnion);
    });

    it('can be created with NONE', function () {
        const t = GIMarshallingTests.StructuredUnionType.NONE;
        const u = new GIMarshallingTests.StructuredUnion(t);
        expect(u.type()).toBe(t);
    });

    it('can be created with SIMPLE_STRUCT', function () {
        const t = GIMarshallingTests.StructuredUnionType.SIMPLE_STRUCT;
        const u = new GIMarshallingTests.StructuredUnion(t);
        expect(u.type()).toBe(t);
        expect(u.simple_struct.parent.long_).toBe(6);
        expect(u.simple_struct.parent.int8).toBe(7);
    });

    it('can be created with NESTED_STRUCT', function () {
        const t = GIMarshallingTests.StructuredUnionType.NESTED_STRUCT;
        const u = new GIMarshallingTests.StructuredUnion(t);
        expect(u.type()).toBe(t);
        expect(u.nested_struct.parent.simple_struct.long_).toBe(6);
        expect(u.nested_struct.parent.simple_struct.int8).toBe(7);
    });

    it('can be created with BOXED_STRUCT', function () {
        const t = GIMarshallingTests.StructuredUnionType.BOXED_STRUCT;
        const u = new GIMarshallingTests.StructuredUnion(t);
        expect(u.type()).toBe(t);
        expect(u.boxed_struct.parent.long_).toBe(42);
        expect(u.boxed_struct.parent.string_).toBe('hello');
        expect(u.boxed_struct.parent.g_strv).toEqual(['0', '1', '2']);
    });

    it('can be created with BOXED_STRUCT_PTR', function () {
        const t = GIMarshallingTests.StructuredUnionType.BOXED_STRUCT_PTR;
        const u = new GIMarshallingTests.StructuredUnion(t);
        expect(u.type()).toBe(t);
        expect(u.boxed_struct_ptr.parent.long_).toBe(42);
        expect(u.boxed_struct_ptr.parent.string_).toBe('hello');
        expect(u.boxed_struct_ptr.parent.g_strv).toEqual(['0', '1', '2']);
    });

    it('can be created with POINTER_STRUCT', function () {
        const t = GIMarshallingTests.StructuredUnionType.POINTER_STRUCT;
        const u = new GIMarshallingTests.StructuredUnion(t);
        expect(u.type()).toBe(t);
        expect(u.pointer_struct.parent.long_).toBe(42);
    });

    it('can be created with SINGLE_UNION', function () {
        const t = GIMarshallingTests.StructuredUnionType.SINGLE_UNION;
        const u = new GIMarshallingTests.StructuredUnion(t);
        expect(u.type()).toBe(t);
        expect(u.single_union.parent.union_.long_).toBe(42);
    });
});

describe('GObject', function () {
    it('has a static method that can be called', function () {
        expect(() => GIMarshallingTests.Object.static_method()).not.toThrow();
    });

    it('has a method that can be called', function () {
        const o = new GIMarshallingTests.Object({int: 42});
        expect(() => o.method()).not.toThrow();
    });

    it('has an overridden method that can be called', function () {
        const o = new GIMarshallingTests.Object({int: 0});
        expect(() => o.overridden_method()).not.toThrow();
    });

    it('can be created from a static constructor', function () {
        const o = GIMarshallingTests.Object.new(42);
        expect(o.int).toEqual(42);
    });

    it('can have a static constructor that fails', function () {
        expect(() => GIMarshallingTests.Object.new_fail(42)).toThrow();
    });

    describe('method', function () {
        let o;
        beforeEach(function () {
            o = new GIMarshallingTests.Object();
        });

        it('marshals an int array as an in parameter', function () {
            expect(() => o.method_array_in([-1, 0, 1, 2])).not.toThrow();
        });

        it('marshals an int array as an out parameter', function () {
            expect(o.method_array_out()).toEqual([-1, 0, 1, 2]);
        });

        it('marshals an int array as an inout parameter', function () {
            expect(o.method_array_inout([-1, 0, 1, 2])).toEqual([-2, -1, 0, 1, 2]);
        });

        it('marshals an int array as a return value', function () {
            expect(o.method_array_return()).toEqual([-1, 0, 1, 2]);
        });

        it('with default implementation can be called', function () {
            o = new GIMarshallingTests.Object({int: 42});
            o.method_with_default_implementation(43);
            expect(o.int).toEqual(43);
        });
    });

    ['none', 'full'].forEach(transfer => {
        ['return', 'out'].forEach(mode => {
            it(`marshals as a ${mode} parameter with transfer ${transfer}`, function () {
                expect(GIMarshallingTests.Object[`${transfer}_${mode}`]().int).toEqual(0);
            });
        });

        it(`picks a reasonable default when uninitialized as out parameter with transfer ${transfer}`, function () {
            expect(GIMarshallingTests.Object[`${transfer}_out_uninitialized`]()).toEqual([false, null]);
        });

        it(`marshals as an inout parameter with transfer ${transfer}`, function () {
            const o = new GIMarshallingTests.Object({int: 42});
            expect(GIMarshallingTests.Object[`${transfer}_inout`](o).int).toEqual(0);
        });
    });

    it('marshals as a this value with transfer none', function () {
        const o = new GIMarshallingTests.Object({int: 42});
        expect(() => o.none_in()).not.toThrow();
    });
});

let VFuncTester = GObject.registerClass(class VFuncTester extends GIMarshallingTests.Object {
    vfunc_method_int8_in(i) {
        this.int = i;
    }

    vfunc_method_int8_out() {
        return 40;
    }

    vfunc_method_int8_arg_and_out_caller(i) {
        return i + 3;
    }

    vfunc_method_int8_arg_and_out_callee(i) {
        return i + 4;
    }

    vfunc_method_str_arg_out_ret(s) {
        return [`Called with ${s}`, 41];
    }

    vfunc_method_with_default_implementation(i) {
        this.int = i + 2;
    }

    // vfunc_vfunc_with_callback(callback) {
    //     this.int = callback(41);
    // }

    vfunc_vfunc_return_value_only() {
        return 42;
    }

    vfunc_vfunc_one_out_parameter() {
        return 43;
    }

    vfunc_vfunc_multiple_out_parameters() {
        return [44, 45];
    }

    vfunc_vfunc_return_value_and_one_out_parameter() {
        return [46, 47];
    }

    vfunc_vfunc_return_value_and_multiple_out_parameters() {
        return [48, 49, 50];
    }

    vfunc_vfunc_array_out_parameter() {
        return [50, 51];
    }

    vfunc_vfunc_caller_allocated_out_parameter() {
        return 52;
    }

    vfunc_vfunc_meth_with_err(x) {
        switch (x) {
        case -1:
            return true;
        case 0:
            undefined.throwTypeError();
            break;
        case 1:
            void referenceError;  // eslint-disable-line no-undef
            break;
        case 2:
            throw new Gio.IOErrorEnum({
                code: Gio.IOErrorEnum.FAILED,
                message: 'I FAILED, but the test passed!',
            });
        case 3:
            throw new GLib.SpawnError({
                code: GLib.SpawnError.TOO_BIG,
                message: 'This test is Too Big to Fail',
            });
        case 4:
            throw null;  // eslint-disable-line no-throw-literal
        case 5:
            throw undefined;  // eslint-disable-line no-throw-literal
        case 6:
            throw 42;  // eslint-disable-line no-throw-literal
        case 7:
            throw true;  // eslint-disable-line no-throw-literal
        case 8:
            throw 'a string';  // eslint-disable-line no-throw-literal
        case 9:
            throw 42n;  // eslint-disable-line no-throw-literal
        case 10:
            throw Symbol('a symbol');
        case 11:
            throw {plain: 'object'};  // eslint-disable-line no-throw-literal
        case 12:
            // eslint-disable-next-line no-throw-literal
            throw {name: 'TypeError', message: 'an error message'};
        case 13:
            // eslint-disable-next-line no-throw-literal
            throw {name: 1, message: 'an error message'};
        case 14:
            // eslint-disable-next-line no-throw-literal
            throw {name: 'TypeError', message: false};
        }
    }

    vfunc_vfunc_return_enum() {
        return GIMarshallingTests.Enum.VALUE2;
    }

    vfunc_vfunc_out_enum() {
        return GIMarshallingTests.Enum.VALUE3;
    }

    vfunc_vfunc_return_flags() {
        return GIMarshallingTests.Flags.VALUE2;
    }

    vfunc_vfunc_out_flags() {
        return GIMarshallingTests.Flags.VALUE3;
    }

    vfunc_vfunc_return_object_transfer_none() {
        if (!this._returnObject)
            this._returnObject = new GIMarshallingTests.Object({int: 53});
        return this._returnObject;
    }

    vfunc_vfunc_return_object_transfer_full() {
        return new GIMarshallingTests.Object({int: 54});
    }

    vfunc_vfunc_out_object_transfer_none() {
        if (!this._outObject)
            this._outObject = new GIMarshallingTests.Object({int: 55});
        return this._outObject;
    }

    vfunc_vfunc_out_object_transfer_full() {
        return new GIMarshallingTests.Object({int: 56});
    }

    vfunc_vfunc_in_object_transfer_none(object) {
        void object;
    }

    vfunc_vfunc_in_object_transfer_full(object) {
        this._inObject = object;
    }
});

try {
    VFuncTester = GObject.registerClass(class VFuncTesterInOut extends VFuncTester {
        vfunc_vfunc_one_inout_parameter(input) {
            return input * 5;
        }

        vfunc_vfunc_multiple_inout_parameters(inputA, inputB) {
            return [inputA * 5, inputB * -1];
        }

        vfunc_vfunc_return_value_and_one_inout_parameter(input) {
            return [49, input * 5];
        }

        vfunc_vfunc_return_value_and_multiple_inout_parameters(inputA, inputB) {
            return [49, inputA * 5, inputB * -1];
        }
    });
} catch {}

describe('Virtual function', function () {
    let tester;
    beforeEach(function () {
        tester = new VFuncTester();
    });

    it('marshals an in argument', function () {
        tester.method_int8_in(39);
        expect(tester.int).toEqual(39);
    });

    it('marshals an in argument through a method that indirectly calls the vfunc', function () {
        tester.int8_in(39);
        expect(tester.int).toEqual(39);
    });

    it('marshals an out argument', function () {
        expect(tester.method_int8_out()).toEqual(40);
    });

    it('marshals an out argument through a method that indirectly calls the vfunc', function () {
        expect(tester.int8_out()).toEqual(40);
    });

    it('marshals a POD out argument', function () {
        expect(tester.method_int8_arg_and_out_caller(39)).toEqual(42);
    });

    it('marshals a callee-allocated pointer out argument', function () {
        expect(tester.method_int8_arg_and_out_callee(38)).toEqual(42);
    });

    it('marshals a string out argument and return value', function () {
        expect(tester.method_str_arg_out_ret('a string')).toEqual(['Called with a string', 41]);
        expect(tester.method_str_arg_out_ret('a 2nd string')).toEqual(['Called with a 2nd string', 41]);
    });

    it('can override a default implementation in JS', function () {
        tester.method_with_default_implementation(40);
        expect(tester.int).toEqual(42);
    });

    xit('marshals a callback', function () {
        tester.call_vfunc_with_callback();
        expect(tester.int).toEqual(41);
    }).pend('callback parameters to vfuncs not supported');

    it('marshals a return value', function () {
        expect(tester.vfunc_return_value_only()).toEqual(42);
    });

    it('marshals one out parameter', function () {
        expect(tester.vfunc_one_out_parameter()).toEqual(43);
    });

    it('marshals multiple out parameters', function () {
        expect(tester.vfunc_multiple_out_parameters()).toEqual([44, 45]);
    });

    it('marshals a return value and one out parameter', function () {
        expect(tester.vfunc_return_value_and_one_out_parameter())
            .toEqual([46, 47]);
    });

    it('marshals a return value and multiple out parameters', function () {
        expect(tester.vfunc_return_value_and_multiple_out_parameters())
            .toEqual([48, 49, 50]);
    });

    it('marshals one inout parameter', function () {
        expect(tester.vfunc_one_inout_parameter(10)).toEqual(50);
    });

    it('marshals multiple inout parameters', function () {
        expect(tester.vfunc_multiple_inout_parameters(10, 5)).toEqual([50, -5]);
    });

    it('marshals a return value and one inout parameter', function () {
        expect(tester.vfunc_return_value_and_one_inout_parameter(10))
            .toEqual([49, 50]);
    });

    it('marshals a return value and multiple inout parameters', function () {
        expect(tester.vfunc_return_value_and_multiple_inout_parameters(10, -51))
            .toEqual([49, 50, 51]);
    });

    it('marshals an array out parameter', function () {
        expect(tester.vfunc_array_out_parameter()).toEqual([50, 51]);
    });

    it('marshals a caller-allocated GValue out parameter', function () {
        expect(tester.vfunc_caller_allocated_out_parameter()).toEqual(52);
    });

    it('marshals an error out parameter when no error', function () {
        expect(tester.vfunc_meth_with_error(-1)).toBeTruthy();
    });

    it('marshals an error out parameter with a JavaScript exception', function () {
        expect(() => tester.vfunc_meth_with_error(0)).toThrowError(TypeError);
        expect(() => tester.vfunc_meth_with_error(1)).toThrowError(ReferenceError);
    });

    it('marshals an error out parameter with a GError exception', function () {
        try {
            tester.vfunc_meth_with_error(2);
            fail('Exception should be thrown');
        } catch (e) {
            expect(e.matches(Gio.IOErrorEnum, Gio.IOErrorEnum.FAILED)).toBeTruthy();
            expect(e.message).toEqual('I FAILED, but the test passed!');
        }

        try {
            tester.vfunc_meth_with_error(3);
            fail('Exception should be thrown');
        } catch (e) {
            expect(e.matches(GLib.SpawnError, GLib.SpawnError.TOO_BIG)).toBeTruthy();
            expect(e.message).toEqual('This test is Too Big to Fail');
        }
    });

    it('marshals an error out parameter with a primitive value', function () {
        expect(() => tester.vfunc_meth_with_error(4)).toThrowError(/null/);
        expect(() => tester.vfunc_meth_with_error(5)).toThrowError(/undefined/);
        expect(() => tester.vfunc_meth_with_error(6)).toThrowError(/42/);
        expect(() => tester.vfunc_meth_with_error(7)).toThrowError(/true/);
        expect(() => tester.vfunc_meth_with_error(8)).toThrowError(/"a string"/);
        expect(() => tester.vfunc_meth_with_error(9)).toThrowError(/42n/);
        expect(() => tester.vfunc_meth_with_error(10)).toThrowError(/Symbol\("a symbol"\)/);
    });

    it('marshals an error out parameter with a plain object', function () {
        expect(() => tester.vfunc_meth_with_error(11)).toThrowError(/Object/);
        expect(() => tester.vfunc_meth_with_error(12)).toThrowError(TypeError, /an error message/);
        expect(() => tester.vfunc_meth_with_error(13)).toThrowError(/Object/);
        expect(() => tester.vfunc_meth_with_error(14)).toThrowError(Error, /Object/);
    });

    it('marshals an enum return value', function () {
        expect(tester.vfunc_return_enum()).toEqual(GIMarshallingTests.Enum.VALUE2);
    });

    it('marshals an enum out parameter', function () {
        expect(tester.vfunc_out_enum()).toEqual(GIMarshallingTests.Enum.VALUE3);
    });

    it('marshals a flags return value', function () {
        expect(tester.vfunc_return_flags()).toEqual(GIMarshallingTests.Flags.VALUE2);
    });

    it('marshals a flags out parameter', function () {
        expect(tester.vfunc_out_flags()).toEqual(GIMarshallingTests.Flags.VALUE3);
    });

    // These tests check what the refcount is of the returned objects; see
    // comments in gimarshallingtests.c.
    // Objects that are exposed in JS always have at least one reference (the
    // toggle reference.) JS never owns more than one reference. There may be
    // other references owned on the C side.
    // In any case the refs should not be floating. We never have floating refs
    // in JS.
    function testVfuncRefcount(mode, transfer, expectedRefcount, options = {}, ...args) {
        it(`marshals an object ${mode} parameter with transfer ${transfer}`, function () {
            if (options.skip)
                pending(options.skip);
            const [refcount, floating] =
                tester[`get_ref_info_for_vfunc_${mode}_object_transfer_${transfer}`](...args);
            expect(floating).toBeFalsy();
            expect(refcount).toEqual(expectedRefcount);
        });
    }
    // Running in extra-gc mode can drop the JS reference, since it is not
    // actually stored anywhere reachable from user code. However, we cannot
    // force the extra GC under normal conditions because it occurs in the
    // middle of C++ code.
    const skipExtraGC = {};
    const zeal = GLib.getenv('JS_GC_ZEAL');
    if (zeal && zeal.startsWith('2,'))
        skipExtraGC.skip = 'Skip during extra-gc.';
    // 1 reference = the object is owned only by JS.
    // 2 references = the object is owned by JS and the vfunc caller.
    testVfuncRefcount('return', 'none', 1);
    testVfuncRefcount('return', 'full', 2, skipExtraGC);
    testVfuncRefcount('out', 'none', 1);
    testVfuncRefcount('out', 'full', 2, skipExtraGC);
    testVfuncRefcount('in', 'none', 2, skipExtraGC, GIMarshallingTests.Object);
    testVfuncRefcount('in', 'full', 1, {
        skip: 'https://gitlab.gnome.org/GNOME/gjs/issues/275',
    }, GIMarshallingTests.Object);
});

const WrongVFuncTester = GObject.registerClass(class WrongVFuncTester extends GIMarshallingTests.Object {
    vfunc_vfunc_return_value_only() {
    }

    vfunc_vfunc_one_out_parameter() {
    }

    vfunc_vfunc_multiple_out_parameters() {
    }

    vfunc_vfunc_return_value_and_one_out_parameter() {
    }

    vfunc_vfunc_return_value_and_multiple_out_parameters() {
    }

    vfunc_vfunc_array_out_parameter() {
    }

    vfunc_vfunc_caller_allocated_out_parameter() {
    }

    vfunc_vfunc_return_enum() {
    }

    vfunc_vfunc_out_enum() {
    }

    vfunc_vfunc_return_flags() {
    }

    vfunc_vfunc_out_flags() {
    }

    vfunc_vfunc_return_object_transfer_none() {
    }

    vfunc_vfunc_return_object_transfer_full() {
    }

    vfunc_vfunc_out_object_transfer_none() {
    }

    vfunc_vfunc_out_object_transfer_full() {
    }

    vfunc_vfunc_in_object_transfer_none() {
    }
});

describe('Wrong virtual functions', function () {
    let tester;
    beforeEach(function () {
        tester = new WrongVFuncTester();
    });

    it('marshals a return value', function () {
        expect(tester.vfunc_return_value_only()).toBeUndefined();
    }).pend('https://gitlab.gnome.org/GNOME/gjs/issues/311');

    it('marshals one out parameter', function () {
        expect(tester.vfunc_one_out_parameter()).toBeUndefined();
    }).pend('https://gitlab.gnome.org/GNOME/gjs/issues/311');

    it('marshals multiple out parameters', function () {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_CRITICAL,
            'JS ERROR: Error: *vfunc_vfunc_multiple_out_parameters*Array*');

        expect(tester.vfunc_multiple_out_parameters()).toEqual([0, 0]);

        GLib.test_assert_expected_messages_internal('Gjs', 'testGIMarshalling.js', 0,
            'testVFuncReturnWrongValue');
    });

    it('marshals a return value and one out parameter', function () {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_CRITICAL,
            'JS ERROR: Error: *vfunc_return_value_and_one_out_parameter*Array*');

        expect(tester.vfunc_return_value_and_one_out_parameter()).toEqual([0, 0]);

        GLib.test_assert_expected_messages_internal('Gjs', 'testGIMarshalling.js', 0,
            'testVFuncReturnWrongValue');
    });

    it('marshals a return value and multiple out parameters', function () {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_CRITICAL,
            'JS ERROR: Error: *vfunc_return_value_and_multiple_out_parameters*Array*');

        expect(tester.vfunc_return_value_and_multiple_out_parameters()).toEqual([0, 0, 0]);

        GLib.test_assert_expected_messages_internal('Gjs', 'testGIMarshalling.js', 0,
            'testVFuncReturnWrongValue');
    });

    it('marshals an array out parameter', function () {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_CRITICAL,
            'JS ERROR: Error: Expected type gfloat for Argument*undefined*');

        expect(tester.vfunc_array_out_parameter()).toEqual(null);

        GLib.test_assert_expected_messages_internal('Gjs', 'testGIMarshalling.js', 0,
            'testVFuncReturnWrongValue');
    });

    it('marshals an enum return value', function () {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_CRITICAL,
            'JS ERROR: Error: Expected type *Enum* for Return*undefined*');

        expect(tester.vfunc_return_enum()).toEqual(0);

        GLib.test_assert_expected_messages_internal('Gjs', 'testGIMarshalling.js', 0,
            'testVFuncReturnWrongValue');
    });

    it('marshals an enum out parameter', function () {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_CRITICAL,
            'JS ERROR: Error: Expected type *Enum* for Argument*undefined*');

        expect(tester.vfunc_out_enum()).toEqual(0);

        GLib.test_assert_expected_messages_internal('Gjs', 'testGIMarshalling.js', 0,
            'testVFuncReturnWrongValue');
    });

    it('marshals a flags return value', function () {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_CRITICAL,
            'JS ERROR: Error: Expected type *Flags* for Return*undefined*');

        expect(tester.vfunc_return_flags()).toEqual(0);

        GLib.test_assert_expected_messages_internal('Gjs', 'testGIMarshalling.js', 0,
            'testVFuncReturnWrongValue');
    });

    it('marshals a flags out parameter', function () {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_CRITICAL,
            'JS ERROR: Error: Expected type *Flags* for Argument*undefined*');

        expect(tester.vfunc_out_flags()).toEqual(0);

        GLib.test_assert_expected_messages_internal('Gjs', 'testGIMarshalling.js', 0,
            'testVFuncReturnWrongValue');
    });
});

let StaticVFuncTester;
try {
    StaticVFuncTester = GObject.registerClass(
    class StaticVFuncTesterClass extends VFuncTester {
        static vfunc_vfunc_static_name() {
            return 'Overridden name';
        }

        static vfunc_vfunc_static_create_new(int) {
            return new StaticVFuncTester({int});
        }

        static vfunc_vfunc_static_create_new_out(int) {
            return new StaticVFuncTester({int});
        }
    });
} catch (e) {
    if (!`${e}`.includes('Could not find definition of virtual function'))
        throw e;
}

describe('Static virtual functions', function () {
    beforeEach(function () {
        if (!StaticVFuncTester) {
            if (GIMarshallingTests.Object.vfunc_static_name)
                pending('https://gitlab.gnome.org/GNOME/gobject-introspection/-/issues/543');
        }
    });

    it('has static_name', function () {
        expect(GIMarshallingTests.Object.vfunc_static_name()).toBe(
            'GIMarshallingTestsObject');
        expect(StaticVFuncTester.vfunc_static_name()).toBe(
            'GIMarshallingTestsObject');
    });

    it('has static_typed_name', function () {
        expect(GIMarshallingTests.Object.vfunc_static_typed_name(
            GIMarshallingTests.Object.$gtype)).toBe('GIMarshallingTestsObject');
        expect(StaticVFuncTester.vfunc_static_typed_name(StaticVFuncTester.$gtype))
            .toBe('Overridden name');
    });

    ['', '_out'].forEach(suffix => it(`has static_create_new${suffix}`, function () {
        const baseObj = GIMarshallingTests.Object[`vfunc_static_create_new${suffix}`](
            GIMarshallingTests.Object.$gtype, 55);
        expect(baseObj).toBeInstanceOf(GIMarshallingTests.Object);
        expect(baseObj).not.toBeInstanceOf(StaticVFuncTester);
        expect(baseObj.int_).toBe(55);

        const middleObj = GIMarshallingTests.Object[`vfunc_static_create_new${suffix}`](
            VFuncTester.$gtype, 35);
        expect(middleObj).toBeInstanceOf(GIMarshallingTests.Object);
        expect(middleObj).not.toBeInstanceOf(VFuncTester);
        expect(middleObj.int_).toBe(35);

        const obj = GIMarshallingTests.Object[`vfunc_static_create_new${suffix}`](
            StaticVFuncTester.$gtype, 85);
        expect(obj).toBeInstanceOf(GIMarshallingTests.Object);
        expect(obj).toBeInstanceOf(VFuncTester);
        expect(obj).toBeInstanceOf(StaticVFuncTester);
        expect(obj.int_).toBe(85);
    }));
});

describe('Inherited GObject', function () {
    ['SubObject', 'SubSubObject'].forEach(klass => {
        describe(klass, function () {
            it('has a parent method that can be called', function () {
                const o = new GIMarshallingTests[klass]({int: 42});
                expect(() => o.method()).not.toThrow();
            });

            it('has a method that can be called', function () {
                const o = new GIMarshallingTests[klass]({int: 0});
                expect(() => o.sub_method()).not.toThrow();
            });

            it('has an overridden method that can be called', function () {
                const o = new GIMarshallingTests[klass]({int: 0});
                expect(() => o.overwritten_method()).not.toThrow();
            });

            it('has a method with default implementation that can be called', function () {
                const o = new GIMarshallingTests[klass]({int: 42});
                o.method_with_default_implementation(43);
                expect(o.int).toEqual(43);
            });

            it('has a vfunc default implementation that can be called', function () {
                const o = new GIMarshallingTests[klass]({int: 0});
                o.vfunc_method_deep_hierarchy(44);
                expect(o.int).toBe(44);
            });

            it('has a vfunc that can be overridden', function () {
                class Derived extends GIMarshallingTests[klass] {
                    static [GObject.GTypeName] = `Derived${klass}`;
                    static {
                        GObject.registerClass(Derived);
                    }

                    vfunc_method_deep_hierarchy(param) {
                        expect(param).toBe(45);
                        this.int = 46;
                    }
                }
                const o = new Derived({int: 0});
                o.vfunc_method_deep_hierarchy(45);
                expect(o.int).toBe(46);
            });
        });
    });
});

describe('Interface', function () {
    it('can be returned', function () {
        let ifaceImpl = new GIMarshallingTests.InterfaceImpl();
        let itself = ifaceImpl.get_as_interface();
        expect(ifaceImpl).toEqual(itself);
    });

    it('can call an interface vfunc in C', function () {
        let ifaceImpl = new GIMarshallingTests.InterfaceImpl();
        expect(() => ifaceImpl.test_int8_in(42)).not.toThrow();
        expect(() => GIMarshallingTests.test_interface_test_int8_in(ifaceImpl, 42))
            .not.toThrow();
    });

    it('can implement a C interface', function () {
        const I2Impl = GObject.registerClass({
            Implements: [GIMarshallingTests.Interface2],
        }, class I2Impl extends GObject.Object {});
        expect(() => new I2Impl()).not.toThrow();
    });

    it('can implement a C interface with a vfunc', function () {
        const I3Impl = GObject.registerClass({
            Implements: [GIMarshallingTests.Interface3],
        }, class I3Impl extends GObject.Object {
            vfunc_test_variant_array_in(variantArray) {
                this.stuff = variantArray.map(v => {
                    const bit64 = this.bigInt &&
                        (v.is_of_type(new GLib.VariantType('t')) ||
                        v.is_of_type(new GLib.VariantType('x')));
                    return warn64(bit64, () => v.deepUnpack());
                });
            }
        });
        const i3 = new I3Impl();
        i3.test_variant_array_in([
            new GLib.Variant('b', true),
            new GLib.Variant('s', 'hello'),
            new GLib.Variant('i', 42),
            new GLib.Variant('t', 43),
            new GLib.Variant('x', 44),
        ]);
        expect(i3.stuff).toEqual([true, 'hello', 42, 43, 44]);

        i3.bigInt = true;
        i3.test_variant_array_in([
            new GLib.Variant('x', BigIntLimits.int64.min),
            new GLib.Variant('x', BigIntLimits.int64.max),
            new GLib.Variant('t', BigIntLimits.int64.umax),
        ]);
        expect(i3.stuff).toEqual([
            Limits.int64.min,
            Limits.int64.max,
            Limits.int64.umax,
        ]);
    });
});

describe('Configurations of return values', function () {
    it('can handle two out parameters', function () {
        expect(GIMarshallingTests.int_out_out()).toEqual([6, 7]);
    });

    it('can handle three in and three out parameters', function () {
        expect(GIMarshallingTests.int_three_in_three_out(1, 2, 3)).toEqual([1, 2, 3]);
    });

    it('can handle a return value and an out parameter', function () {
        expect(GIMarshallingTests.int_return_out()).toEqual([6, 7]);
    });

    it('can handle four in parameters, two of which are nullable', function () {
        expect(() => GIMarshallingTests.int_two_in_utf8_two_in_with_allow_none(1, 2, '3', '4'))
            .not.toThrow();
        expect(() => GIMarshallingTests.int_two_in_utf8_two_in_with_allow_none(1, 2, '3', null))
            .not.toThrow();
        expect(() => GIMarshallingTests.int_two_in_utf8_two_in_with_allow_none(1, 2, null, '4'))
            .not.toThrow();
        expect(() => GIMarshallingTests.int_two_in_utf8_two_in_with_allow_none(1, 2, null, null))
            .not.toThrow();
    });

    it('can handle three in parameters, one of which is nullable and one not', function () {
        expect(() => GIMarshallingTests.int_one_in_utf8_two_in_one_allows_none(1, '2', '3'))
            .not.toThrow();
        expect(() => GIMarshallingTests.int_one_in_utf8_two_in_one_allows_none(1, null, '3'))
            .not.toThrow();
        expect(() => GIMarshallingTests.int_one_in_utf8_two_in_one_allows_none(1, '2', null))
            .toThrow();
    });

    it('can handle an array in parameter and two nullable in parameters', function () {
        expect(() => GIMarshallingTests.array_in_utf8_two_in([-1, 0, 1, 2], '1', '2'))
            .not.toThrow();
        expect(() => GIMarshallingTests.array_in_utf8_two_in([-1, 0, 1, 2], '1', null))
            .not.toThrow();
        expect(() => GIMarshallingTests.array_in_utf8_two_in([-1, 0, 1, 2], null, '2'))
            .not.toThrow();
        expect(() => GIMarshallingTests.array_in_utf8_two_in([-1, 0, 1, 2], null, null))
            .not.toThrow();
    });

    it('can handle an array in parameter and two nullable in parameters, mixed with the array length', function () {
        expect(() =>
            GIMarshallingTests.array_in_utf8_two_in_out_of_order('1', [-1, 0, 1, 2], '2'))
            .not.toThrow();
        expect(() =>
            GIMarshallingTests.array_in_utf8_two_in_out_of_order('1', [-1, 0, 1, 2], null))
            .not.toThrow();
        expect(() =>
            GIMarshallingTests.array_in_utf8_two_in_out_of_order(null, [-1, 0, 1, 2], '2'))
            .not.toThrow();
        expect(() =>
            GIMarshallingTests.array_in_utf8_two_in_out_of_order(null, [-1, 0, 1, 2], null))
            .not.toThrow();
    });
});

describe('GError', function () {
    it('marshals a GError** signature as an exception', function () {
        expect(() => GIMarshallingTests.gerror()).toThrow();
    });

    it('marshals a GError** at the end of the signature as an exception', function () {
        expect(() => GIMarshallingTests.gerror_array_in([-1, 0, 1, 2])).toThrowMatching(e =>
            e.matches(GLib.quark_from_static_string(GIMarshallingTests.CONSTANT_GERROR_DOMAIN),
                GIMarshallingTests.CONSTANT_GERROR_CODE) &&
            e.message === GIMarshallingTests.CONSTANT_GERROR_MESSAGE);
    });

    it('marshals a GError** elsewhere in the signature as an out parameter', function () {
        expect(GIMarshallingTests.gerror_out()).toEqual([
            jasmine.any(GLib.Error),
            'we got an error, life is shit',
        ]);
    });

    testUninitializedOutParameter('gerror', null);

    it('marshals a GError** elsewhere in the signature as an out parameter with transfer none', function () {
        expect(GIMarshallingTests.gerror_out_transfer_none()).toEqual([
            jasmine.any(GLib.Error),
            'we got an error, life is shit',
        ]);
    });

    it('picks a reasonable default value when out parameter is uninitialized with transfer none', function () {
        expect(GIMarshallingTests.gerror_out_transfer_none_uninitialized()).toEqual([false, null, null]);
    });

    it('marshals GError as a return value', function () {
        expect(GIMarshallingTests.gerror_return()).toEqual(jasmine.any(GLib.Error));
    });
});

describe('Filename', function () {
    testReturnValue('filename_list', []);
});

describe('GObject.ParamSpec', function () {
    const pspec = GObject.ParamSpec.boolean('mybool', 'My Bool', 'My boolean property',
        GObject.ParamFlags.READWRITE, true);
    testInParameter('param_spec', pspec, {
        funcName: 'param_spec_in_bool',
    });

    const expectedProps = {
        name: 'test-param',
        nick: 'test',
        blurb: 'This is a test',
        default_value: '42',
        flags: GObject.ParamFlags.READABLE,
        value_type: GObject.TYPE_STRING,
    };
    testReturnValue('param_spec', jasmine.objectContaining(expectedProps));
    testOutParameter('param_spec', jasmine.objectContaining(expectedProps));
    testUninitializedOutParameter('param_spec', null);
});

describe('GObject properties', function () {
    let obj;
    beforeEach(function () {
        obj = new GIMarshallingTests.PropertiesObject();
    });

    function testPropertyGetSet(type, value1, value2) {
        const snakeCase = `some_${type}`;
        const paramCase = snakeCase.replaceAll('_', '-');
        const camelCase = snakeCase.replace(/(_\w)/g,
            match => match.toUpperCase().replace('_', ''));

        [snakeCase, paramCase, camelCase].forEach(propertyName => {
            it(`gets and sets a ${type} property as ${propertyName}`, function () {
                const handler = jasmine.createSpy(`handle-${paramCase}`);
                const id = obj.connect(`notify::${paramCase}`, handler);

                obj[propertyName] = value1;
                expect(obj[propertyName]).toEqual(value1);
                expect(handler).toHaveBeenCalledTimes(1);

                obj[propertyName] = value2;
                expect(obj[propertyName]).toEqual(value2);
                expect(handler).toHaveBeenCalledTimes(2);

                obj.disconnect(id);
            });
        });
    }

    function testPropertyGetSetBigInt(type, value1, value2) {
        const snakeCase = `some_${type}`;
        const paramCase = snakeCase.replaceAll('_', '-');
        const isBigInt = v =>
            v > BigInt(Number.MAX_SAFE_INTEGER) || v < BigInt(Number.MIN_SAFE_INTEGER);
        it(`gets and sets a ${type} property with a bigint`, function () {
            const handler = jasmine.createSpy(`handle-${paramCase}`);
            const id = obj.connect(`notify::${paramCase}`, handler);

            obj[snakeCase] = value1;
            expect(handler).toHaveBeenCalledTimes(1);
            expect(warn64(isBigInt(value1), () => obj[snakeCase])).toEqual(
                Number(value1));

            obj[snakeCase] = value2;
            expect(handler).toHaveBeenCalledTimes(2);
            expect(warn64(isBigInt(value2), () => obj[snakeCase])).toEqual(
                Number(value2));

            obj.disconnect(id);
        });
    }

    testPropertyGetSet('boolean', true, false);
    testPropertyGetSet('char', 42, 64);
    testPropertyGetSet('uchar', 42, 64);
    testPropertyGetSet('int', 42, 64);
    testPropertyGetSet('uint', 42, 64);
    testPropertyGetSet('long', 42, 64);
    testPropertyGetSet('ulong', 42, 64);
    testPropertyGetSet('int64', 42, 64);
    testPropertyGetSet('int64', Number.MIN_SAFE_INTEGER, Number.MAX_SAFE_INTEGER);
    testPropertyGetSetBigInt('int64', BigIntLimits.int64.min, BigIntLimits.int64.max);
    testPropertyGetSet('uint64', 42, 64);
    testPropertyGetSetBigInt('uint64', BigIntLimits.int64.max, BigIntLimits.int64.umax);
    testPropertyGetSetBigInt('uint64', 0n, BigInt(Number.MAX_SAFE_INTEGER));
    testPropertyGetSet('string', 'Gjs', 'is cool!');
    testPropertyGetSet('string', 'and supports', null);

    it('get and sets out-of-range values throws', function () {
        expect(() => {
            obj.some_int64 = Limits.int64.max;
        }).toThrowError(/out of range/);
        expect(() => {
            obj.some_int64 = BigIntLimits.int64.max + 1n;
        }).toThrowError(/out of range/);
        expect(() => {
            obj.some_int64 = BigIntLimits.int64.min - 1n;
        }).toThrowError(/out of range/);
        expect(() => {
            obj.some_int64 = BigIntLimits.int64.umax;
        }).toThrowError(/out of range/);
        expect(() => {
            obj.some_int64 = -BigIntLimits.int64.umax;
        }).toThrowError(/out of range/);
        expect(() => {
            obj.some_uint64 = Limits.int64.min;
        }).toThrowError(/out of range/);
        expect(() => {
            obj.some_uint64 = BigIntLimits.int64.umax + 100n;
        }).toThrowError(/out of range/);
    });

    it('gets and sets a float property', function () {
        const handler = jasmine.createSpy('handle-some-float');
        const id = obj.connect('notify::some-float', handler);

        obj.some_float = Math.E;
        expect(handler).toHaveBeenCalledTimes(1);
        expect(obj.some_float).toBeCloseTo(Math.E);

        obj.some_float = Math.PI;
        expect(handler).toHaveBeenCalledTimes(2);
        expect(obj.some_float).toBeCloseTo(Math.PI);

        obj.disconnect(id);
    });

    it('gets and sets a double property', function () {
        const handler = jasmine.createSpy('handle-some-double');
        const id = obj.connect('notify::some-double', handler);

        obj.some_double = Math.E;
        expect(handler).toHaveBeenCalledTimes(1);
        expect(obj.some_double).toBeCloseTo(Math.E);

        obj.some_double = Math.PI;
        expect(handler).toHaveBeenCalledTimes(2);
        expect(obj.some_double).toBeCloseTo(Math.PI);

        obj.disconnect(id);
    });

    testPropertyGetSet('strv', ['0', '1', '2'], []);
    testPropertyGetSet('boxed_struct', new GIMarshallingTests.BoxedStruct(),
        new GIMarshallingTests.BoxedStruct({long_: 42}));
    testPropertyGetSet('boxed_struct', new GIMarshallingTests.BoxedStruct(),
        null);
    testPropertyGetSet('boxed_glist', null, null);
    testPropertyGetSet('gvalue', 42, 'foo');
    testPropertyGetSetBigInt('gvalue', BigIntLimits.int64.umax, BigIntLimits.int64.min);
    testPropertyGetSet('variant', new GLib.Variant('b', true),
        new GLib.Variant('s', 'hello'));
    testPropertyGetSet('variant', new GLib.Variant('x', BigIntLimits.int64.min),
        new GLib.Variant('x', BigIntLimits.int64.max));
    testPropertyGetSet('variant', new GLib.Variant('t', BigIntLimits.int64.max),
        new GLib.Variant('t', BigIntLimits.int64.umax));
    testPropertyGetSet('object', new GObject.Object(),
        new GIMarshallingTests.Object({int: 42}));
    testPropertyGetSet('object', new GIMarshallingTests.PropertiesObject({
        'some-int': 23, 'some-string': '👾',
    }), null);
    testPropertyGetSet('flags', GIMarshallingTests.Flags.VALUE2,
        GIMarshallingTests.Flags.VALUE1 | GIMarshallingTests.Flags.VALUE2);
    testPropertyGetSet('enum', GIMarshallingTests.GEnum.VALUE2,
        GIMarshallingTests.GEnum.VALUE3);
    testPropertyGetSet('byte_array', Uint8Array.of(1, 2, 3),
        new TextEncoder().encode('👾'));
    testPropertyGetSet('byte_array', Uint8Array.of(3, 2, 1), null);

    it('gets a read-only property', function () {
        expect(obj.some_readonly).toEqual(42);
    });

    it('throws when setting a read-only property', function () {
        expect(() => (obj.some_readonly = 35)).toThrow();
    });

    it('allows to set/get deprecated properties', function () {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_WARNING,
            '*GObject property*.some-deprecated-int is deprecated*');
        obj.some_deprecated_int = 35;
        GLib.test_assert_expected_messages_internal('Gjs', 'testGIMarshalling.js', 0,
            'testAllowToSetGetDeprecatedProperties');

        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_WARNING,
            '*GObject property*.some-deprecated-int is deprecated*');
        expect(obj.some_deprecated_int).toBe(35);
        GLib.test_assert_expected_messages_internal('Gjs', 'testGIMarshalling.js', 0,
            'testAllowToSetGetDeprecatedProperties');
    });

    const JSOverridingProperty = GObject.registerClass(
        class Overriding extends GIMarshallingTests.PropertiesObject {
            constructor(params) {
                super(params);
                this.intValue = 55;
                this.stringValue = 'a string';
            }

            set some_int(v) {
                this.intValue = v;
            }

            get someInt() {
                return this.intValue;
            }

            set someString(v) {
                this.stringValue = v;
            }

            get someString() {
                return this.stringValue;
            }
        });

    it('can be overridden from JS', function () {
        const intHandler = jasmine.createSpy('handle-some-int');
        const stringHandler = jasmine.createSpy('handle-some-string');
        const overriding = new JSOverridingProperty({
            'someInt': 45,
            'someString': 'other string',
        });
        const ids = [];
        ids.push(overriding.connect('notify::some-int', intHandler));
        ids.push(overriding.connect('notify::some-string', stringHandler));

        expect(overriding['some-int']).toBe(45);
        expect(overriding.someInt).toBe(55);
        expect(overriding.some_int).toBeUndefined();
        expect(overriding.intValue).toBe(55);
        expect(overriding.someString).toBe('a string');
        expect(overriding.some_string).toBe('other string');
        expect(intHandler).not.toHaveBeenCalled();
        expect(stringHandler).not.toHaveBeenCalled();

        overriding.some_int = 35;
        expect(overriding['some-int']).toBe(45);
        expect(overriding.some_int).toBeUndefined();
        expect(overriding.someInt).toBe(35);
        expect(overriding.intValue).toBe(35);
        expect(intHandler).not.toHaveBeenCalled();

        expect(() => (overriding.someInt = 85)).toThrowError(TypeError);
        expect(overriding['some-int']).toBe(45);
        expect(overriding.someInt).toBe(35);
        expect(overriding.some_int).toBeUndefined();
        expect(overriding.intValue).toBe(35);
        expect(intHandler).not.toHaveBeenCalled();

        overriding['some-int'] = 123;
        expect(overriding['some-int']).toBe(123);
        expect(overriding.someInt).toBe(35);
        expect(overriding.some_int).toBeUndefined();
        expect(overriding.intValue).toBe(35);
        expect(intHandler).toHaveBeenCalledTimes(1);

        overriding['some-string'] = '🐧';
        expect(overriding['some-string']).toBe('🐧');
        expect(overriding.some_string).toBe('🐧');
        expect(overriding.someString).toBe('a string');
        expect(overriding.stringValue).toBe('a string');
        expect(stringHandler).toHaveBeenCalledTimes(1);

        overriding.some_string = '🍕';
        expect(overriding['some-string']).toBe('🍕');
        expect(overriding.some_string).toBe('🍕');
        expect(overriding.someString).toBe('a string');
        expect(overriding.stringValue).toBe('a string');
        expect(stringHandler).toHaveBeenCalledTimes(2);

        overriding.someString = '🍝';
        expect(overriding['some-string']).toBe('🍕');
        expect(overriding.some_string).toBe('🍕');
        expect(overriding.someString).toBe('🍝');
        expect(overriding.stringValue).toBe('🍝');
        expect(stringHandler).toHaveBeenCalledTimes(2);

        ids.forEach(id => overriding.disconnect(id));
    });

    it('can be created from C constructor as well', function () {
        obj = GIMarshallingTests.PropertiesObject.new();
        expect(obj).toBeInstanceOf(GIMarshallingTests.PropertiesObject);
    });
});

describe('GObject properties accessors', function () {
    let obj;
    beforeEach(function () {
        obj = new GIMarshallingTests.PropertiesAccessorsObject();
    });

    function testPropertyGetSet(type, value1, value2) {
        const snakeCase = `some_${type}`;
        const paramCase = snakeCase.replaceAll('_', '-');
        const camelCase = snakeCase.replace(/(_\w)/g,
            match => match.toUpperCase().replace('_', ''));

        [snakeCase, paramCase, camelCase].forEach(propertyName => {
            it(`gets and sets a ${type} property as ${propertyName}`, function () {
                obj[propertyName] = value1;
                expect(obj[propertyName]).toEqual(value1);
                obj[propertyName] = value2;
                expect(obj[propertyName]).toEqual(value2);
            });
        });
    }

    function testPropertyGetSetBigInt(type, value1, value2) {
        const isBigInt = v =>
            v > BigInt(Number.MAX_SAFE_INTEGER) || v < BigInt(Number.MIN_SAFE_INTEGER);
        it(`gets and sets a ${type} property with a bigint`, function () {
            obj[`some_${type}`] = value1;
            expect(warn64(isBigInt(value1), () => obj[`some_${type}`])).toEqual(
                Number(value1));
            obj[`some_${type}`] = value2;
            expect(warn64(isBigInt(value2), () => obj[`some_${type}`])).toEqual(
                Number(value2));
        });
    }

    testPropertyGetSet('boolean', true, false);
    testPropertyGetSet('char', 42, 64);
    testPropertyGetSet('uchar', 42, 64);
    testPropertyGetSet('int', 42, 64);
    testPropertyGetSet('uint', 42, 64);
    testPropertyGetSet('long', 42, 64);
    testPropertyGetSet('ulong', 42, 64);
    testPropertyGetSet('int64', 42, 64);
    testPropertyGetSet('int64', Number.MIN_SAFE_INTEGER, Number.MAX_SAFE_INTEGER);
    testPropertyGetSetBigInt('int64', BigIntLimits.int64.min, BigIntLimits.int64.max);
    testPropertyGetSet('uint64', 42, 64);
    testPropertyGetSetBigInt('uint64', BigIntLimits.int64.max, BigIntLimits.int64.umax);
    testPropertyGetSet('string', 'Gjs', 'is cool!');

    it('get and sets out-of-range values throws', function () {
        expect(() => {
            obj.some_int64 = Limits.int64.max;
        }).toThrowError(/out of range/);
        expect(() => {
            obj.some_int64 = BigIntLimits.int64.max + 1n;
        }).toThrowError(/out of range/);
        expect(() => {
            obj.some_int64 = BigIntLimits.int64.min - 1n;
        }).toThrowError(/out of range/);
        expect(() => {
            obj.some_int64 = BigIntLimits.int64.umax;
        }).toThrowError(/out of range/);
        expect(() => {
            obj.some_int64 = -BigIntLimits.int64.umax;
        }).toThrowError(/out of range/);
        expect(() => {
            obj.some_uint64 = Limits.int64.min;
        }).toThrowError(/out of range/);
        expect(() => {
            obj.some_uint64 = BigIntLimits.int64.umax + 100n;
        }).toThrowError(/out of range/);
    });

    it('gets and sets a float property', function () {
        obj.some_float = Math.E;
        expect(obj.some_float).toBeCloseTo(Math.E);
        obj.some_float = Math.PI;
        expect(obj.some_float).toBeCloseTo(Math.PI);
    });

    it('gets and sets a double property', function () {
        obj.some_double = Math.E;
        expect(obj.some_double).toBeCloseTo(Math.E);
        obj.some_double = Math.PI;
        expect(obj.some_double).toBeCloseTo(Math.PI);
    });

    testPropertyGetSet('strv', ['0', '1', '2'], []);
    testPropertyGetSet('boxed_struct', new GIMarshallingTests.BoxedStruct(),
        new GIMarshallingTests.BoxedStruct({long_: 42}));
    // testPropertyGetSet('boxed_glist', [1, 2, 3], []);
    testPropertyGetSet('gvalue', 42, 'foo');
    testPropertyGetSetBigInt('gvalue', BigIntLimits.int64.umax, BigIntLimits.int64.min);
    testPropertyGetSet('variant', new GLib.Variant('b', true),
        new GLib.Variant('s', 'hello'));
    testPropertyGetSet('variant', new GLib.Variant('x', BigIntLimits.int64.min),
        new GLib.Variant('x', BigIntLimits.int64.max));
    testPropertyGetSet('variant', new GLib.Variant('t', BigIntLimits.int64.max),
        new GLib.Variant('t', BigIntLimits.int64.umax));
    testPropertyGetSet('object', new GObject.Object(),
        new GIMarshallingTests.Object({int: 42}));
    testPropertyGetSet('flags', GIMarshallingTests.Flags.VALUE2,
        GIMarshallingTests.Flags.VALUE1 | GIMarshallingTests.Flags.VALUE2);
    testPropertyGetSet('enum', GIMarshallingTests.GEnum.VALUE2,
        GIMarshallingTests.GEnum.VALUE3);
    testPropertyGetSet('byte_array', Uint8Array.of(1, 2, 3),
        new TextEncoder().encode('👾'));

    it('gets a read-only property', function () {
        expect(obj.some_readonly).toEqual(42);
    });

    it('throws when setting a read-only property', function () {
        expect(() => (obj.some_readonly = 35)).toThrow();
    });

    it('allows to set/get deprecated properties', function () {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_WARNING,
            '*GObject property*.some-deprecated-int is deprecated*');
        obj.some_deprecated_int = 35;
        GLib.test_assert_expected_messages_internal('Gjs', 'testGIMarshalling.js', 0,
            'testAllowToSetGetDeprecatedProperties');

        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_WARNING,
            '*GObject property*.some-deprecated-int is deprecated*');
        expect(obj.some_deprecated_int).toBe(35);
        GLib.test_assert_expected_messages_internal('Gjs', 'testGIMarshalling.js', 0,
            'testAllowToSetGetDeprecatedProperties');
    });
});

describe('GObject signals', function () {
    let obj;
    beforeEach(function () {
        obj = new GIMarshallingTests.SignalsObject();
    });

    function testSignalEmission(type, transfer, value, skip = false) {
        it(`checks emission of signal with ${type} argument and transfer ${transfer}`, function () {
            if (skip)
                pending(skip);

            const signalCallback = jasmine.createSpy('signalCallback');

            if (transfer !== 'none')
                type += `-${transfer}`;

            const signalName = `some_${type}`;
            const funcName = `emit_${type}`.replaceAll('-', '_');
            const signalId = obj.connect(signalName, signalCallback);
            obj[funcName]();
            obj.disconnect(signalId);
            expect(signalCallback).toHaveBeenCalledOnceWith(obj, value);
        });
    }

    ['none', 'container', 'full'].forEach(transfer => {
        testSignalEmission('boxed-gptrarray-utf8', transfer, ['0', '1', '2']);
        testSignalEmission('boxed-gptrarray-boxed-struct', transfer, [
            new GIMarshallingTests.BoxedStruct({long_: 42}),
            new GIMarshallingTests.BoxedStruct({long_: 43}),
            new GIMarshallingTests.BoxedStruct({long_: 44}),
        ]);

        testSignalEmission('hash-table-utf8-int', transfer, {
            '-1': 1,
            '0': 0,
            '1': -1,
            '2': -2,
        });
    });

    ['none', 'full'].forEach(transfer => {
        let skip = false;
        if (transfer === 'full')
            skip = 'https://gitlab.gnome.org/GNOME/gobject-introspection/-/issues/470';

        testSignalEmission('boxed-struct', transfer, jasmine.objectContaining({
            long_: 99,
            string_: 'a string',
            g_strv: ['foo', 'bar', 'baz'],
        }), skip);
    });

    it('with not-ref-counted boxed types with transfer full are properly handled', function () {
        // When using JS side only we can handle properly the problems of
        // https://gitlab.gnome.org/GNOME/gobject-introspection/-/issues/470
        const callbackFunc = jasmine.createSpy('callbackFunc');
        const signalId = obj.connect('some-boxed-struct-full', callbackFunc);
        obj.emit('some-boxed-struct-full',
            new GIMarshallingTests.BoxedStruct({long_: 44}));
        obj.disconnect(signalId);
        expect(callbackFunc).toHaveBeenCalledOnceWith(obj,
            new GIMarshallingTests.BoxedStruct({long_: 44}));
    });

    xit('not-ref-counted boxed types with transfer full originating from C are properly handled', function () {
        const callbackFunc = jasmine.createSpy('callbackFunc');
        const signalId = obj.connect('some-boxed-struct-full', callbackFunc);
        obj.emit_boxed_struct_full();
        obj.disconnect(signalId);
        expect(callbackFunc).toHaveBeenCalledOnceWith(obj,
            new GIMarshallingTests.BoxedStruct({long_: 99, string_: 'a string', g_strv: ['foo', 'bar', 'baz']}));
    }).pend('https://gitlab.gnome.org/GNOME/gobject-introspection/-/issues/470');

    it('can be created from C constructor as well', function () {
        obj = GIMarshallingTests.SignalsObject.new();
        expect(obj).toBeInstanceOf(GIMarshallingTests.SignalsObject);
    });
});

// Adapted from pygobject
describe('GError extra tests', function () {
    it('marshals GError instances through GValue', function () {
        const error = GLib.Error.new_literal(Gio.IOErrorEnum, Gio.IOErrorEnum.FAILED, 'error');
        const error1 = GLib.Error.new_literal(Gio.IOErrorEnum, Gio.IOErrorEnum.FAILED, 'error');
        GIMarshallingTests.compare_two_gerrors_in_gvalue(error, error1);
    });

    it('can be nullable', function () {
        const error = GLib.Error.new_literal(Gio.IOErrorEnum, Gio.IOErrorEnum.FAILED, 'error');
        expect(GIMarshallingTests.nullable_gerror(error)).toBeTruthy();
        expect(GIMarshallingTests.nullable_gerror(null)).toBeFalsy();
    });
});

// Adapted from pygobject
describe('GHashTable extra tests', function () {
    it('marshals a hash table of enums as an in argument', function () {
        GIMarshallingTests.ghashtable_enum_none_in({
            1: GIMarshallingTests.ExtraEnum.VALUE1,
            2: GIMarshallingTests.ExtraEnum.VALUE2,
            3: GIMarshallingTests.ExtraEnum.VALUE3,
        });
    });

    it('marshals a hash table of enums as a return value', function () {
        expect(GIMarshallingTests.ghashtable_enum_none_return()).toEqual({
            1: GIMarshallingTests.ExtraEnum.VALUE1,
            2: GIMarshallingTests.ExtraEnum.VALUE2,
            3: GIMarshallingTests.ExtraEnum.VALUE3,
        });
    });
});

// Adapted from pygobject
describe('Filename tests', function () {
    let workdir;
    beforeAll(function (done) {
        Gio.File.new_tmp_dir_async(null, GLib.PRIORITY_DEFAULT, null, (self, result) => {
            workdir = Gio.File.new_tmp_dir_finish(result);
            done();
        });
    });

    afterAll(function () {
        GLib.rmdir(workdir.get_path());
    });

    it('wrong types', function () {
        expect(() => GIMarshallingTests.filename_copy(23)).toThrowError();
        expect(() => GIMarshallingTests.filename_copy([])).toThrowError();
    });

    it('nullability', function () {
        expect(GIMarshallingTests.filename_copy(null)).toBeNull();
        expect(() => GIMarshallingTests.filename_exists(null)).toThrowError();
    });

    it('round-tripping', function () {
        expect(GIMarshallingTests.filename_copy('foo')).toBe('foo');
    });

    // We run the tests with Latin1 filename encoding, to catch mistakes
    it('various types of paths in GLib encoding', function () {
        const strPath = GIMarshallingTests.filename_copy('ä');
        expect(strPath).toBe('ä');
        expect(GIMarshallingTests.filename_to_glib_repr(strPath))
            .toEqual(Uint8Array.of(0xe4));
    });

    it('various types of path existing', function () {
        const paths = ['foo-2', 'öäü-3'];
        for (const path of paths) {
            const file = workdir.get_child(path);
            const stream = file.create(Gio.FileCreateFlags.NONE, null);
            expect(GIMarshallingTests.filename_exists(file.get_path())).toBeTrue();
            stream.close(null);
            file.delete(null);
        }
    });
});

// Adapted from pygobject
describe('Array of enum extra tests', function () {
    it('marshals a C array of enum values as a return value', function () {
        expect(GIMarshallingTests.enum_array_return_type()).toEqual([0, 1, 42]);
    });
});

// Adapted from pygobject
describe('Flags extra tests', function () {
    it('marshals a 32-high bit flags value as an in argument', function () {
        GIMarshallingTests.extra_flags_large_in(GIMarshallingTests.ExtraFlags.VALUE2);
    });
});

// Adapted from pygobject
describe('UTF-8 strings invalid bytes tests', function () {
    it('handles invalid UTF-8 return values gracefully', function () {
        expect(() => GIMarshallingTests.extra_utf8_full_return_invalid()).toThrowError(TypeError);
    });

    it('handles invalid UTF-8 out arguments gracefully', function () {
        expect(() => GIMarshallingTests.extra_utf8_full_out_invalid()).toThrowError(TypeError);
    });
});
