const Regress = imports.gi.Regress;
const WarnLib = imports.gi.WarnLib;

// We use Gio to have some objects that we know exist
const GLib = imports.gi.GLib;
const Gio = imports.gi.Gio;
const GObject = imports.gi.GObject;
const Lang = imports.lang;

describe('Life, the Universe and Everything', function () {
    it('includes booleans', function () {
        expect(Regress.test_boolean(false)).toBe(false);
        expect(Regress.test_boolean(true)).toBe(true);
    });

    [8, 16, 32, 64].forEach(bits => {
        it('includes ' + bits + '-bit integers', function () {
            let method = 'test_int' + bits;
            expect(Regress[method](42)).toBe(42);
            expect(Regress[method](-42)).toBe(-42);
        });

        it('includes unsigned ' + bits + '-bit integers', function () {
            let method = 'test_uint' + bits;
            expect(Regress[method](42)).toBe(42);
        });
    });

    ['short', 'int', 'long', 'ssize', 'float', 'double'].forEach(type => {
        it('includes ' + type + 's', function () {
            let method = 'test_' + type;
            expect(Regress[method](42)).toBe(42);
            expect(Regress[method](-42)).toBe(-42);
        });
    });

    ['ushort', 'uint', 'ulong', 'size'].forEach(type => {
        it('includes ' + type + 's', function () {
            let method = 'test_' + type;
            expect(Regress[method](42)).toBe(42);
        });
    });

    it('includes wide characters', function () {
        expect(Regress.test_unichar('c')).toBe('c');
        expect(Regress.test_unichar('')).toBe('');
        expect(Regress.test_unichar('\u2665')).toBe('\u2665');
    });

    it('includes time_t', function () {
        let now = Math.floor(new Date().getTime() / 1000);
        let bounced = Math.floor(Regress.test_timet(now));
        expect(bounced).toEqual(now);
    });

    describe('Limits', function () {
        const Limits = {
            '8': {
                MIN: -128,
                MAX: 127,
                UMAX: 255,
            },
            '16': {
                MIN: -32767 - 1,
                MAX: 32767,
                UMAX: 65535,
            },
            '32': {
                MIN: -2147483647 - 1,
                MAX: 2147483647,
                UMAX: 4294967295,
            },
            '64': {
                MIN: -9223372036854775807 - 1,
                MAX: 9223372036854775807,
                UMAX: 18446744073709551615,
            },
        };

        const skip = {
            'UMAX64': true,  // FAIL: expected 18446744073709552000, got 0
            'MAX64': true,   // FAIL: expected 9223372036854776000, got -9223372036854776000
        };

        function run_test(bytes, limit, method_stem) {
            if(skip[limit + bytes])
                pending("This test doesn't work");
            let val = Limits[bytes][limit];
            expect(Regress[method_stem + bytes](val)).toBe(val);
        }
        ['8', '16', '32', '64'].forEach(bytes => {
            it('marshals max value of unsigned ' + bytes + '-bit integers', function () {
                run_test(bytes, 'UMAX', 'test_uint');
            });

            it('marshals min value of signed ' + bytes + '-bit integers', function () {
                run_test(bytes, 'MIN', 'test_int');
            });

            it('marshals max value of signed ' + bytes + '-bit integers', function () {
                run_test(bytes, 'MAX', 'test_int');
            });
        });
    });

    describe('No implicit conversion to unsigned', function () {
        ['uint8', 'uint16', 'uint32', 'uint64', 'uint', 'size'].forEach(type => {
            it('for ' + type, function () {
                expect(() => Regress['test_' + type](-42)).toThrow();
            });
        });
    });

    it('throws when constructor called without new', function () {
        expect(() => Gio.AppLaunchContext())
            .toThrowError(/Constructor called as normal method/);
    });

    describe('String arrays', function () {
        it('marshalling in', function () {
            expect(Regress.test_strv_in(['1', '2', '3'])).toBeTruthy();
            // Second two are deliberately not strings
            expect(() => Regress.test_strv_in(['1', 2, 3])).toThrow();
        });

        it('marshalling out', function () {
            expect(Regress.test_strv_out())
                .toEqual(['thanks', 'for', 'all', 'the', 'fish']);
        });

        it('marshalling out with container transfer', function () {
            expect(Regress.test_strv_out_container()).toEqual(['1', '2', '3']);
        });
    });

    it('in after out', function () {
        const str = "hello";
        let len = Regress.test_int_out_utf8(str);
        expect(len).toEqual(str.length);
    });

    describe('UTF-8 strings', function () {
        const CONST_STR = "const \u2665 utf8";
        const NONCONST_STR = "nonconst \u2665 utf8";

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

        // FIXME: this is broken due to a change in gobject-introspection.
        xit('as in-out parameters', function () {
            expect(Regress.test_utf8_inout(CONST_STR)).toEqual(NONCONST_STR);
        }).pend('https://bugzilla.gnome.org/show_bug.cgi?id=736517');
    });

    it('return values in filename encoding', function () {
        let filenames = Regress.test_filename_return();
        expect(filenames).toEqual(['\u00e5\u00e4\u00f6', '/etc/fstab']);
    });

    it('static methods', function () {
        let v = Regress.TestObj.new_from_file("/enoent");
        expect(v instanceof Regress.TestObj).toBeTruthy();
    });

    it('closures', function () {
        let callback = jasmine.createSpy('callback').and.returnValue(42);
        expect(Regress.test_closure(callback)).toEqual(42);
        expect(callback).toHaveBeenCalledWith();
    });

    it('closures with one argument', function () {
        let callback = jasmine.createSpy('callback')
            .and.callFake(someValue => someValue);
        expect(Regress.test_closure_one_arg(callback, 42)).toEqual(42);
        expect(callback).toHaveBeenCalledWith(42);
    });

    it('callbacks', function () {
        let callback = jasmine.createSpy('callback').and.returnValue(42);
        expect(Regress.test_callback(callback)).toEqual(42);
    });

    it('null / undefined callback', function () {
        expect(Regress.test_callback(null)).toEqual(0);
        expect(() => Regress.test_callback(undefined)).toThrow();
    });

    it('array callbacks', function () {
        let callback = jasmine.createSpy('callback').and.returnValue(7);
        expect(Regress.test_array_callback(callback)).toEqual(14);
        expect(callback).toHaveBeenCalledWith([-1, 0, 1, 2], ["one", "two", "three"]);
    });

    it('null array callback', function () {
        expect(() => Regress.test_array_callback(null)).toThrow();
    });

    it('callback with transfer-full return value', function () {
        function callback() {
            return Regress.TestObj.new_from_file("/enoent");
        }
        Regress.test_callback_return_full(callback);
    });

    it('callback with destroy-notify', function () {
        let testObj = {
            test: function (data) { return data; },
        };
        spyOn(testObj, 'test').and.callThrough();
        expect(Regress.test_callback_destroy_notify(function () {
            return testObj.test(42);
        }.bind(testObj))).toEqual(42);
        expect(testObj.test).toHaveBeenCalledTimes(1);
        expect(Regress.test_callback_thaw_notifications()).toEqual(42);
    });

    it('async callback', function () {
        Regress.test_callback_async(() => 44);
        expect(Regress.test_callback_thaw_async()).toEqual(44);
    });

    it('method taking a GValue', function () {
        expect(Regress.test_int_value_arg(42)).toEqual(42);
    });

    it('method returning a GValue', function () {
        expect(Regress.test_value_return(42)).toEqual(42);
    });

    ['glist', 'gslist'].forEach(list => {
        describe(list + ' types', function () {
            const STR_LIST = ['1', '2', '3'];

            it('return with transfer-none', function () {
                expect(Regress['test_' + list + '_nothing_return']()).toEqual(STR_LIST);
                expect(Regress['test_' + list + '_nothing_return2']()).toEqual(STR_LIST);
            });

            it('return with transfer-container', function () {
                expect(Regress['test_' + list + '_container_return']()).toEqual(STR_LIST);
            });

            it('return with transfer-full', function () {
                expect(Regress['test_' + list + '_everything_return']()).toEqual(STR_LIST);
            });

            it('in with transfer-none', function () {
                Regress['test_' + list + '_nothing_in'](STR_LIST);
                Regress['test_' + list + '_nothing_in2'](STR_LIST);
            });

            xit('in with transfer-container', function () {
                Regress['test_' + list + '_container_in'](STR_LIST);
            }).pend('Not sure why this is skipped');
        });
    });

    ['int', 'gint8', 'gint16', 'gint32', 'gint64'].forEach(inttype => {
        it('arrays of ' + inttype + ' in', function () {
            expect(Regress['test_array_' + inttype + '_in']([1, 2, 3, 4])).toEqual(10);
        });
    });

    it('implicit conversions from strings to int arrays', function () {
        expect(Regress.test_array_gint8_in("\x01\x02\x03\x04")).toEqual(10);
        expect(Regress.test_array_gint16_in("\x01\x02\x03\x04")).toEqual(10);
        expect(Regress.test_array_gint16_in("\u0100\u0200\u0300\u0400")).toEqual(2560);
    });

    it('GType arrays', function () {
        expect(Regress.test_array_gtype_in([Gio.SimpleAction, Gio.Icon, GObject.TYPE_BOXED]))
            .toEqual('[GSimpleAction,GIcon,GBoxed,]');
        expect(() => Regress.test_array_gtype_in(42)).toThrow();
        expect(() => Regress.test_array_gtype_in([undefined])).toThrow();
        // 80 is G_TYPE_OBJECT, but we don't want it to work
        expect(() => Regress.test_array_gtype_in([80])).toThrow();
    });

    it('out arrays of integers', function () {
        expect(Regress.test_array_int_out()).toEqual([0, 1, 2, 3, 4]);

        let array = Regress.test_array_fixed_size_int_out();
        expect(array[0]).toEqual(0);
        expect(array[4]).toEqual(4);
        let array = Regress.test_array_fixed_size_int_return();
        expect(array[0]).toEqual(0);
        expect(array[4]).toEqual(4);

        expect(Regress.test_array_int_none_out()).toEqual([1, 2, 3, 4, 5]);

        expect(Regress.test_array_int_full_out()).toEqual([0, 1, 2, 3, 4]);

        expect(Regress.test_array_int_null_out()).toEqual([]);
    });

    it('null in-array', function () {
        Regress.test_array_int_null_in(null);
    });

    it('out arrays of structs', function () {
        let array = Regress.test_array_struct_out();
        let ints = array.map(struct => struct.some_int);
        expect(ints).toEqual([22, 33, 44]);
    });

    describe('GHash type', function () {;
        const EXPECTED_HASH = { baz: 'bat', foo: 'bar', qux: 'quux' };

        it('null GHash in', function () {
            Regress.test_ghash_null_in(null);
        });

        it('null GHash out', function () {
            expect(Regress.test_ghash_null_return()).toBeNull();
        });

        it('out GHash', function () {
            expect(Regress.test_ghash_nothing_return()).toEqual(EXPECTED_HASH);
            expect(Regress.test_ghash_nothing_return2()).toEqual(EXPECTED_HASH);
            expect(Regress.test_ghash_container_return()).toEqual(EXPECTED_HASH);
            expect(Regress.test_ghash_everything_return()).toEqual(EXPECTED_HASH);
        });

        it('in GHash', function () {
            Regress.test_ghash_nothing_in(EXPECTED_HASH);
            Regress.test_ghash_nothing_in2(EXPECTED_HASH);
        });

        it('nested GHash', function () {
            const EXPECTED_NESTED_HASH = { wibble: EXPECTED_HASH };

            expect(Regress.test_ghash_nested_everything_return())
                .toEqual(EXPECTED_NESTED_HASH);
            expect(Regress.test_ghash_nested_everything_return2())
                .toEqual(EXPECTED_NESTED_HASH);
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

    it('enum has a $gtype property', function () {
        expect(Regress.TestEnumUnsigned.$gtype).toBeDefined();
    });

    it('enum $gtype property is enumerable', function () {
        expect('$gtype' in Regress.TestEnumUnsigned).toBeTruthy();
    });

    it('Number converts error to quark', function () {
        expect(Regress.TestError.quark()).toEqual(Number(Regress.TestError));
    });

    it('converts enum to string', function () {
        expect(Regress.TestEnum.param(Regress.TestEnum.VALUE4)).toEqual('value4');
    });

    describe('Signal connection', function () {
        let o;
        beforeEach(function () {
            o = new Regress.TestObj();
        });

        it('calls correct handlers with correct arguments', function () {
            let handler = jasmine.createSpy('handler');
            let handlerId = o.connect('test', handler);
            handler.and.callFake(() => o.disconnect(handlerId));

            o.emit('test');
            expect(handler).toHaveBeenCalledTimes(1);
            expect(handler).toHaveBeenCalledWith(o);

            handler.calls.reset();
            o.emit('test');
            expect(handler).not.toHaveBeenCalled();
        });

        it('throws errors for invalid signals', function () {
            expect(() => o.connect('invalid-signal', o => {})).toThrow();
            expect(() => o.emit('invalid-signal')).toThrow();
        });

        it('signal handler with static scope arg gets arg passed by reference', function () {
            let b = new Regress.TestSimpleBoxedA({
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

        it('signal with array len parameter is not passed correct array and no length arg', function (done) {
            o.connect('sig-with-array-len-prop', (signalObj, signalArray, shouldBeUndefined) => {
                expect(shouldBeUndefined).not.toBeDefined();
                expect(signalArray).toEqual([0, 1, 2, 3, 4]);
                done();
            });
            o.emit_sig_with_array_len_prop();
        });

        xit('can pass parameter to signal with array len parameter via emit', function () {
            o.connect('sig-with-array-len-prop', (signalObj, signalArray) => {
                expect(signalArray).toEqual([0, 1, 2, 3, 4]);
                done();
            });
            o.emit('sig-with-array-len-prop', [0, 1, 2, 3, 4]);
        }).pend('Not yet implemented');

        xit('can pass null to signal with array len parameter', function () {
            let handler = jasmine.createSpy('handler');
            o.connect('sig-with-array-len-prop', handler);
            o.emit('sig-with-array-len-prop', null);
            expect(handler).toHaveBeenCalledWith([jasmine.any(Object), null]);
        }).pend('Not yet implemented');
    });

    it('torture signature 0', function () {
        let [y, z, q] = Regress.test_torture_signature_0(42, 'foo', 7);
        expect(Math.floor(y)).toEqual(42);
        expect(z).toEqual(84);
        expect(q).toEqual(10);
    });

    it('torture signature 1 fail', function () {
        expect(() => Regress.test_torture_signature_1(42, 'foo', 7)).toThrow();
    });

    it('torture signature 1 success', function () {
        let [, y, z, q] = Regress.test_torture_signature_1(11, 'barbaz', 8);
        expect(Math.floor(y)).toEqual(11);
        expect(z).toEqual(22);
        expect(q).toEqual(14);
    });

    it('torture signature 2', function () {
        let [y, z, q] = Regress.test_torture_signature_2(42, () => 0, 'foo', 7);
        expect(Math.floor(y)).toEqual(42);
        expect(z).toEqual(84);
        expect(q).toEqual(10);
    });

    describe('Object torture signature', function () {
        let o;
        beforeEach(function () {
            o = new Regress.TestObj();
        });

        it('0', function () {
            let [y, z, q] = o.torture_signature_0(42, 'foo', 7);
            expect(Math.floor(y)).toEqual(42);
            expect(z).toEqual(84);
            expect(q).toEqual(10);
        });

        it('1 fail', function () {
            expect(() => o.torture_signature_1(42, 'foo', 7)).toThrow();
        });

        it('1 success', function () {
            let [, y, z, q] = o.torture_signature_1(11, 'barbaz', 8);
            expect(Math.floor(y)).toEqual(11);
            expect(z).toEqual(22);
            expect(q).toEqual(14);
        });
    });

    it('strv in GValue', function () {
        expect(Regress.test_strv_in_gvalue()).toEqual(['one', 'two', 'three']);
    });

    // Cannot access the variant contents, for now
    it('integer GVariant', function () {
        let ivar = Regress.test_gvariant_i();
        expect(ivar.get_type_string()).toEqual('i');
        expect(ivar.equal(GLib.Variant.new_int32(1))).toBeTruthy();
    });

    it('string GVariant', function () {
        let svar = Regress.test_gvariant_s();
        expect(String.fromCharCode(svar.classify())).toEqual('s');
        expect(svar.get_string()[0]).toEqual('one');
    });

    it('a{sv} GVariant', function () {
        let asvvar = Regress.test_gvariant_asv();
        expect(asvvar.n_children()).toEqual(2);
    });

    it('as Variant', function () {
        let asvar = Regress.test_gvariant_as();
        expect(asvar.get_strv()).toEqual(['one', 'two', 'three']);
    });

    it('error enum names match error quarks', function () {
        expect(Number(Gio.IOErrorEnum)).toEqual(Gio.io_error_quark());
    });

    describe('thrown GError', function () {
        let err;
        beforeEach(function () {
            try {
                let file = Gio.file_new_for_path("\\/,.^!@&$_don't exist");
                file.read(null);
            } catch (x) {
                err = x;
            }
        });

        it('is an instance of error enum type', function () {
            expect(err instanceof Gio.IOErrorEnum).toBeTruthy();
        });

        it('matches error domain and code', function () {
            expect(err.matches(Gio.io_error_quark(), Gio.IOErrorEnum.NOT_FOUND))
                .toBeTruthy();
        });

        it('has properties for domain and code', function () {
            expect(err.domain).toEqual(Gio.io_error_quark());
            expect(err.code).toEqual(Gio.IOErrorEnum.NOT_FOUND);
        });
    });

    it('GError callback', function (done) {
        Regress.test_gerror_callback(e => {
            expect(e instanceof Gio.IOErrorEnum).toBeTruthy();
            expect(e.domain).toEqual(Gio.io_error_quark());
            expect(e.code).toEqual(Gio.IOErrorEnum.NOT_SUPPORTED);
            done();
        });
    });

    it('owned GError callback', function (done) {
        Regress.test_owned_gerror_callback(e => {
            expect(e instanceof Gio.IOErrorEnum).toBeTruthy();
            expect(e.domain).toEqual(Gio.io_error_quark());
            expect(e.code).toEqual(Gio.IOErrorEnum.PERMISSION_DENIED);
            done();
        });
    });

    // Calling matches() on an unpaired error used to JSUnit.assert:
    // https://bugzilla.gnome.org/show_bug.cgi?id=689482
    it('bug 689482', function () {
        try {
            WarnLib.throw_unpaired();
            fail();
        } catch (e) {
            expect(e.matches(Gio.IOErrorEnum, Gio.IOErrorEnum.NOT_FOUND)).toBeFalsy();
        }
    });

    describe('wrong type for GObject', function () {
        let wrongObject, wrongBoxed, subclassObject;
        beforeEach(function () {
            wrongObject = new Gio.SimpleAction();
            wrongBoxed = new GLib.KeyFile();
            subclassObject = new Regress.TestSubObj();
        });

        // Everything.func_obj_null_in expects a Everything.TestObj
        it('function does not accept a GObject of the wrong type', function () {
            expect(() => Regress.func_obj_null_in(wrongObject)).toThrow();
        });

        it('function does not accept a GBoxed instead of GObject', function () {
            expect(() => Regress.func_obj_null_in(wrongBoxed)).toThrow();
        });

        it('function does not accept returned GObject of the wrong type', function () {
            let wrongReturnedObject = Gio.File.new_for_path('/');
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
            expect(() => Regress.TestSimpleBoxedA.protoype.copy.call(wrongBoxed))
                .toThrow();
        });

        it('method can be called on correct GBoxed type', function () {
            expect(() => Regress.TestSimpleBoxedA.prototype.copy.call(simpleBoxed))
                .not.toThrow();
        });
    });
});
