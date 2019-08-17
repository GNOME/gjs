// Load overrides for GIMarshallingTests
imports.overrides.searchPath.unshift('resource:///org/gjs/jsunit/modules/overrides');

const ByteArray = imports.byteArray;
const GIMarshallingTests = imports.gi.GIMarshallingTests;

// We use Gio and GLib to have some objects that we know exist
const Gio = imports.gi.Gio;
const GLib = imports.gi.GLib;
const GObject = imports.gi.GObject;

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

function testSimpleMarshalling(root, value, inoutValue, options = {}) {
    testReturnValue(root, value, options.returnv);
    testInParameter(root, value, options.in);
    testOutParameter(root, value, options.out);
    testInoutParameter(root, value, inoutValue, options.inout);
}

function testTransferMarshalling(root, value, inoutValue, options = {}) {
    describe('with transfer none', function () {
        testSimpleMarshalling(`${root}_none`, value, inoutValue, options.none);
    });
    describe('with transfer full', function () {
        const fullOptions = {
            in: {
                omit: true,  // this case is not in the test suite
            },
            inout: {
                skip: 'https://gitlab.gnome.org/GNOME/gobject-introspection/issues/192',
            },
        };
        Object.assign(fullOptions, options.full);
        testSimpleMarshalling(`${root}_full`, value, inoutValue, fullOptions);
    });
}

function testContainerMarshalling(root, value, inoutValue, options = {}) {
    testTransferMarshalling(root, value, inoutValue, options);
    describe('with transfer container', function () {
        const containerOptions = {
            in: {
                omit: true,  // this case is not in the test suite
            },
            inout: {
                skip: 'https://gitlab.gnome.org/GNOME/gjs/issues/44',
            },
        };
        Object.assign(containerOptions, options.container);
        testSimpleMarshalling(`${root}_container`, value, inoutValue, containerOptions);
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
Object.assign(Limits.short, Limits.int16);
Object.assign(Limits.int, Limits.int32);
// Platform dependent sizes; expand definitions as needed
if (GLib.SIZEOF_LONG === 8)
    Object.assign(Limits.long, Limits.int64);
else
    Object.assign(Limits.long, Limits.int32);
if (GLib.SIZEOF_SSIZE_T === 8)
    Object.assign(Limits.ssize, Limits.int64);
else
    Object.assign(Limits.ssize, Limits.int32);

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
            testSimpleMarshalling('boolean', bool, !bool, {
                returnv: {
                    funcName: `boolean_return_${bool}`,
                },
                in: {
                    funcName: `boolean_in_${bool}`,
                },
                out: {
                    funcName: `boolean_out_${bool}`,
                },
                inout: {
                    funcName: `boolean_inout_${bool}_${!bool}`,
                },
            });
        });
    });
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

            it('marshals unsigned value as an inout parameter', function () {
                skip64(bit64);
                expect(GIMarshallingTests[`${utype}_inout`](umax)).toEqual(0);
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

            it('marshals value as an inout parameter', function () {
                expect(GIMarshallingTests[`${type}_inout`](max)).toBeCloseTo(min, 10);
            });
        });
    });
});

describe('time_t', function () {
    testSimpleMarshalling('time_t', 1234567890, 0);
});

describe('GType', function () {
    describe('void', function () {
        testSimpleMarshalling('gtype', GObject.TYPE_NONE, GObject.TYPE_INT);
    });

    describe('string', function () {
        testSimpleMarshalling('gtype_string', GObject.TYPE_STRING, null, {
            inout: {omit: true},
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
    testTransferMarshalling('utf8', 'const ♥ utf8', '');

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

    xit('marshals an inout empty array', function () {
        const [, newArray] = GIMarshallingTests.init_function([]);
        expect(newArray).toEqual([]);
    }).pend('https://gitlab.gnome.org/GNOME/gjs/issues/88');

    xit('marshals an inout array', function () {
        const [, newArray] = GIMarshallingTests.init_function(['--foo', '--bar']);
        expect(newArray).toEqual(['--foo']);
    }).pend('https://gitlab.gnome.org/GNOME/gjs/issues/88');
});

describe('Fixed-size C array', function () {
    describe('of ints', function () {
        testReturnValue('array_fixed_int', [-1, 0, 1, 2]);
        testInParameter('array_fixed_int', [-1, 0, 1, 2]);
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
});

describe('C array with length', function () {
    function createStructArray(StructType = GIMarshallingTests.BoxedStruct) {
        return [1, 2, 3].map(num => {
            let struct = new StructType();
            struct.long_ = num;
            return struct;
        });
    }

    testSimpleMarshalling('array', [-1, 0, 1, 2], [-2, -1, 0, 1, 2]);

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
        expect(() => GIMarshallingTests.array_uint8_in(ByteArray.fromString('abcd')))
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

        // Intercept message; see https://gitlab.gnome.org/GNOME/gjs/issues/267
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_MESSAGE,
            '*Too many arguments*');

        expect(() => GIMarshallingTests.multi_array_key_value_in(keys, values)).not.toThrow();

        GLib.test_assert_expected_messages_internal('Gjs',
            'testGIMarshalling.js', 0, 'Ignore message');
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

    it('marshals an array with a 64-bit length parameter', function () {
        expect(() => GIMarshallingTests.array_in_guint64_len([-1, 0, 1, 2])).not.toThrow();
    });

    it('marshals an array with an 8-bit length parameter', function () {
        expect(() => GIMarshallingTests.array_in_guint8_len([-1, 0, 1, 2])).not.toThrow();
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
});

describe('Zero-terminated C array', function () {
    describe('of strings', function () {
        testSimpleMarshalling('array_zero_terminated', ['0', '1', '2'],
            ['-1', '0', '1', '2']);
    });

    it('marshals null as a zero-terminated array return value', function () {
        expect(GIMarshallingTests.array_zero_terminated_return_null()).toEqual(null);
    });

    it('marshals an array of structs as a return value', function () {
        let structArray = GIMarshallingTests.array_zero_terminated_return_struct();
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
            xit(`marshals as a transfer-${transfer} in and out parameter`, function () {
                const returnedArray =
                    GIMarshallingTests[`array_gvariant_${transfer}_in`](variantArray);
                expect(returnedArray.map(v => v.deepUnpack())).toEqual([27, 'Hello']);
            }).pend('https://gitlab.gnome.org/GNOME/gjs/issues/269');
        });
    });
});

describe('GArray', function () {
    describe('of ints with transfer none', function () {
        testReturnValue('garray_int_none', [-1, 0, 1, 2]);
        testInParameter('garray_int_none', [-1, 0, 1, 2]);
    });

    it('marshals int64s as a transfer-none return value', function () {
        expect(warn64(true, GIMarshallingTests.garray_uint64_none_return))
            .toEqual([0, Limits.int64.umax]);
    });

    describe('of strings', function () {
        testContainerMarshalling('garray_utf8', ['0', '1', '2'], ['-2', '-1', '0', '1']);

        it('marshals as a transfer-full caller-allocated out parameter', function () {
            expect(GIMarshallingTests.garray_utf8_full_out_caller_allocated())
                .toEqual(['0', '1', '2']);
        }).pend('https://gitlab.gnome.org/GNOME/gjs/issues/106');
    });

    xit('marshals boxed structs as a transfer-full return value', function () {
        expect(GIMarshallingTests.garray_boxed_struct_full_return().map(e => e.long_))
            .toEqual([42, 43, 44]);
    }).pend('https://gitlab.gnome.org/GNOME/gobject-introspection/merge_requests/160');

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
        testContainerMarshalling('garray_utf8', ['0', '1', '2'], ['-2', '-1', '0', '1']);
    });

    describe('of structs', function () {
        xit('can be returned with transfer full', function () {
            expect(GIMarshallingTests.gptrarray_boxed_struct_full_return().map(e => e.long_))
                .toEqual([42, 43, 44]);
        }).pend('https://gitlab.gnome.org/GNOME/gobject-introspection/merge_requests/160');
    });
});

describe('GByteArray', function () {
    const refByteArray = Uint8Array.from([0, 49, 0xFF, 51]);

    testReturnValue('bytearray_full', refByteArray);

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

    it('can be implicitly converted from a ByteArray', function () {
        expect(() => GIMarshallingTests.gbytes_none_in(refByteArray))
            .not.toThrow();
    });

    it('can be created from a string and is encoded in UTF-8', function () {
        let bytes = GLib.Bytes.new('const \u2665 utf8');
        expect(() => GIMarshallingTests.utf8_as_uint8array_in(bytes.toArray()))
            .not.toThrow();
    });

    it('turns into a GByteArray on assignment', function () {
        let bytes = GIMarshallingTests.gbytes_full_return();
        let array = bytes.toArray();  // Array should just be holding a ref, not a copy
        expect(array[1]).toEqual(49);
        array[1] = 42;  // Assignment should force to GByteArray
        expect(array[1]).toEqual(42);
        array[1] = 49;  // Flip the value back
        // Now convert back to GBytes
        expect(() => GIMarshallingTests.gbytes_none_in(ByteArray.toGBytes(array)))
            .not.toThrow();
    });

    it('cannot be passed to a function expecting a byte array', function () {
        let bytes = GLib.Bytes.new([97, 98, 99, 100]);
        expect(() => GIMarshallingTests.array_uint8_in(bytes.toArray())).not.toThrow();
        expect(() => GIMarshallingTests.array_uint8_in(bytes)).toThrow();
    });
});

describe('GStrv', function () {
    testSimpleMarshalling('gstrv', ['0', '1', '2'], ['-1', '0', '1', '2']);
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
                ['-2', '-1', '0', '1']);
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
        testContainerMarshalling('ghashtable_utf8', stringDict, stringDictOut);
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
});

describe('GValue', function () {
    testSimpleMarshalling('gvalue', 42, '42', {
        inout: {
            skip: 'https://gitlab.gnome.org/GNOME/gobject-introspection/issues/192',
        },
    });

    xit('marshals as an int64 in parameter', function () {
        expect(() => GIMarshallingTests.gvalue_int64_in(Limits.int64.max)).not.toThrow();
    }).pend('https://gitlab.gnome.org/GNOME/gjs/issues/271');

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
        // Let's assume this test doesn't expect that the modified GValue makes
        // it back to the caller; I don't see how that could be achieved.
        // See https://gitlab.gnome.org/GNOME/gjs/issues/80
    });

    xit('enum can be passed into a function and packed', function () {
        expect(() => GIMarshallingTests.gvalue_in_enum(GIMarshallingTests.Enum.VALUE3))
            .not.toThrow();
    }).pend("GJS doesn't support native enum types");

    it('marshals as an int64 out parameter', function () {
        expect(GIMarshallingTests.gvalue_int64_out()).toEqual(Limits.int64.max);
    });

    it('marshals as a caller-allocated out parameter', function () {
        expect(GIMarshallingTests.gvalue_out_caller_allocates()).toEqual(42);
    });

    it('array can be passed into a function and packed', function () {
        expect(() => GIMarshallingTests.gvalue_flat_array([42, '42', true]))
            .not.toThrow();
    });

    it('array can be passed as an out argument and unpacked', function () {
        expect(GIMarshallingTests.return_gvalue_flat_array())
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
});

describe('Raw pointers', function () {
    xit('can be roundtripped at least if the pointer is null', function () {
        expect(GIMarshallingTests.pointer_in_return(null)).toBeNull();
    }).pend('https://gitlab.gnome.org/GNOME/gjs/merge_requests/46');
});

describe('Registered enum type', function () {
    testSimpleMarshalling('genum', GIMarshallingTests.GEnum.VALUE3,
        GIMarshallingTests.GEnum.VALUE1, {
            returnv: {
                funcName: 'genum_returnv',
            },
        });
});

describe('Bare enum type', function () {
    testSimpleMarshalling('enum', GIMarshallingTests.Enum.VALUE3,
        GIMarshallingTests.Enum.VALUE1, {
            returnv: {
                funcName: 'enum_returnv',
            },
        });
});

describe('Registered flags type', function () {
    testSimpleMarshalling('flags', GIMarshallingTests.Flags.VALUE2,
        GIMarshallingTests.Flags.VALUE1, {
            returnv: {
                funcName: 'flags_returnv',
            },
        });
});

describe('Bare flags type', function () {
    testSimpleMarshalling('no_type_flags', GIMarshallingTests.NoTypeFlags.VALUE2,
        GIMarshallingTests.NoTypeFlags.VALUE1, {
            returnv: {
                funcName: 'no_type_flags_returnv',
            },
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

    xit('marshals as a return value', function () {
        expect(union.long_).toEqual(42);
    }).pend('https://gitlab.gnome.org/GNOME/gjs/issues/273');

    it('marshals as the this-argument of a method', function () {
        expect(() => union.inv()).not.toThrow();  // was this supposed to be static?
        expect(() => union.method()).not.toThrow();
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

const VFuncTester = GObject.registerClass(class VFuncTester extends GIMarshallingTests.Object {
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
        }
    }

    vfunc_vfunc_return_enum() {
        return GIMarshallingTests.Enum.VALUE2;
    }

    vfunc_vfunc_out_enum() {
        return GIMarshallingTests.Enum.VALUE3;
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
});

describe('Virtual function', function () {
    let tester;
    beforeEach(function () {
        tester = new VFuncTester();
    });

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

    it('marshals an array out parameter', function () {
        expect(tester.vfunc_array_out_parameter()).toEqual([50, 51]);
    });

    it('marshals a caller-allocated GValue out parameter', function () {
        expect(tester.vfunc_caller_allocated_out_parameter()).toEqual(52);
    }).pend('https://gitlab.gnome.org/GNOME/gjs/issues/74');

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

    it('marshals an enum return value', function () {
        expect(tester.vfunc_return_enum()).toEqual(GIMarshallingTests.Enum.VALUE2);
    });

    it('marshals an enum out parameter', function () {
        expect(tester.vfunc_out_enum()).toEqual(GIMarshallingTests.Enum.VALUE3);
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
    // 1 reference = the object is owned only by JS.
    // 2 references = the object is owned by JS and the vfunc caller.
    testVfuncRefcount('return', 'none', 1);
    testVfuncRefcount('return', 'full', 2);
    testVfuncRefcount('out', 'none', 1);
    testVfuncRefcount('out', 'full', 2, {
        skip: 'https://gitlab.gnome.org/GNOME/gjs/issues/275',
    });
    testVfuncRefcount('in', 'none', 2, {}, GIMarshallingTests.Object);
    testVfuncRefcount('in', 'full', 1, {
        skip: 'https://gitlab.gnome.org/GNOME/gjs/issues/275',
    }, GIMarshallingTests.Object);
});

describe('Inherited GObject', function () {
    ['SubObject', 'SubSubObject'].forEach(klass => {
        describe(klass, function () {
            it('has a parent method that can be called', function () {
                const o = new GIMarshallingTests.SubObject({int: 42});
                expect(() => o.method()).not.toThrow();
            });

            it('has a method that can be called', function () {
                const o = new GIMarshallingTests.SubObject({int: 0});
                expect(() => o.sub_method()).not.toThrow();
            });

            it('has an overridden method that can be called', function () {
                const o = new GIMarshallingTests.SubObject({int: 0});
                expect(() => o.overwritten_method()).not.toThrow();
            });

            it('has a method with default implementation can be called', function () {
                const o = new GIMarshallingTests.SubObject({int: 42});
                o.method_with_default_implementation(43);
                expect(o.int).toEqual(43);
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
                this.stuff = variantArray.map(v => v.deepUnpack());
            }
        });
        const i3 = new I3Impl();
        i3.test_variant_array_in([
            new GLib.Variant('b', true),
            new GLib.Variant('s', 'hello'),
            new GLib.Variant('i', 42),
        ]);
        expect(i3.stuff).toEqual([true, 'hello', 42]);
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
        expect(() => GIMarshallingTests.gerror_array_in([-1, 0, 1, 2])).toThrow();
    });

    it('marshals a GError** elsewhere in the signature as an out parameter', function () {
        expect(GIMarshallingTests.gerror_out()).toEqual([
            jasmine.any(GLib.Error),
            'we got an error, life is shit',
        ]);
    });

    it('marshals a GError** elsewhere in the signature as an out parameter with transfer none', function () {
        expect(GIMarshallingTests.gerror_out_transfer_none()).toEqual([
            jasmine.any(GLib.Error),
            'we got an error, life is shit',
        ]);
    });

    it('marshals GError as a return value', function () {
        expect(GIMarshallingTests.gerror_return()).toEqual(jasmine.any(GLib.Error));
    });
});

describe('Overrides', function () {
    it('can add constants', function () {
        expect(GIMarshallingTests.OVERRIDES_CONSTANT).toEqual(7);
    });

    it('can override a struct method', function () {
        const struct = new GIMarshallingTests.OverridesStruct();
        expect(struct.method()).toEqual(6);
    });

    it('can override an object constructor', function () {
        const obj = new GIMarshallingTests.OverridesObject(42);
        expect(obj.num).toEqual(42);
    });

    it('can override an object method', function () {
        const obj = new GIMarshallingTests.OverridesObject();
        expect(obj.method()).toEqual(6);
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
});

describe('GObject properties', function () {
    let obj;
    beforeEach(function () {
        obj = new GIMarshallingTests.PropertiesObject();
    });

    function testPropertyGetSet(type, value1, value2, skip = false) {
        it(`gets and sets a ${type} property`, function () {
            if (skip)
                pending(skip);
            obj[`some_${type}`] = value1;
            expect(obj[`some_${type}`]).toEqual(value1);
            obj[`some_${type}`] = value2;
            expect(obj[`some_${type}`]).toEqual(value2);
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
    testPropertyGetSet('uint64', 42, 64);

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
    testPropertyGetSet('boxed_glist', null, null);
    testPropertyGetSet('gvalue', 42, 'foo');
    testPropertyGetSet('variant', new GLib.Variant('b', true),
        new GLib.Variant('s', 'hello'));
    testPropertyGetSet('object', new GObject.Object(),
        new GIMarshallingTests.Object({int: 42}));
    testPropertyGetSet('flags', GIMarshallingTests.Flags.VALUE2,
        GIMarshallingTests.Flags.VALUE1 | GIMarshallingTests.Flags.VALUE2);
    testPropertyGetSet('enum', GIMarshallingTests.GEnum.VALUE2,
        GIMarshallingTests.GEnum.VALUE3);
    testPropertyGetSet('byte_array', Uint8Array.of(1, 2, 3),
        ByteArray.fromString('👾'),
        'https://gitlab.gnome.org/GNOME/gjs/issues/276');

    it('gets a read-only property', function () {
        expect(obj.some_readonly).toEqual(42);
    });

    it('throws when setting a read-only property', function () {
        expect(() => (obj.some_readonly = 35)).toThrow();
    });
});
