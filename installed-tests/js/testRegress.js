// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC
// SPDX-FileCopyrightText: 2008 Red Hat, Inc.

const Regress = imports.gi.Regress;

// We use Gio to have some objects that we know exist
imports.gi.versions.Gtk = '3.0';
const GLib = imports.gi.GLib;
const Gio = imports.gi.Gio;
const GObject = imports.gi.GObject;

describe('Life, the Universe and Everything', function () {
    it('includes null return value', function () {
        expect(Regress.test_return_allow_none()).toBeNull();
        expect(Regress.test_return_nullable()).toBeNull();
    });

    it('includes booleans', function () {
        expect(Regress.test_boolean(false)).toBe(false);
        expect(Regress.test_boolean(true)).toBe(true);
        expect(Regress.test_boolean_true(true)).toBe(true);
        expect(Regress.test_boolean_false(false)).toBe(false);
    });

    [8, 16, 32, 64].forEach(bits => {
        it(`includes ${bits}-bit integers`, function () {
            const method = `test_int${bits}`;
            expect(Regress[method](42)).toBe(42);
            expect(Regress[method](-42)).toBe(-42);
            expect(Regress[method](undefined)).toBe(0);
        });

        it(`includes unsigned ${bits}-bit integers`, function () {
            const method = `test_uint${bits}`;
            expect(Regress[method](42)).toBe(42);
            expect(Regress[method](undefined)).toBe(0);
        });
    });

    ['short', 'int', 'long', 'ssize', 'float', 'double'].forEach(type => {
        it(`includes ${type}s`, function () {
            const method = `test_${type}`;
            expect(Regress[method](42)).toBe(42);
            expect(Regress[method](-42)).toBe(-42);

            if (['float', 'double'].includes(type))
                expect(Number.isNaN(Regress[method](undefined))).toBeTruthy();
            else
                expect(Regress[method](undefined)).toBe(0);
        });
    });

    ['ushort', 'uint', 'ulong', 'size'].forEach(type => {
        it(`includes ${type}s`, function () {
            const method = `test_${type}`;
            expect(Regress[method](42)).toBe(42);
            expect(Regress[method](undefined)).toBe(0);
        });
    });

    describe('No implicit conversion to unsigned', function () {
        ['uint8', 'uint16', 'uint32', 'uint64', 'uint', 'size'].forEach(type => {
            it(`for ${type}`, function () {
                expect(() => Regress[`test_${type}`](-42)).toThrow();
            });
        });
    });

    it('includes wide characters', function () {
        expect(Regress.test_unichar('c')).toBe('c');
        expect(Regress.test_unichar('')).toBe('');
        expect(Regress.test_unichar('\u2665')).toBe('\u2665');
    });

    it('includes time_t', function () {
        const now = Math.floor(new Date().getTime() / 1000);
        const bounced = Math.floor(Regress.test_timet(now));
        expect(bounced).toEqual(now);
    });

    it('includes GTypes', function () {
        expect(Regress.test_gtype(GObject.TYPE_NONE)).toBe(GObject.TYPE_NONE);
        expect(Regress.test_gtype(String)).toBe(GObject.TYPE_STRING);
        expect(Regress.test_gtype(GObject.Object)).toBe(GObject.Object.$gtype);
    });

    it('closures', function () {
        const callback = jasmine.createSpy('callback').and.returnValue(42);
        expect(Regress.test_closure(callback)).toEqual(42);
        expect(callback).toHaveBeenCalledWith();
    });

    it('closures with one argument', function () {
        const callback = jasmine.createSpy('callback')
            .and.callFake(someValue => someValue);
        expect(Regress.test_closure_one_arg(callback, 42)).toEqual(42);
        expect(callback).toHaveBeenCalledWith(42);
    });

    it('closure with GLib.Variant argument', function () {
        const callback = jasmine.createSpy('callback')
            .and.returnValue(new GLib.Variant('s', 'hello'));
        const variant = new GLib.Variant('i', 42);
        expect(Regress.test_closure_variant(callback, variant).deepUnpack())
            .toEqual('hello');
        expect(callback).toHaveBeenCalledWith(variant);
    });

    describe('GValue marshalling', function () {
        it('integer in', function () {
            expect(Regress.test_int_value_arg(42)).toEqual(42);
        });

        it('integer out', function () {
            expect(Regress.test_value_return(42)).toEqual(42);
        });
    });

    // See testCairo.js for the following tests, since that will be skipped if
    // we are building without Cairo support:
    // Regress.test_cairo_context_full_return()
    // Regress.test_cairo_context_none_in()
    // Regress.test_cairo_surface_none_return()
    // Regress.test_cairo_surface_full_return()
    // Regress.test_cairo_surface_none_in()
    // Regress.test_cairo_surface_full_out()
    // Regress.TestObj.emit_sig_with_foreign_struct()

    it('integer GLib.Variant', function () {
        const ivar = Regress.test_gvariant_i();
        expect(ivar.get_type_string()).toEqual('i');
        expect(ivar.unpack()).toEqual(1);
    });

    it('string GLib.Variant', function () {
        const svar = Regress.test_gvariant_s();
        expect(String.fromCharCode(svar.classify())).toEqual('s');
        expect(svar.unpack()).toEqual('one');
    });

    it('dictionary GLib.Variant', function () {
        const asvvar = Regress.test_gvariant_asv();
        expect(asvvar.recursiveUnpack()).toEqual({name: 'foo', timeout: 10});
    });

    it('variant GLib.Variant', function () {
        const vvar = Regress.test_gvariant_v();
        expect(vvar.unpack()).toEqual(jasmine.any(GLib.Variant));
        expect(vvar.recursiveUnpack()).toEqual('contents');
    });

    it('string array GLib.Variant', function () {
        const asvar = Regress.test_gvariant_as();
        expect(asvar.deepUnpack()).toEqual(['one', 'two', 'three']);
    });

    describe('UTF-8 strings', function () {
        const CONST_STR = 'const ♥ utf8';
        const NONCONST_STR = 'nonconst ♥ utf8';

        it('as return types', function () {
            expect(Regress.test_utf8_const_return()).toEqual(CONST_STR);
            expect(Regress.test_utf8_nonconst_return()).toEqual(NONCONST_STR);
        });

        it('as in parameters', function () {
            Regress.test_utf8_const_in(CONST_STR);
        });

        it('as out parameters', function () {
            expect(Regress.test_utf8_out()).toEqual(NONCONST_STR);
        });

        xit('as in-out parameters', function () {
            expect(Regress.test_utf8_inout(CONST_STR)).toEqual(NONCONST_STR);
        }).pend('https://gitlab.gnome.org/GNOME/gobject-introspection/issues/192');
    });

    it('return values in filename encoding', function () {
        const filenames = Regress.test_filename_return();
        expect(filenames).toEqual(['\u00e5\u00e4\u00f6', '/etc/fstab']);
    });

    describe('Various configurations of arguments', function () {
        it('in after out', function () {
            const str = 'hello';
            const len = Regress.test_int_out_utf8(str);
            expect(len).toEqual(str.length);
        });

        it('multiple number args', function () {
            const [times2, times3] = Regress.test_multi_double_args(2.5);
            expect(times2).toEqual(5);
            expect(times3).toEqual(7.5);
        });

        it('multiple string out parameters', function () {
            const [first, second] = Regress.test_utf8_out_out();
            expect(first).toEqual('first');
            expect(second).toEqual('second');
        });

        it('strings as return value and output parameter', function () {
            const [first, second] = Regress.test_utf8_out_nonconst_return();
            expect(first).toEqual('first');
            expect(second).toEqual('second');
        });

        it('nullable string in parameter', function () {
            expect(() => Regress.test_utf8_null_in(null)).not.toThrow();
        });

        it('nullable string out parameter', function () {
            expect(Regress.test_utf8_null_out()).toBeNull();
        });
    });

    ['int', 'gint8', 'gint16', 'gint32', 'gint64'].forEach(inttype => {
        it(`arrays of ${inttype} in`, function () {
            expect(Regress[`test_array_${inttype}_in`]([1, 2, 3, 4])).toEqual(10);
        });
    });

    it('implicit conversions from strings to int arrays', function () {
        expect(Regress.test_array_gint8_in('\x01\x02\x03\x04')).toEqual(10);
        expect(Regress.test_array_gint16_in('\x01\x02\x03\x04')).toEqual(10);
        expect(Regress.test_array_gint16_in('\u0100\u0200\u0300\u0400')).toEqual(2560);
    });

    it('out arrays of integers', function () {
        expect(Regress.test_array_int_out()).toEqual([0, 1, 2, 3, 4]);
    });

    xit('inout arrays of integers', function () {
        expect(Regress.test_array_int_inout([0, 1, 2, 3, 4])).toEqual([2, 3, 4, 5]);
    }).pend('https://gitlab.gnome.org/GNOME/gobject-introspection/issues/192');

    describe('String arrays', function () {
        it('marshalling in', function () {
            expect(Regress.test_strv_in(['1', '2', '3'])).toBeTruthy();
            expect(Regress.test_strv_in(['4', '5', '6'])).toBeFalsy();
            // Ensure that primitives throw without SEGFAULT
            expect(() => Regress.test_strv_in(1)).toThrow();
            expect(() => Regress.test_strv_in('')).toThrow();
            expect(() => Regress.test_strv_in(false)).toThrow();
            // Second two are deliberately not strings
            expect(() => Regress.test_strv_in(['1', 2, 3])).toThrow();
        });

        it('marshalling out', function () {
            expect(Regress.test_strv_out())
                .toEqual(['thanks', 'for', 'all', 'the', 'fish']);
        });

        it('marshalling return value with container transfer', function () {
            expect(Regress.test_strv_out_container()).toEqual(['1', '2', '3']);
        });

        it('marshalling out parameter with container transfer', function () {
            expect(Regress.test_strv_outarg()).toEqual(['1', '2', '3']);
        });
    });

    it('GType arrays', function () {
        expect(Regress.test_array_gtype_in([Gio.SimpleAction, Gio.Icon, GObject.TYPE_BOXED]))
            .toEqual('[GSimpleAction,GIcon,GBoxed,]');
        expect(() => Regress.test_array_gtype_in(42)).toThrow();
        expect(() => Regress.test_array_gtype_in([undefined])).toThrow();
        // 80 is G_TYPE_OBJECT, but we don't want it to work
        expect(() => Regress.test_array_gtype_in([80])).toThrow();
    });

    describe('Fixed arrays of integers', function () {
        it('marshals as an in parameter', function () {
            expect(Regress.test_array_fixed_size_int_in([1, 2, 3, 4])).toEqual(10);
        });

        it('marshals as an out parameter', function () {
            expect(Regress.test_array_fixed_size_int_out()).toEqual([0, 1, 2, 3, 4]);
        });

        it('marshals as a return value', function () {
            expect(Regress.test_array_fixed_size_int_return()).toEqual([0, 1, 2, 3, 4]);
        });
    });

    it('integer array with static length', function () {
        const arr = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10];
        expect(() => Regress.test_array_static_in_int(arr)).not.toThrow();
    });

    it("string array that's const in C", function () {
        expect(Regress.test_strv_out_c()).toEqual(['thanks', 'for', 'all', 'the', 'fish']);
    });

    describe('arrays of integers with length parameter', function () {
        it('marshals as a return value with transfer full', function () {
            expect(Regress.test_array_int_full_out()).toEqual([0, 1, 2, 3, 4]);
        });

        it('marshals as a return value with transfer none', function () {
            expect(Regress.test_array_int_none_out()).toEqual([1, 2, 3, 4, 5]);
        });

        it('marshalls as a nullable in parameter', function () {
            expect(() => Regress.test_array_int_null_in(null)).not.toThrow();
        });

        it('marshals as a nullable return value', function () {
            expect(Regress.test_array_int_null_out()).toEqual([]);
        });
    });

    ['glist', 'gslist'].forEach(list => {
        describe(`${list} types`, function () {
            const STR_LIST = ['1', '2', '3'];

            it('return with transfer-none', function () {
                expect(Regress[`test_${list}_nothing_return`]()).toEqual(STR_LIST);
                expect(Regress[`test_${list}_nothing_return2`]()).toEqual(STR_LIST);
            });

            it('return with transfer-container', function () {
                expect(Regress[`test_${list}_container_return`]()).toEqual(STR_LIST);
            });

            it('return with transfer-full', function () {
                expect(Regress[`test_${list}_everything_return`]()).toEqual(STR_LIST);
            });

            it('in with transfer-none', function () {
                Regress[`test_${list}_nothing_in`](STR_LIST);
                Regress[`test_${list}_nothing_in2`](STR_LIST);
            });

            it('nullable in', function () {
                expect(() => Regress[`test_${list}_null_in`]([])).not.toThrow();
            });

            it('nullable out', function () {
                expect(Regress[`test_${list}_null_out`]()).toEqual([]);
            });

            xit('in with transfer-container', function () {
                Regress[`test_${list}_container_in`](STR_LIST);
            }).pend('Function not added to gobject-introspection test suite yet');
        });
    });

    it('GList of GTypes in with transfer container', function () {
        expect(() =>
            Regress.test_glist_gtype_container_in([Regress.TestObj, Regress.TestSubObj]))
            .not.toThrow();
    });

    describe('GHash type', function () {
        const EXPECTED_HASH = {baz: 'bat', foo: 'bar', qux: 'quux'};

        it('null GHash out', function () {
            expect(Regress.test_ghash_null_return()).toBeNull();
        });

        it('out GHash', function () {
            expect(Regress.test_ghash_nothing_return()).toEqual(EXPECTED_HASH);
            expect(Regress.test_ghash_nothing_return2()).toEqual(EXPECTED_HASH);
        });

        const GVALUE_HASH_TABLE = {
            'integer': 12,
            'boolean': true,
            'string': 'some text',
            'strings': ['first', 'second', 'third'],
            'flags': Regress.TestFlags.FLAG1 | Regress.TestFlags.FLAG3,
            'enum': Regress.TestEnum.VALUE2,
        };

        it('with GValue value type out', function () {
            expect(Regress.test_ghash_gvalue_return()).toEqual(GVALUE_HASH_TABLE);
        });

        xit('with GValue value type in', function () {
            expect(() => Regress.test_ghash_gvalue_in(GVALUE_HASH_TABLE)).not.toThrow();
        }).pend('https://gitlab.gnome.org/GNOME/gjs/issues/272');

        it('marshals as a return value with transfer container', function () {
            expect(Regress.test_ghash_container_return()).toEqual(EXPECTED_HASH);
        });

        it('marshals as a return value with transfer full', function () {
            expect(Regress.test_ghash_everything_return()).toEqual(EXPECTED_HASH);
        });

        it('null GHash in', function () {
            Regress.test_ghash_null_in(null);
        });

        it('null GHashTable out', function () {
            expect(Regress.test_ghash_null_out()).toBeNull();
        });

        it('in GHash', function () {
            Regress.test_ghash_nothing_in(EXPECTED_HASH);
            Regress.test_ghash_nothing_in2(EXPECTED_HASH);
        });

        it('nested GHash', function () {
            const EXPECTED_NESTED_HASH = {wibble: EXPECTED_HASH};

            expect(Regress.test_ghash_nested_everything_return())
                .toEqual(EXPECTED_NESTED_HASH);
            expect(Regress.test_ghash_nested_everything_return2())
                .toEqual(EXPECTED_NESTED_HASH);
        });
    });

    describe('GArray', function () {
        it('marshals as a return value with transfer container', function () {
            expect(Regress.test_garray_container_return()).toEqual(['regress']);
        });

        it('marshals as a return value with transfer full', function () {
            expect(Regress.test_garray_full_return()).toEqual(['regress']);
        });
    });

    it('enum parameter', function () {
        expect(Regress.test_enum_param(Regress.TestEnum.VALUE1)).toEqual('value1');
        expect(Regress.test_enum_param(Regress.TestEnum.VALUE3)).toEqual('value3');
    });

    it('unsigned enum parameter', function () {
        expect(Regress.test_unsigned_enum_param(Regress.TestEnumUnsigned.VALUE1))
            .toEqual('value1');
        expect(Regress.test_unsigned_enum_param(Regress.TestEnumUnsigned.VALUE2))
            .toEqual('value2');
    });

    it('flags parameter', function () {
        expect(Regress.global_get_flags_out()).toEqual(Regress.TestFlags.FLAG1 |
            Regress.TestFlags.FLAG3);
    });

    describe('Simple introspected struct', function () {
        let struct;
        beforeEach(function () {
            struct = new Regress.TestStructA();
            struct.some_int = 42;
            struct.some_int8 = 43;
            struct.some_double = 42.5;
            struct.some_enum = Regress.TestEnum.VALUE3;
        });

        it('sets fields correctly', function () {
            expect(struct.some_int).toEqual(42);
            expect(struct.some_int8).toEqual(43);
            expect(struct.some_double).toEqual(42.5);
            expect(struct.some_enum).toEqual(Regress.TestEnum.VALUE3);
        });

        it('can clone', function () {
            const b = struct.clone();
            expect(b.some_int).toEqual(42);
            expect(b.some_int8).toEqual(43);
            expect(b.some_double).toEqual(42.5);
            expect(b.some_enum).toEqual(Regress.TestEnum.VALUE3);
        });

        it('can be modified by a method', function () {
            const c = Regress.TestStructA.parse('foobar');
            expect(c.some_int).toEqual(23);
        });

        describe('constructors', function () {
            beforeEach(function () {
                struct = new Regress.TestStructA({
                    some_int: 42,
                    some_int8: 43,
                    some_double: 42.5,
                    some_enum: Regress.TestEnum.VALUE3,
                });
            });

            it('"copies" an object from a hash of field values', function () {
                expect(struct.some_int).toEqual(42);
                expect(struct.some_int8).toEqual(43);
                expect(struct.some_double).toEqual(42.5);
                expect(struct.some_enum).toEqual(Regress.TestEnum.VALUE3);
            });

            it('catches bad field names', function () {
                expect(() => new Regress.TestStructA({junk: 42})).toThrow();
            });

            it('copies an object from another object of the same type', function () {
                const copy = new Regress.TestStructA(struct);
                expect(copy.some_int).toEqual(42);
                expect(copy.some_int8).toEqual(43);
                expect(copy.some_double).toEqual(42.5);
                expect(copy.some_enum).toEqual(Regress.TestEnum.VALUE3);
            });
        });
    });

    it('out arrays of structs', function () {
        const array = Regress.test_array_struct_out();
        const ints = array.map(struct => struct.some_int);
        expect(ints).toEqual([22, 33, 44]);
    });

    describe('Introspected nested struct', function () {
        let struct;
        beforeEach(function () {
            struct = new Regress.TestStructB();
            struct.some_int8 = 43;
            struct.nested_a.some_int8 = 66;
        });

        it('sets fields correctly', function () {
            expect(struct.some_int8).toEqual(43);
            expect(struct.nested_a.some_int8).toEqual(66);
        });

        it('can clone', function () {
            const b = struct.clone();
            expect(b.some_int8).toEqual(43);
            expect(b.nested_a.some_int8).toEqual(66);
        });
    });

    // Bare GObject pointer, not currently supported (and possibly not ever)
    xdescribe('Struct with non-basic member', function () {
        it('sets fields correctly', function () {
            const struct = new Regress.TestStructC();
            struct.another_int = 43;
            struct.obj = new GObject.Object();

            expect(struct.another_int).toEqual(43);
            expect(struct.obj).toEqual(jasmine.any(GObject.Object));
        });
    });

    describe('Struct with annotated fields', function () {
        xit('sets fields correctly', function () {
            const testObjList = [new Regress.TestObj(), new Regress.TestObj()];
            const testStructList = [new Regress.TestStructA(), new Regress.TestStructA()];
            const struct = new Regress.TestStructD();
            struct.array1 = testStructList;
            struct.array2 = testObjList;
            struct.field = testObjList[0];
            struct.list = testObjList;
            struct.garray = testObjList;

            expect(struct.array1).toEqual(testStructList);
            expect(struct.array2).toEqual(testObjList);
            expect(struct.field).toEqual(testObjList[0]);
            expect(struct.list).toEqual(testObjList);
            expect(struct.garray).toEqual(testObjList);
        }).pend('https://gitlab.gnome.org/GNOME/gjs/issues/83');
    });

    describe('Struct with array of anonymous unions', function () {
        xit('sets fields correctly', function () {
            const struct = new Regress.TestStructE();
            struct.some_type = GObject.Object.$gtype;
            for (let ix = 0; ix < 1; ix++) {
                struct.some_union[ix].v_int = 42;
                struct.some_union[ix].v_uint = 43;
                struct.some_union[ix].v_long = 44;
                struct.some_union[ix].v_ulong = 45;
                struct.some_union[ix].v_int64 = 46;
                struct.some_union[ix].v_uint64 = 47;
                struct.some_union[ix].v_float = 48.5;
                struct.some_union[ix].v_double = 49.5;
                struct.some_union[ix].v_pointer = null;
            }

            expect(struct.some_type).toEqual(GObject.Object.$gtype);
            for (let ix = 0; ix < 1; ix++) {
                expect(struct.some_union[ix].v_int).toEqual(42);
                expect(struct.some_union[ix].v_uint).toEqual(43);
                expect(struct.some_union[ix].v_long).toEqual(44);
                expect(struct.some_union[ix].v_ulong).toEqual(45);
                expect(struct.some_union[ix].v_int64).toEqual(46);
                expect(struct.some_union[ix].v_uint64).toEqual(47);
                expect(struct.some_union[ix].v_float).toEqual(48.5);
                expect(struct.some_union[ix].v_double).toEqual(49.5);
                expect(struct.some_union[ix].v_pointer).toBeNull();
            }
        }).pend('https://gitlab.gnome.org/GNOME/gjs/issues/273');
    });

    // Bare int pointers, not currently supported (and possibly not ever)
    xdescribe('Struct with const/volatile members', function () {
        it('sets fields correctly', function () {
            const struct = new Regress.TestStructF();
            struct.ref_count = 1;
            struct.data1 = null;
            struct.data2 = null;
            struct.data3 = null;
            struct.data4 = null;
            struct.data5 = null;
            struct.data6 = null;
            struct.data7 = 42;

            expect(struct.ref_count).toEqual(1);
            expect(struct.data1).toBeNull();
            expect(struct.data2).toBeNull();
            expect(struct.data3).toBeNull();
            expect(struct.data4).toBeNull();
            expect(struct.data5).toBeNull();
            expect(struct.data6).toBeNull();
            expect(struct.data7).toEqual(42);
        });
    });

    describe('Introspected simple boxed struct', function () {
        let struct;
        beforeEach(function () {
            struct = new Regress.TestSimpleBoxedA();
            struct.some_int = 42;
            struct.some_int8 = 43;
            struct.some_double = 42.5;
            struct.some_enum = Regress.TestEnum.VALUE3;
        });

        it('sets fields correctly', function () {
            expect(struct.some_int).toEqual(42);
            expect(struct.some_int8).toEqual(43);
            expect(struct.some_double).toEqual(42.5);
            expect(struct.some_enum).toEqual(Regress.TestEnum.VALUE3);
        });

        it('can be passed to a method', function () {
            const other = new Regress.TestSimpleBoxedA({
                some_int: 42,
                some_int8: 43,
                some_double: 42.5,
            });
            expect(other.equals(struct)).toBeTruthy();
        });

        it('can be returned from a method', function () {
            const other = Regress.TestSimpleBoxedA.const_return();
            expect(other.some_int).toEqual(5);
            expect(other.some_int8).toEqual(6);
            expect(other.some_double).toEqual(7);
        });

        describe('constructors', function () {
            beforeEach(function () {
                struct = new Regress.TestSimpleBoxedA({
                    some_int: 42,
                    some_int8: 43,
                    some_double: 42.5,
                    some_enum: Regress.TestEnum.VALUE3,
                });
            });

            it('"copies" an object from a hash of field values', function () {
                expect(struct.some_int).toEqual(42);
                expect(struct.some_int8).toEqual(43);
                expect(struct.some_double).toEqual(42.5);
                expect(struct.some_enum).toEqual(Regress.TestEnum.VALUE3);
            });

            it('catches bad field names', function () {
                expect(() => new Regress.TestSimpleBoxedA({junk: 42})).toThrow();
            });

            it('copies an object from another object of the same type', function () {
                const copy = new Regress.TestSimpleBoxedA(struct);
                expect(copy).toEqual(jasmine.any(Regress.TestSimpleBoxedA));
                expect(copy.some_int).toEqual(42);
                expect(copy.some_int8).toEqual(43);
                expect(copy.some_double).toEqual(42.5);
                expect(copy.some_enum).toEqual(Regress.TestEnum.VALUE3);
            });
        });
    });

    describe('Introspected boxed nested struct', function () {
        let struct;
        beforeEach(function () {
            struct = new Regress.TestSimpleBoxedB();
            struct.some_int8 = 42;
            struct.nested_a.some_int = 43;
        });

        it('reads fields and nested fields', function () {
            expect(struct.some_int8).toEqual(42);
            expect(struct.nested_a.some_int).toEqual(43);
        });

        it('assigns nested struct field from an instance', function () {
            struct.nested_a = new Regress.TestSimpleBoxedA({some_int: 53});
            expect(struct.nested_a.some_int).toEqual(53);
        });

        it('assigns nested struct field directly from a hash of field values', function () {
            struct.nested_a = {some_int: 63};
            expect(struct.nested_a.some_int).toEqual(63);
        });

        describe('constructors', function () {
            it('constructs with a nested hash of field values', function () {
                const simple2 = new Regress.TestSimpleBoxedB({
                    some_int8: 42,
                    nested_a: {
                        some_int: 43,
                        some_int8: 44,
                        some_double: 43.5,
                    },
                });
                expect(simple2.some_int8).toEqual(42);
                expect(simple2.nested_a.some_int).toEqual(43);
                expect(simple2.nested_a.some_int8).toEqual(44);
                expect(simple2.nested_a.some_double).toEqual(43.5);
            });

            it('copies an object from another object of the same type', function () {
                const copy = new Regress.TestSimpleBoxedB(struct);
                expect(copy.some_int8).toEqual(42);
                expect(copy.nested_a.some_int).toEqual(43);
            });
        });
    });

    describe('Introspected boxed types', function () {
        describe('Opaque', function () {
            it('constructs from a default constructor', function () {
                const boxed = new Regress.TestBoxed();
                expect(boxed).toEqual(jasmine.any(Regress.TestBoxed));
            });

            it('sets fields correctly', function () {
                const boxed = new Regress.TestBoxed();
                boxed.some_int8 = 42;
                expect(boxed.some_int8).toEqual(42);
            });

            it('constructs from a static constructor', function () {
                const boxed = Regress.TestBoxed.new_alternative_constructor1(42);
                expect(boxed.some_int8).toEqual(42);
            });

            it('constructs from a static constructor with different args', function () {
                const boxed = Regress.TestBoxed.new_alternative_constructor2(40, 2);
                expect(boxed.some_int8).toEqual(42);
            });

            it('constructs from a static constructor with differently typed args', function () {
                const boxed = Regress.TestBoxed.new_alternative_constructor3('42');
                expect(boxed.some_int8).toEqual(42);
            });

            it('constructs from a another object of the same type', function () {
                const boxed = new Regress.TestBoxed({some_int8: 42});
                const copy = new Regress.TestBoxed(boxed);
                expect(copy.some_int8).toEqual(42);
                expect(copy.equals(boxed)).toBeTruthy();
            });

            it('ensures methods are named correctly', function () {
                const boxed = new Regress.TestBoxed();
                expect(boxed.s_not_a_method).not.toBeDefined();
                expect(boxed.not_a_method).not.toBeDefined();
                expect(() => Regress.test_boxeds_not_a_method(boxed)).not.toThrow();
            });

            it('ensures static methods are named correctly', function () {
                expect(Regress.TestBoxed.s_not_a_static).not.toBeDefined();
                expect(Regress.TestBoxed.not_a_static).not.toBeDefined();
                expect(Regress.test_boxeds_not_a_static).not.toThrow();
            });
        });

        describe('Simple', function () {
            it('sets fields correctly', function () {
                const boxed = new Regress.TestBoxedB();
                boxed.some_int8 = 7;
                boxed.some_long = 5;
                expect(boxed.some_int8).toEqual(7);
                expect(boxed.some_long).toEqual(5);
            });

            it('constructs from a static constructor', function () {
                const boxed = Regress.TestBoxedB.new(7, 5);
                expect(boxed.some_int8).toEqual(7);
                expect(boxed.some_long).toEqual(5);
            });

            it('constructs from another object of the same type', function () {
                const boxed = Regress.TestBoxedB.new(7, 5);
                const copy = new Regress.TestBoxedB(boxed);
                expect(copy.some_int8).toEqual(7);
                expect(copy.some_long).toEqual(5);
            });

            // Regress.TestBoxedB has a constructor that takes multiple arguments,
            // but since it is directly allocatable, we keep the old style of
            // passing an hash of fields. The two real world structs that have this
            // behavior are Clutter.Color and Clutter.ActorBox.
            it('constructs in backwards compatibility mode', function () {
                const boxed = new Regress.TestBoxedB({some_int8: 7, some_long: 5});
                expect(boxed.some_int8).toEqual(7);
                expect(boxed.some_long).toEqual(5);
            });
        });

        describe('Refcounted', function () {
            it('constructs from a default constructor', function () {
                const boxed = new Regress.TestBoxedC();
                expect(boxed.another_thing).toEqual(42);
            });

            it('constructs from another object of the same type', function () {
                const boxed = new Regress.TestBoxedC({another_thing: 43});
                const copy = new Regress.TestBoxedC(boxed);
                expect(copy.another_thing).toEqual(43);
            });
        });

        describe('Private', function () {
            it('constructs using a custom constructor', function () {
                const boxed = new Regress.TestBoxedD('abcd', 8);
                expect(boxed.get_magic()).toEqual(12);
            });

            it('constructs from another object of the same type', function () {
                const boxed = new Regress.TestBoxedD('abcd', 8);
                const copy = new Regress.TestBoxedD(boxed);
                expect(copy.get_magic()).toEqual(12);
            });

            it('does not construct with a default constructor', function () {
                expect(() => new Regress.TestBoxedD()).toThrow();
            });
        });
    });

    describe('wrong type for GBoxed', function () {
        let simpleBoxed, wrongObject, wrongBoxed;
        beforeEach(function () {
            simpleBoxed = new Regress.TestSimpleBoxedA();
            wrongObject = new Gio.SimpleAction();
            wrongBoxed = new GLib.KeyFile();
        });

        // simpleBoxed.equals expects a Everything.TestSimpleBoxedA
        it('function does not accept a GObject of the wrong type', function () {
            expect(() => simpleBoxed.equals(wrongObject)).toThrow();
        });

        it('function does not accept a GBoxed of the wrong type', function () {
            expect(() => simpleBoxed.equals(wrongBoxed)).toThrow();
        });

        it('function does accept a GBoxed of the correct type', function () {
            expect(simpleBoxed.equals(simpleBoxed)).toBeTruthy();
        });

        it('method cannot be called on a GObject', function () {
            expect(() => Regress.TestSimpleBoxedA.prototype.copy.call(wrongObject))
                .toThrow();
        });

        it('method cannot be called on a GBoxed of the wrong type', function () {
            expect(() => Regress.TestSimpleBoxedA.prototype.copy.call(wrongBoxed))
                .toThrow();
        });

        it('method can be called on correct GBoxed type', function () {
            expect(() => Regress.TestSimpleBoxedA.prototype.copy.call(simpleBoxed))
                .not.toThrow();
        });
    });

    describe('Introspected GObject', function () {
        let o;
        beforeEach(function () {
            o = new Regress.TestObj({
                // These properties have backing public fields with different names
                int: 42,
                float: 3.1416,
                double: 2.71828,
            });
        });

        it('can access fields with simple types', function () {
            // Compare the values gotten through the GObject property getters to the
            // values of the backing fields
            expect(o.some_int8).toEqual(o.int);
            expect(o.some_float).toEqual(o.float);
            expect(o.some_double).toEqual(o.double);
        });

        it('cannot access fields with complex types (GI limitation)', function () {
            expect(() => o.parent_instance).toThrow();
            expect(() => o.function_ptr).toThrow();
        });

        it('throws when setting a read-only field', function () {
            expect(() => (o.some_int8 = 41)).toThrow();
        });

        it('has normal Object methods', function () {
            o.ownprop = 'foo';
            // eslint-disable-next-line no-prototype-builtins
            expect(o.hasOwnProperty('ownprop')).toBeTruthy();
        });

        it('sets write-only properties', function () {
            expect(o.int).not.toEqual(0);
            o.write_only = true;
            expect(o.int).toEqual(0);
        });

        it('gives undefined for write-only properties', function () {
            expect(o.write_only).not.toBeDefined();
        });

        it('constructs from constructors annotated with (constructor)', function () {
            expect(Regress.TestObj.new(o)).toEqual(jasmine.any(Regress.TestObj));
            expect(Regress.TestObj.constructor()).toEqual(jasmine.any(Regress.TestObj));
        });

        it('static methods', function () {
            const v = Regress.TestObj.new_from_file('/enoent');
            expect(v).toEqual(jasmine.any(Regress.TestObj));
        });

        describe('GProperty', function () {
            let t, boxed, hashTable, hashTable2, list2, string, gtype, byteArray;
            const list = null;
            const int = 42;
            const double = Math.PI;
            const double2 = Math.E;

            beforeEach(function () {
                boxed = new Regress.TestBoxed({some_int8: 127});
                hashTable = {a: 1, b: 2};
                hashTable2 = {c: 3, d: 4};
                list2 = ['j', 'k', 'l'];
                string = 'cauliflower';
                gtype = GObject.Object.$gtype;
                byteArray = Uint8Array.from('abcd', c => c.charCodeAt(0));
                t = new Regress.TestObj({
                    boxed,
                    // hashTable,
                    list,
                    // pptrarray: list,
                    // hashTableOld: hashTable,
                    listOld: list,
                    int,
                    float: double,
                    double,
                    string,
                    gtype,
                    // byteArray,
                });
            });

            it('Boxed type', function () {
                expect(t.boxed.some_int8).toBe(127);
                const boxed2 = new Regress.TestBoxed({some_int8: 31});
                t.boxed = boxed2;
                expect(t.boxed.some_int8).toBe(31);
            });

            xit('Hash table', function () {
                expect(t.hashTable).toBe(hashTable);
                t.hashTable = hashTable2;
                expect(t.hashTable).toBe(hashTable2);
            }).pend('https://gitlab.gnome.org/GNOME/gjs/-/issues/83');

            xit('List', function () {
                expect(t.list).toBe(list);
                t.list = list2;
                expect(t.list).toBe(list2);
            }).pend('https://gitlab.gnome.org/GNOME/gjs/-/issues/83');

            xit('Pointer array', function () {
                expect(t.pptrarray).toBe(list);
                t.pptrarray = list2;
                expect(t.pptrarray).toBe(list2);
            }).pend('https://gitlab.gnome.org/GNOME/gjs/-/issues/83');

            xit('Hash table with old-style annotation', function () {
                expect(t.hashTableOld).toBe(hashTable);
                t.hashTableOld = hashTable2;
                expect(t.hashTableOld).toBe(hashTable2);
            }).pend('https://gitlab.gnome.org/GNOME/gjs/-/issues/83');

            xit('List with old-style annotation', function () {
                expect(t.listOld).toBe(list);
                t.listOld = list2;
                expect(t.listOld).toBe(list2);
            }).pend('https://gitlab.gnome.org/GNOME/gjs/-/issues/83');

            it('Integer', function () {
                expect(t.int).toBe(int);
                t.int = 35;
                expect(t.int).toBe(35);
            });

            it('Float', function () {
                expect(t.float).toBeCloseTo(double);
                t.float = double2;
                expect(t.float).toBeCloseTo(double2);
            });

            it('Double', function () {
                expect(t.double).toBeCloseTo(double);
                t.double = double2;
                expect(t.double).toBeCloseTo(double2);
            });

            it('String', function () {
                expect(t.string).toBe(string);
                t.string = 'string2';
                expect(t.string).toBe('string2');
            });

            xit('GType object', function () {
                expect(t.gtype).toBe(gtype);
                const gtype2 = GObject.InitiallyUnowned.$gtype;
                t.gtype = gtype2;
                expect(t.gtype).toBe(gtype2);
            }).pend('https://gitlab.gnome.org/GNOME/gjs/-/issues/83');

            xit('Byte array', function () {
                expect(t.byteArray).toBe(byteArray);
                const byteArray2 = Uint8Array.from('efgh', c => c.charCodeAt(0));
                t.byteArray = byteArray2;
                expect(t.byteArray).toBe(byteArray2);
            }).pend('https://gitlab.gnome.org/GNOME/gjs/-/issues/276');
        });

        describe('Object-valued GProperty', function () {
            let o1, t1, t2;
            beforeEach(function () {
                o1 = new GObject.Object();
                t1 = new Regress.TestObj({bare: o1});
                t2 = new Regress.TestSubObj();
                t2.bare = o1;
            });

            it('marshals correctly in the getter', function () {
                expect(t1.bare).toBe(o1);
            });

            it('marshals correctly when inherited', function () {
                expect(t2.bare).toBe(o1);
            });

            it('marshals into setter function', function () {
                const o2 = new GObject.Object();
                t2.set_bare(o2);
                expect(t2.bare).toBe(o2);
            });

            it('marshals null', function () {
                t2.unset_bare();
                expect(t2.bare).toBeNull();
            });
        });

        describe('Signal connection', function () {
            it('calls correct handlers with correct arguments', function () {
                const handler = jasmine.createSpy('handler');
                const handlerId = o.connect('test', handler);
                handler.and.callFake(() => o.disconnect(handlerId));

                o.emit('test');
                expect(handler).toHaveBeenCalledTimes(1);
                expect(handler).toHaveBeenCalledWith(o);

                handler.calls.reset();
                o.emit('test');
                expect(handler).not.toHaveBeenCalled();
            });

            it('throws errors for invalid signals', function () {
                expect(() => o.connect('invalid-signal', () => {})).toThrow();
                expect(() => o.emit('invalid-signal')).toThrow();
            });

            it('signal handler with static scope arg gets arg passed by reference', function () {
                const b = new Regress.TestSimpleBoxedA({
                    some_int: 42,
                    some_int8: 43,
                    some_double: 42.5,
                    some_enum: Regress.TestEnum.VALUE3,
                });
                o.connect('test-with-static-scope-arg', (signalObject, signalArg) => {
                    signalArg.some_int = 44;
                });
                o.emit('test-with-static-scope-arg', b);
                expect(b.some_int).toEqual(44);
            });

            it('signal with object gets correct arguments', function (done) {
                o.connect('sig-with-obj', (self, objectParam) => {
                    expect(objectParam.int).toEqual(3);
                    done();
                });
                o.emit_sig_with_obj();
            });

            // See testCairo.js for a test of
            // Regress.TestObj::sig-with-foreign-struct.

            xit('signal with int64 gets correct value', function (done) {
                o.connect('sig-with-int64-prop', (self, number) => {
                    expect(number).toEqual(GLib.MAXINT64);
                    done();
                    return GLib.MAXINT64;
                });
                o.emit_sig_with_int64();
            }).pend('https://gitlab.gnome.org/GNOME/gjs/issues/271');

            xit('signal with uint64 gets correct value', function (done) {
                o.connect('sig-with-uint64-prop', (self, number) => {
                    expect(number).toEqual(GLib.MAXUINT64);
                    done();
                    return GLib.MAXUINT64;
                });
                o.emit_sig_with_uint64();
            }).pend('https://gitlab.gnome.org/GNOME/gjs/issues/271');

            it('signal with array len parameter is not passed correct array and no length arg', function (done) {
                o.connect('sig-with-array-len-prop', (signalObj, signalArray, shouldBeUndefined) => {
                    expect(shouldBeUndefined).not.toBeDefined();
                    expect(signalArray).toEqual([0, 1, 2, 3, 4]);
                    done();
                });
                o.emit_sig_with_array_len_prop();
            });

            xit('can pass parameter to signal with array len parameter via emit', function (done) {
                o.connect('sig-with-array-len-prop', (signalObj, signalArray) => {
                    expect(signalArray).toEqual([0, 1, 2, 3, 4]);
                    done();
                });
                o.emit('sig-with-array-len-prop', [0, 1, 2, 3, 4]);
            }).pend('Not yet implemented');

            xit('can pass null to signal with array len parameter', function () {
                const handler = jasmine.createSpy('handler');
                o.connect('sig-with-array-len-prop', handler);
                o.emit('sig-with-array-len-prop', null);
                expect(handler).toHaveBeenCalledWith([jasmine.any(Object), null]);
            }).pend('Not yet implemented');

            xit('signal with int in-out parameter', function () {
                const handler = jasmine.createSpy('handler').and.callFake(() => 43);
                o.connect('sig-with-inout-int', handler);
                o.emit_sig_with_inout_int();
                expect(handler.toHaveBeenCalledWith([jasmine.any(Object), 42]));
            }).pend('Not yet implemented');

            it('GError signal with GError set', function (done) {
                o.connect('sig-with-gerror', (obj, e) => {
                    expect(e).toEqual(jasmine.any(Gio.IOErrorEnum));
                    expect(e.domain).toEqual(Gio.io_error_quark());
                    expect(e.code).toEqual(Gio.IOErrorEnum.FAILED);
                    done();
                });
                o.emit_sig_with_error();
            });

            it('GError signal with no GError set', function (done) {
                o.connect('sig-with-gerror', (obj, e) => {
                    expect(e).toBeNull();
                    done();
                });
                o.emit_sig_with_null_error();
            });
        });

        it('can call an instance method', function () {
            expect(o.instance_method()).toEqual(-1);
        });

        it('can call a transfer-full instance method', function () {
            expect(() => o.instance_method_full()).not.toThrow();
        });

        it('can call a static method', function () {
            expect(Regress.TestObj.static_method(5)).toEqual(5);
        });

        it('can call a method annotated with (method)', function () {
            expect(() => o.forced_method()).not.toThrow();
        });

        describe('Object torture signature', function () {
            it('0', function () {
                const [y, z, q] = o.torture_signature_0(42, 'foo', 7);
                expect(Math.floor(y)).toEqual(42);
                expect(z).toEqual(84);
                expect(q).toEqual(10);
            });

            it('1 fail', function () {
                expect(() => o.torture_signature_1(42, 'foo', 7)).toThrow();
            });

            it('1 success', function () {
                const [, y, z, q] = o.torture_signature_1(11, 'barbaz', 8);
                expect(Math.floor(y)).toEqual(11);
                expect(z).toEqual(22);
                expect(q).toEqual(14);
            });
        });

        describe('Introspected function length', function () {
            it('skips over instance parameters of methods', function () {
                expect(o.set_bare.length).toEqual(1);
            });

            it('skips over out and GError parameters', function () {
                expect(o.torture_signature_1.length).toEqual(3);
            });

            it('does not skip over inout parameters', function () {
                expect(o.skip_return_val.length).toEqual(5);
            });

            xit('skips over return value annotated with skip', function () {
                const [b, d, sum] = o.skip_return_val(1, 2, 3, 4, 5);
                expect(b).toEqual(2);
                expect(d).toEqual(4);
                expect(sum).toEqual(54);

                const retval = o.skip_return_val_no_out(1);
                expect(retval).not.toBeDefined();
            }).pend('https://gitlab.gnome.org/GNOME/gjs/issues/59');

            xit('skips over parameters annotated with skip', function () {
                expect(o.skip_param.length).toEqual(4);

                const [success, b, d, sum] = o.skip_param(1, 2, 3, 4);
                expect(success).toBeTruthy();
                expect(b).toEqual(2);
                expect(d).toEqual(3);
                expect(sum).toEqual(43);
            }).pend('https://gitlab.gnome.org/GNOME/gjs/issues/59');

            xit('skips over out parameters annotated with skip', function () {
                const [success, d, sum] = o.skip_out_param(1, 2, 3, 4, 5);
                expect(success).toBeTruthy();
                expect(d).toEqual(4);
                expect(sum).toEqual(54);
            }).pend('https://gitlab.gnome.org/GNOME/gjs/issues/59');

            xit('skips over inout parameters annotated with skip', function () {
                expect(o.skip_inout_param.length).toEqual(4);

                const [success, b, sum] = o.skip_inout_param(1, 2, 3, 4);
                expect(success).toBeTruthy();
                expect(b).toEqual(2);
                expect(sum).toEqual(43);
            }).pend('https://gitlab.gnome.org/GNOME/gjs/issues/59');

            it('gives number of arguments for static methods', function () {
                expect(Regress.TestObj.new_from_file.length).toEqual(1);
            });

            it('skips over destroy-notify and user-data parameters', function () {
                expect(Regress.TestObj.new_callback.length).toEqual(1);
            });
        });

        it('virtual function', function () {
            expect(o.do_matrix('meaningless string')).toEqual(42);
        });

        describe('wrong type for GObject', function () {
            let wrongObject, wrongBoxed, subclassObject;
            beforeEach(function () {
                wrongObject = new Gio.SimpleAction();
                wrongBoxed = new GLib.KeyFile();
                subclassObject = new Regress.TestSubObj();
            });

            // Regress.func_obj_null_in expects a Regress.TestObj
            it('function does not accept a GObject of the wrong type', function () {
                expect(() => Regress.func_obj_null_in(wrongObject)).toThrow();
            });

            it('function does not accept a GBoxed instead of GObject', function () {
                expect(() => Regress.func_obj_null_in(wrongBoxed)).toThrow();
            });

            it('function does not accept returned GObject of the wrong type', function () {
                const wrongReturnedObject = Gio.File.new_for_path('/');
                expect(() => Regress.func_obj_null_in(wrongReturnedObject)).toThrow();
            });

            it('function accepts GObject of subclass of expected type', function () {
                expect(() => Regress.func_obj_null_in(subclassObject)).not.toThrow();
            });

            it('method cannot be called on a GObject of the wrong type', function () {
                expect(() => Regress.TestObj.prototype.instance_method.call(wrongObject))
                    .toThrow();
            });

            it('method cannot be called on a GBoxed', function () {
                expect(() => Regress.TestObj.prototype.instance_method.call(wrongBoxed))
                    .toThrow();
            });

            it('method can be called on a GObject of subclass of expected type', function () {
                expect(() => Regress.TestObj.prototype.instance_method.call(subclassObject))
                    .not.toThrow();
            });
        });

        it('marshals a null object in', function () {
            expect(() => Regress.func_obj_null_in(null)).not.toThrow();
            expect(() => Regress.func_obj_nullable_in(null)).not.toThrow();
        });

        it('marshals a null object out', function () {
            expect(Regress.TestObj.null_out()).toBeNull();
        });

        it('marshals a gpointer with a type annotation in', function () {
            const o2 = new GObject.Object();
            expect(() => o.not_nullable_typed_gpointer_in(o2)).not.toThrow();
        });

        it('marshals a gpointer with an element-type annotation in', function () {
            expect(() => o.not_nullable_element_typed_gpointer_in([1, 2])).not.toThrow();
        });

        // This test is not meant to be normative; a GObject behaving like this is
        // doing something unsupported. However, we have been handling this so far
        // in a certain way, and we don't want to break user code because of badly
        // behaved libraries. This test ensures that any change to the behaviour
        // must be intentional.
        it('resolves properties when they are shadowed by methods', function () {
            expect(o.name_conflict).toEqual(42);
            expect(o.name_conflict).not.toEqual(jasmine.any(Function));
        });
    });

    it('marshals a fixed-size array of objects out', function () {
        expect(Regress.test_array_fixed_out_objects()).toEqual([
            jasmine.any(Regress.TestObj),
            jasmine.any(Regress.TestObj),
        ]);
    });

    describe('Inherited GObject', function () {
        let subobj;
        beforeEach(function () {
            subobj = new Regress.TestSubObj({
                int: 42,
                float: Math.PI,
                double: Math.E,
                boolean: true,
            });
        });

        it('can read fields from a parent class', function () {
            // see "can access fields with simple types" above
            expect(subobj.some_int8).toEqual(subobj.int);
            expect(subobj.some_float).toEqual(subobj.float);
            expect(subobj.some_double).toEqual(subobj.double);
        });

        it('can be constructed from a static constructor', function () {
            expect(Regress.TestSubObj.new).not.toThrow();
        });

        it('can call an instance method that overrides the parent class', function () {
            expect(subobj.instance_method()).toEqual(0);
        });

        it('can have its own properties', function () {
            expect(subobj.boolean).toBeTruthy();
            subobj.boolean = false;
            expect(subobj.boolean).toBeFalsy();
        });
    });

    describe('Overridden properties on interfaces', function () {
        it('set and get properly', function () {
            const o = new Regress.TestSubObj();
            o.number = 4;
            expect(o.number).toEqual(4);
        });

        it('default properly', function () {
            const o = new Regress.TestSubObj();
            expect(o.number).toBeDefined();
            expect(o.number).toEqual(0);
        });

        it('construct properly', function () {
            const o = new Regress.TestSubObj({number: 4});
            expect(o.number).toEqual(4);
        });
    });

    describe('Fundamental type', function () {
        it('constructs a subtype of a fundamental type', function () {
            expect(() => new Regress.TestFundamentalSubObject('plop')).not.toThrow();
        });

        it('constructs a subtype of a hidden (no introspection data) fundamental type', function () {
            expect(() => Regress.test_create_fundamental_hidden_class_instance()).not.toThrow();
        });
    });

    it('callbacks', function () {
        const callback = jasmine.createSpy('callback').and.returnValue(42);
        expect(Regress.test_callback(callback)).toEqual(42);
    });

    it('null / undefined callback', function () {
        expect(Regress.test_callback(null)).toEqual(0);
        expect(() => Regress.test_callback(undefined)).toThrow();
    });

    it('callback called more than once', function () {
        const callback = jasmine.createSpy('callback').and.returnValue(21);
        expect(Regress.test_multi_callback(callback)).toEqual(42);
        expect(callback).toHaveBeenCalledTimes(2);
    });

    it('null callback called more than once', function () {
        expect(Regress.test_multi_callback(null)).toEqual(0);
    });

    it('array callbacks', function () {
        const callback = jasmine.createSpy('callback').and.returnValue(7);
        expect(Regress.test_array_callback(callback)).toEqual(14);
        expect(callback).toHaveBeenCalledWith([-1, 0, 1, 2], ['one', 'two', 'three']);
    });

    it('null array callback', function () {
        expect(() => Regress.test_array_callback(null)).toThrow();
    });

    xit('callback with inout array', function () {
        const callback = jasmine.createSpy('callback').and.callFake(arr => arr.slice(1));
        expect(Regress.test_array_inout_callback(callback)).toEqual(3);
        expect(callback).toHaveBeenCalledWith([-2, -1, 0, 1, 2], [-1, 0, 1, 2]);
    });  // assertion failed, "Use gjs_value_from_explicit_array() for arrays with length param""

    ['simple', 'noptr'].forEach(type => {
        it(`${type} callback`, function () {
            const callback = jasmine.createSpy('callback');
            Regress[`test_${type}_callback`](callback);
            expect(callback).toHaveBeenCalled();
        });

        it('null simple callback', function () {
            expect(() => Regress[`test_${type}_callback`](null)).not.toThrow();
        });
    });

    it('callback with user data', function () {
        const callback = jasmine.createSpy('callback').and.returnValue(7);
        expect(Regress.test_callback_user_data(callback)).toEqual(7);
        expect(callback).toHaveBeenCalled();
    });

    it('callback with transfer-full return value', function () {
        const callback = jasmine.createSpy('callback')
            .and.returnValue(Regress.TestObj.new_from_file('/enoent'));
        Regress.test_callback_return_full(callback);
        expect(callback).toHaveBeenCalled();
    });

    it('callback with destroy-notify', function () {
        const callback1 = jasmine.createSpy('callback').and.returnValue(42);
        const callback2 = jasmine.createSpy('callback').and.returnValue(58);
        expect(Regress.test_callback_destroy_notify(callback1)).toEqual(42);
        expect(callback1).toHaveBeenCalledTimes(1);
        expect(Regress.test_callback_destroy_notify(callback2)).toEqual(58);
        expect(callback2).toHaveBeenCalledTimes(1);
        expect(Regress.test_callback_thaw_notifications()).toEqual(100);
        expect(callback1).toHaveBeenCalledTimes(2);
        expect(callback2).toHaveBeenCalledTimes(2);
    });

    xit('callback with destroy-notify and no user data', function () {
        const callback1 = jasmine.createSpy('callback').and.returnValue(42);
        const callback2 = jasmine.createSpy('callback').and.returnValue(58);
        expect(Regress.test_callback_destroy_notify_no_user_data(callback1)).toEqual(42);
        expect(callback1).toHaveBeenCalledTimes(1);
        expect(Regress.test_callback_destroy_notify_no_user_data(callback2)).toEqual(58);
        expect(callback2).toHaveBeenCalledTimes(1);
        expect(Regress.test_callback_thaw_notifications()).toEqual(100);
        expect(callback1).toHaveBeenCalledTimes(2);
        expect(callback2).toHaveBeenCalledTimes(2);
    }).pend('Callback with destroy-notify and no user data not currently supported');

    // If this is ever supported, then replace it with the above test.
    it('callback with destroy-notify and no user data throws error', function () {
        // should throw when called, not when the function object is created
        expect(() => Regress.test_callback_destroy_notify_no_user_data).not.toThrow();
        expect(() => Regress.test_callback_destroy_notify_no_user_data(() => {}))
            .toThrowError(/no user data/);
    });

    it('async callback', function () {
        Regress.test_callback_async(() => 44);
        expect(Regress.test_callback_thaw_async()).toEqual(44);
    });

    it('Gio.AsyncReadyCallback', function (done) {
        Regress.test_async_ready_callback((obj, res) => {
            expect(obj).toBeNull();
            expect(res).toEqual(jasmine.any(Gio.SimpleAsyncResult));
            done();
        });
    });

    it('instance method taking a callback', function () {
        const o = new Regress.TestObj();
        const callback = jasmine.createSpy('callback');
        o.instance_method_callback(callback);
        expect(callback).toHaveBeenCalled();
    });

    it('static method taking a callback', function () {
        const callback = jasmine.createSpy('callback');
        Regress.TestObj.static_method_callback(callback);
        expect(callback).toHaveBeenCalled();
    });

    it('constructor taking a callback', function () {
        const callback = jasmine.createSpy('callback').and.returnValue(42);
        void Regress.TestObj.new_callback(callback);
        expect(callback).toHaveBeenCalled();
        expect(Regress.test_callback_thaw_notifications()).toEqual(42);
        expect(callback).toHaveBeenCalledTimes(2);
    });

    it('hash table passed to callback', function () {
        const hashtable = {
            a: 1,
            b: 2,
            c: 3,
        };
        const callback = jasmine.createSpy('callback');
        Regress.test_hash_table_callback(hashtable, callback);
        expect(callback).toHaveBeenCalledWith(hashtable);
    });

    it('GError callback', function (done) {
        Regress.test_gerror_callback(e => {
            expect(e).toEqual(jasmine.any(Gio.IOErrorEnum));
            expect(e.domain).toEqual(Gio.io_error_quark());
            expect(e.code).toEqual(Gio.IOErrorEnum.NOT_SUPPORTED);
            done();
        });
    });

    it('null GError callback', function () {
        const callback = jasmine.createSpy('callback');
        Regress.test_null_gerror_callback(callback);
        expect(callback).toHaveBeenCalledWith(null);
    });

    it('owned GError callback', function (done) {
        Regress.test_owned_gerror_callback(e => {
            expect(e).toEqual(jasmine.any(Gio.IOErrorEnum));
            expect(e.domain).toEqual(Gio.io_error_quark());
            expect(e.code).toEqual(Gio.IOErrorEnum.PERMISSION_DENIED);
            done();
        });
    });

    describe('Introspected interface', function () {
        const Implementor = GObject.registerClass({
            Implements: [Regress.TestInterface],
            Properties: {
                number: GObject.ParamSpec.override('number', Regress.TestInterface),
            },
        }, class Implementor extends GObject.Object {
            get number() {
                return 5;
            }
        });

        it('correctly emits interface signals', function () {
            const obj = new Implementor();
            const handler = jasmine.createSpy('handler').and.callFake(() => {});
            obj.connect('interface-signal', handler);
            obj.emit_signal();
            expect(handler).toHaveBeenCalled();
        });
    });

    describe('GObject with nonstandard prefix', function () {
        let o;
        beforeEach(function () {
            o = new Regress.TestWi8021x();
        });

        it('sets and gets properties', function () {
            expect(o.testbool).toBeTruthy();
            o.testbool = false;
            expect(o.testbool).toBeFalsy();
        });

        it('constructs via a static constructor', function () {
            expect(Regress.TestWi8021x.new()).toEqual(jasmine.any(Regress.TestWi8021x));
        });

        it('calls methods', function () {
            expect(o.get_testbool()).toBeTruthy();
            o.set_testbool(false);
            expect(o.get_testbool()).toBeFalsy();
        });

        it('calls a static method', function () {
            expect(Regress.TestWi8021x.static_method(21)).toEqual(42);
        });
    });

    describe('GObject.InitiallyUnowned', function () {
        it('constructs', function () {
            expect(new Regress.TestFloating()).toEqual(jasmine.any(Regress.TestFloating));
        });

        it('constructs via a static constructor', function () {
            expect(Regress.TestFloating.new()).toEqual(jasmine.any(Regress.TestFloating));
        });
    });

    it('torture signature 0', function () {
        const [y, z, q] = Regress.test_torture_signature_0(42, 'foo', 7);
        expect(Math.floor(y)).toEqual(42);
        expect(z).toEqual(84);
        expect(q).toEqual(10);
    });

    it('torture signature 1 fail', function () {
        expect(() => Regress.test_torture_signature_1(42, 'foo', 7)).toThrow();
    });

    it('torture signature 1 success', function () {
        const [, y, z, q] = Regress.test_torture_signature_1(11, 'barbaz', 8);
        expect(Math.floor(y)).toEqual(11);
        expect(z).toEqual(22);
        expect(q).toEqual(14);
    });

    it('torture signature 2', function () {
        const [y, z, q] = Regress.test_torture_signature_2(42, () => 0, 'foo', 7);
        expect(Math.floor(y)).toEqual(42);
        expect(z).toEqual(84);
        expect(q).toEqual(10);
    });

    describe('GValue boxing and unboxing', function () {
        it('date in', function () {
            const date = Regress.test_date_in_gvalue();
            expect(date.get_year()).toEqual(1984);
            expect(date.get_month()).toEqual(GLib.DateMonth.DECEMBER);
            expect(date.get_day()).toEqual(5);
        });

        it('strv in', function () {
            expect(Regress.test_strv_in_gvalue()).toEqual(['one', 'two', 'three']);
        });

        it('correctly converts a NULL strv in a GValue to an empty array', function () {
            expect(Regress.test_null_strv_in_gvalue()).toEqual([]);
        });
    });

    it("code coverage for documentation tests that don't do anything", function () {
        expect(() => {
            Regress.test_multiline_doc_comments();
            Regress.test_nested_parameter(5);
            Regress.test_versioning();
        }).not.toThrow();
    });

    it('marshals an aliased type', function () {
        // GLib.PtrArray is not introspectable, so neither is an alias of it
        // Regress.introspectable_via_alias(new GLib.PtrArray());
        expect(Regress.aliased_caller_alloc()).toEqual(jasmine.any(Regress.TestBoxed));
    });

    it('deals with a fixed-size array in a struct', function () {
        const struct = new Regress.TestStructFixedArray();
        struct.frob();
        expect(struct.just_int).toEqual(7);
        expect(struct.array).toEqual([42, 43, 44, 45, 46, 47, 48, 49, 50, 51]);
    });

    it('marshals a fixed-size int array as a gpointer', function () {
        expect(() => Regress.has_parameter_named_attrs(0, Array(32).fill(42))).not.toThrow();
    });

    it('deals with a fixed-size and also zero-terminated array in a struct', function () {
        const x = new Regress.LikeXklConfigItem();
        x.set_name('foo');
        expect(x.name).toEqual([...'foo'].map(c => c.codePointAt()).concat(Array(29).fill(0)));
        x.set_name('*'.repeat(33));
        expect(x.name).toEqual(Array(31).fill('*'.codePointAt()).concat([0]));
    });

    it('marshals a transfer-floating GLib.Variant', function () {
        expect(Regress.get_variant().unpack()).toEqual(42);
    });

    describe('Flat array of structs', function () {
        it('out parameter with transfer none', function () {
            const expected = [111, 222, 333].map(some_int =>
                jasmine.objectContaining({some_int}));
            expect(Regress.test_array_struct_out_none()).toEqual(expected);
        });

        it('out parameter with transfer container', function () {
            const expected = [11, 13, 17, 19, 23].map(some_int =>
                jasmine.objectContaining({some_int}));
            expect(Regress.test_array_struct_out_container()).toEqual(expected);
        });

        it('out parameter with transfer full', function () {
            const expected = [2, 3, 5, 7].map(some_int =>
                jasmine.objectContaining({some_int}));
            expect(Regress.test_array_struct_out_full_fixed()).toEqual(expected);
        });

        xit('caller-allocated out parameter', function () {
            // With caller-allocated array in, there's no way to supply the
            // length. This happens in GLib.MainContext.query()
            expect(Regress.test_array_struct_out_caller_alloc()).toEqual([]);
        }).pend('Not supported');

        it('transfer-full in parameter', function () {
            const array = [201, 202].map(some_int =>
                new Regress.TestStructA({some_int}));
            expect(() => Regress.test_array_struct_in_full(array)).not.toThrow();
        });

        it('transfer-none in parameter', function () {
            const array = [301, 302, 303].map(some_int =>
                new Regress.TestStructA({some_int}));
            expect(() => Regress.test_array_struct_in_none(array)).not.toThrow();
        });
    });
});
