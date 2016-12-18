const ByteArray = imports.byteArray;
const GIMarshallingTests = imports.gi.GIMarshallingTests;

// We use Gio and GLib to have some objects that we know exist
const Gio = imports.gi.Gio;
const GLib = imports.gi.GLib;
const GObject = imports.gi.GObject;
const Lang = imports.lang;

describe('C array', function () {
    function createStructArray() {
        return [1, 2, 3].map(num => {
            let struct = new GIMarshallingTests.BoxedStruct();
            struct.long_ = num;
            return struct;
        });
    }

    it('can be passed to a function', function () {
        expect(() => GIMarshallingTests.array_in([-1, 0, 1, 2])).not.toThrow();
    });

    it('can be passed to a function with its length parameter before it', function () {
        expect(() => GIMarshallingTests.array_in_len_before([-1, 0, 1, 2]))
            .not.toThrow();
    });

    it('can be passed to a function with zero terminator', function () {
        expect(() => GIMarshallingTests.array_in_len_zero_terminated([-1, 0, 1, 2]))
            .not.toThrow();
    });

    it('can be passed to a function in the style of gtk_init()', function () {
        let [, newArray] = GIMarshallingTests.init_function(null);
        expect(newArray).toEqual([]);
    });

    it('can be returned with zero terminator', function () {
        expect(GIMarshallingTests.array_zero_terminated_return())
            .toEqual(['0', '1', '2']);
    });

    it('can be returned', function () {
        expect(GIMarshallingTests.array_return()).toEqual([-1, 0, 1, 2]);
    });

    it('can be returned along with other arguments', function () {
        let [array, sum] = GIMarshallingTests.array_return_etc(9, 5);
        expect(sum).toEqual(14);
        expect(array).toEqual([9, 0, 1, 5]);
    });

    it('can be an out argument', function () {
        expect(GIMarshallingTests.array_out()).toEqual([-1, 0, 1, 2]);
    });

    it('can be an out argument along with other arguments', function () {
        let [array, sum] = GIMarshallingTests.array_out_etc(9, 5);
        expect(sum).toEqual(14);
        expect(array).toEqual([9, 0, 1, 5]);
    });

    it('can be an in-out argument', function () {
        expect(GIMarshallingTests.array_inout([-1, 0, 1, 2]))
            .toEqual([-2, -1, 0, 1, 2]);
    });

    it('can be an in-out argument along with other arguments', function () {
        let [array, sum] = GIMarshallingTests.array_inout_etc(9, [-1, 0, 1, 2], 5);
        expect(sum).toEqual(14);
        expect(array).toEqual([9, -1, 0, 1, 5]);
    });

    // Run twice to ensure that copies are correctly made for (transfer full)
    it('copies correctly on transfer full', function () {
        let array = createStructArray();
        expect(() => {
            GIMarshallingTests.array_struct_take_in(array);
            GIMarshallingTests.array_struct_take_in(array);
        }).not.toThrow();
    });

    describe('of structs', function () {
        it('can be passed to a function', function () {
            expect(() => GIMarshallingTests.array_struct_in(createStructArray()))
                .not.toThrow();
        });

        it('can be returned with zero terminator', function () {
            let structArray = GIMarshallingTests.array_zero_terminated_return_struct();
            expect(structArray.map(e => e.long_)).toEqual([42, 43, 44]);
        });
    });

    describe('of booleans', function () {
        it('is coerced to true/false when passed to a function', function () {
            expect(() => GIMarshallingTests.array_bool_in([-1, 0, 1, 2]))
                .not.toThrow();
        });

        it('can be an out argument', function () {
            expect(GIMarshallingTests.array_bool_out())
                .toEqual([true, false, true, true]);
        });
    });

    describe('of unichars', function () {
        it('can be passed to a function', function () {
            expect(() => GIMarshallingTests.array_unichar_in('const \u2665 utf8'))
                .not.toThrow();
        });

        it('can be an out argument', function () {
            expect(GIMarshallingTests.array_unichar_out())
                .toEqual('const \u2665 utf8');
        });

        it('can be returned with zero terminator', function () {
            expect(GIMarshallingTests.array_zero_terminated_return_unichar())
                .toEqual('const \u2665 utf8');
        });

        it('can be implicitly converted from a number array', function () {
            expect(() => GIMarshallingTests.array_unichar_in([0x63, 0x6f, 0x6e, 0x73,
                0x74, 0x20, 0x2665, 0x20, 0x75, 0x74, 0x66, 0x38])).not.toThrow();
        });
    });

    describe('of strings', function () {
        it('can be passed to a function', function () {
            expect(() => GIMarshallingTests.array_string_in(['foo', 'bar']))
                .not.toThrow();
        });
    });

    describe('of enums', function () {
        it('can be passed to a function', function () {
            expect(() => GIMarshallingTests.array_enum_in([GIMarshallingTests.Enum.VALUE1,
                GIMarshallingTests.Enum.VALUE2, GIMarshallingTests.Enum.VALUE3])).not.toThrow();
        });
    });

    describe('of bytes', function () {
        it('can be an in argument with length', function () {
            expect(() => GIMarshallingTests.array_in_guint8_len([-1, 0, 1, 2]))
                .not.toThrow();
        });

        it('can be implicitly converted from a string', function () {
            expect(() => GIMarshallingTests.array_uint8_in('abcd')).not.toThrow();
        });
    });

    describe('of 64-bit ints', function () {
        it('can be passed to a function', function () {
            expect(() => GIMarshallingTests.array_int64_in([-1, 0, 1, 2]))
                .not.toThrow();
            expect(() => GIMarshallingTests.array_uint64_in([-1, 0, 1, 2]))
                .not.toThrow();
        });

        it('can be an in argument with length', function () {
            expect(() => GIMarshallingTests.array_in_guint64_len([-1, 0, 1, 2]))
                .not.toThrow();
        });
    });
});

describe('GArray', function () {
    describe('of integers', function () {
        it('can be passed in with transfer none', function () {
            expect(() => GIMarshallingTests.garray_int_none_in([-1, 0, 1, 2]))
                .not.toThrow();
        });

        it('can be returned with transfer none', function () {
            expect(GIMarshallingTests.garray_int_none_return())
                .toEqual([-1, 0, 1, 2]);
        });
    });

    describe('of strings', function () {
        it('can be passed in with transfer none', function () {
            expect(() => GIMarshallingTests.garray_utf8_none_in(['0', '1', '2']))
                .not.toThrow();
        });

        ['return', 'out'].forEach(method => {
            ['none', 'container', 'full'].forEach(transfer => {
                it('can be passed as ' + method + ' with transfer ' + transfer, function () {
                    expect(GIMarshallingTests['garray_utf8_' + transfer + '_' + method]())
                        .toEqual(['0', '1', '2']);
                });
            });
        });
    });

    describe('of booleans', function () {
        it('can be passed in with transfer none', function () {
            expect(() => GIMarshallingTests.garray_bool_none_in([-1, 0, 1, 2]))
                .not.toThrow();
        });
    });

    describe('of unichars', function () {
        it('can be passed in with transfer none', function () {
            expect(() => GIMarshallingTests.garray_unichar_none_in('const \u2665 utf8'))
                .not.toThrow();
        });

        it('can be implicitly converted from a number array', function () {
            expect(() => GIMarshallingTests.garray_unichar_none_in([0x63, 0x6f, 0x6e,
                0x73, 0x74, 0x20, 0x2665, 0x20, 0x75, 0x74, 0x66, 0x38])).not.toThrow();
        });
    });
});

describe('GByteArray', function () {
    const refByteArray = ByteArray.fromArray([0, 49, 0xFF, 51]);

    it('can be passed in with transfer none', function () {
        expect(() => GIMarshallingTests.bytearray_none_in(refByteArray))
            .not.toThrow();
    });

    it('can be returned with transfer full', function () {
        expect(GIMarshallingTests.bytearray_full_return()).toEqual(refByteArray);
    });

    it('can be implicitly converted from a normal array', function () {
        expect(() => GIMarshallingTests.bytearray_none_in([0, 49, 0xFF, 51]))
            .not.toThrow();
    });
});

describe('GBytes', function () {
    const refByteArray = ByteArray.fromArray([0, 49, 0xFF, 51]);

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
        let bytes = GLib.Bytes.new("const \u2665 utf8");
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
        expect(() => GIMarshallingTests.gbytes_none_in(array.toGBytes()))
            .not.toThrow();
    });

    it('cannot be passed to a function expecting a byte array', function () {
        let bytes = GLib.Bytes.new([97, 98, 99, 100]);
        expect(() => GIMarshallingTests.array_uint8_in(bytes.toArray())).not.toThrow();
        expect(() => GIMarshallingTests.array_uint8_in(bytes)).toThrow();
    });
});

describe('GPtrArray', function () {
    const refArray = ['0', '1', '2'];

    it('can be passed to a function with transfer none', function () {
        expect(() => GIMarshallingTests.gptrarray_utf8_none_in(refArray))
            .not.toThrow();
    });

    ['return', 'out'].forEach(method => {
        ['none', 'container', 'full'].forEach(transfer => {
            it('can be passed as ' + method + ' with transfer ' + transfer, function () {
                expect(GIMarshallingTests['gptrarray_utf8_' + transfer + '_' + method]())
                    .toEqual(refArray);
            });
        });
    });
});

describe('GHashTable', function () {
    const INT_DICT = {
        '-1': 1,
        0: 0,
        1: -1,
        2: -2,
    };
    const STRING_DICT = {
        '-1': '1',
        0: '0',
        1: '-1',
        2: '-2',
    };
    const NUMBER_DICT = {
        '-1': -0.1,
        0: 0,
        1: 0.1,
        2: 0.2,
    };
    const STRING_DICT_OUT = {
        '-1': '1',
        0: '0',
        1: '1',
    };

    it('can be passed in with integer value type', function () {
        expect(() => GIMarshallingTests.ghashtable_int_none_in(INT_DICT))
            .not.toThrow();
    });

    it('can be passed in with string value type', function () {
        expect(() => GIMarshallingTests.ghashtable_utf8_none_in(STRING_DICT))
            .not.toThrow();
    });

    it('can be passed in with float value type', function () {
        expect(() => GIMarshallingTests.ghashtable_float_in(NUMBER_DICT))
            .not.toThrow();
    });

    it('can be passed in with double value type', function () {
        expect(() => GIMarshallingTests.ghashtable_double_in(NUMBER_DICT))
            .not.toThrow();
    });

    it('can be passed in with int64 value type', function () {
        const int64Dict = {
            '-1': -1,
            0: 0,
            1: 1,
            2: 0x100000000,
        };
        expect(() => GIMarshallingTests.ghashtable_int64_in(int64Dict))
            .not.toThrow();
    });

    it('can be passed in with uint64 value type', function () {
        const uint64Dict = {
            '-1': 0x100000000,
            0: 0,
            1: 1,
            2: 2,
        };
        expect(() => GIMarshallingTests.ghashtable_uint64_in(uint64Dict))
            .not.toThrow();
    });

    it('can be returned with integer value type', function () {
        expect(GIMarshallingTests.ghashtable_int_none_return()).toEqual(INT_DICT);
    });

    ['return', 'out'].forEach(method => {
        ['none', 'container', 'full'].forEach(transfer => {
            it('can be passed as ' + method + ' with transfer ' + transfer, function () {
                expect(GIMarshallingTests['ghashtable_utf8_' + transfer + '_' + method]())
                    .toEqual(STRING_DICT);
            });
        });
    });

    it('can be passed as inout with transfer none', function () {
        expect(GIMarshallingTests.ghashtable_utf8_none_inout(STRING_DICT))
            .toEqual(STRING_DICT_OUT);
    });

    xit('can be passed as inout with transfer container', function () {
        expect(GIMarshallingTests.ghashtable_utf8_container_inout(STRING_DICT))
            .toEqual(STRING_DICT_OUT);
    }).pend('Container transfer for in parameters not supported');

    xit('can be passed as inout with transfer full', function () {
        expect(GIMarshallingTests.ghashtable_utf8_full_inout(STRING_DICT))
            .toEqual(STRING_DICT_OUT);
    }).pend('https://bugzilla.gnome.org/show_bug.cgi?id=773763');
});

describe('GValue', function () {
    it('can be passed into a function and packed', function () {
        expect(() => GIMarshallingTests.gvalue_in(42)).not.toThrow();
    });

    it('array can be passed into a function and packed', function () {
        expect(() => GIMarshallingTests.gvalue_flat_array([42, '42', true]))
            .not.toThrow();
    });

    xit('enum can be passed into a function and packed', function () {
        expect(() => GIMarshallingTests.gvalue_in_enum(GIMarshallingTests.Enum.VALUE_3))
            .not.toThrow();
    }).pend("GJS doesn't support native enum types");

    it('can be returned and unpacked', function () {
        expect(GIMarshallingTests.gvalue_return()).toEqual(42);
    });

    it('can be passed as an out argument and unpacked', function () {
        expect(GIMarshallingTests.gvalue_out()).toEqual(42);
    });

    it('array can be passed as an out argument and unpacked', function () {
        expect(GIMarshallingTests.return_gvalue_flat_array())
            .toEqual([42, '42', true]);
    });

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

    it('type objects can be converted from primitive-like types', function () {
        expect(() => GIMarshallingTests.gvalue_in_with_type(42, GObject.Int))
            .not.toThrow();
        expect(() => GIMarshallingTests.gvalue_in_with_type(42.5, GObject.Double))
            .not.toThrow();
        expect(() => GIMarshallingTests.gvalue_in_with_type(42.5, Number))
            .not.toThrow();
    });

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

    xit('can have its type inferred as a foreign struct', function () {
        let Cairo;
        try {
            Cairo = imports.cairo;
        } catch(e) {
            pending('Compiled without Cairo support');
            return;
        }

        let surface = new Cairo.ImageSurface(Cairo.Format.ARGB32, 2, 2);
        let cr = new Cairo.Context(surface);
        expect(() => GIMarshallingTests.gvalue_in_with_type(cr, Cairo.Context))
            .not.toThrow();
        expect(() => GIMarshallingTests.gvalue_in_with_type(surface, Cairo.Surface))
            .not.toThrow();
    }).pend('Errors out with "not a subclass of GObject_Boxed"');
});

describe('GType', function () {
    it('can be passed into a function', function () {
        expect(() => GIMarshallingTests.gtype_in(GObject.TYPE_NONE)).not.toThrow();
        expect(() => GIMarshallingTests.gtype_in(GObject.VoidType)).not.toThrow();
        expect(() => GIMarshallingTests.gtype_string_in(GObject.TYPE_STRING)).not.toThrow();
        expect(() => GIMarshallingTests.gtype_string_in(GObject.String)).not.toThrow();
    });

    it('can be returned', function () {
        expect(GIMarshallingTests.gtype_return()).toEqual(GObject.TYPE_NONE);
        expect(GIMarshallingTests.gtype_string_return())
            .toEqual(GObject.TYPE_STRING);
    });

    it('can be passed as an out argument', function () {
        expect(GIMarshallingTests.gtype_out()).toEqual(GObject.TYPE_NONE);
        expect(GIMarshallingTests.gtype_string_out()).toEqual(GObject.TYPE_STRING);
    });

    it('can be passed as an inout argument', function () {
        expect(GIMarshallingTests.gtype_inout(GObject.TYPE_NONE))
            .toEqual(GObject.TYPE_INT);
    });

    it('can be implicitly converted from a JS type', function () {
        expect(() => GIMarshallingTests.gtype_string_in(String)).not.toThrow();
    });
});

function callback_return_value_only() {
    return 42;
}

function callback_one_out_parameter() {
    return 43;
}

function callback_multiple_out_parameters() {
    return [44, 45];
}

function callback_return_value_and_one_out_parameter() {
    return [46, 47];
}

function callback_return_value_and_multiple_out_parameters() {
    return [48, 49, 50];
}

describe('Callback', function () {
    it('marshals a return value', function () {
        expect(GIMarshallingTests.callback_return_value_only(callback_return_value_only))
            .toEqual(42);
    });

    it('marshals one out parameter', function () {
        expect(GIMarshallingTests.callback_one_out_parameter(callback_one_out_parameter))
            .toEqual(43);
    });

    it('marshals multiple out parameters', function () {
        expect(GIMarshallingTests.callback_multiple_out_parameters(callback_multiple_out_parameters))
            .toEqual([44, 45]);
    });

    it('marshals a return value and one out parameter', function () {
        expect(GIMarshallingTests.callback_return_value_and_one_out_parameter(callback_return_value_and_one_out_parameter))
            .toEqual([46, 47]);
    });

    it('marshals a return value and multiple out parameters', function () {
        expect(GIMarshallingTests.callback_return_value_and_multiple_out_parameters(callback_return_value_and_multiple_out_parameters))
            .toEqual([48, 49, 50]);
    });
});

const VFuncTester = new Lang.Class({
    Name: 'VFuncTester',
    Extends: GIMarshallingTests.Object,

    vfunc_vfunc_return_value_only: callback_return_value_only,
    vfunc_vfunc_one_out_parameter: callback_one_out_parameter,
    vfunc_vfunc_multiple_out_parameters: callback_multiple_out_parameters,
    vfunc_vfunc_return_value_and_one_out_parameter: callback_return_value_and_one_out_parameter,
    vfunc_vfunc_return_value_and_multiple_out_parameters: callback_return_value_and_multiple_out_parameters
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
});

describe('Interface', function () {
    it('can be returned', function () {
        let ifaceImpl = new GIMarshallingTests.InterfaceImpl();
        let itself = ifaceImpl.get_as_interface();
        expect(ifaceImpl).toEqual(itself);
    });
});
