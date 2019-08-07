const {GLib, GObject, Regress} = imports.gi;
const System = imports.system;

describe('Introspected structs', function () {
    let struct;

    describe('simple', function () {
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
            let b = struct.clone();
            expect(b.some_int).toEqual(42);
            expect(b.some_int8).toEqual(43);
            expect(b.some_double).toEqual(42.5);
            expect(b.some_enum).toEqual(Regress.TestEnum.VALUE3);
        });
    });

    describe('nested', function () {
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
            let b = struct.clone();
            expect(b.some_int8).toEqual(43);
            expect(b.nested_a.some_int8).toEqual(66);
        });
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
            expect(() => new Regress.TestStructA({ junk: 42 })).toThrow();
        });

        it('copies an object from another object of the same type', function () {
            let copy = new Regress.TestStructA(struct);
            expect(copy.some_int).toEqual(42);
            expect(copy.some_int8).toEqual(43);
            expect(copy.some_double).toEqual(42.5);
            expect(copy.some_enum).toEqual(Regress.TestEnum.VALUE3);
        });
    });

    it('containing fixed array', function () {
        let struct = new Regress.TestStructFixedArray();
        struct.frob();
        expect(struct.just_int).toEqual(7);
        expect(struct.array).toEqual([42, 43, 44, 45, 46, 47, 48, 49, 50, 51]);
    });
});

describe('Introspected boxed types', function () {
    let simple_boxed;

    it('sets fields correctly', function () {
        simple_boxed = new Regress.TestSimpleBoxedA();
        simple_boxed.some_int = 42;
        simple_boxed.some_int8 = 43;
        simple_boxed.some_double = 42.5;
        simple_boxed.some_enum = Regress.TestEnum.VALUE3;
        expect(simple_boxed.some_int).toEqual(42);
        expect(simple_boxed.some_int8).toEqual(43);
        expect(simple_boxed.some_double).toEqual(42.5);
        expect(simple_boxed.some_enum).toEqual(Regress.TestEnum.VALUE3);

        let boxed = new Regress.TestBoxed();
        boxed.some_int8 = 42;
        expect(boxed.some_int8).toEqual(42);
    });

    describe('copy constructors', function () {
        beforeEach(function () {
            simple_boxed = new Regress.TestSimpleBoxedA({
                some_int: 42,
                some_int8: 43,
                some_double: 42.5,
                some_enum: Regress.TestEnum.VALUE3,
            });
        });

        it('"copies" an object from a hash of field values', function () {
            expect(simple_boxed.some_int).toEqual(42);
            expect(simple_boxed.some_int8).toEqual(43);
            expect(simple_boxed.some_double).toEqual(42.5);
            expect(simple_boxed.some_enum).toEqual(Regress.TestEnum.VALUE3);
        });

        it('catches bad field names', function () {
            expect(() => new Regress.TestSimpleBoxedA({ junk: 42 })).toThrow();
        });

        it('copies an object from another object of the same type', function () {
            let copy = new Regress.TestSimpleBoxedA(simple_boxed);
            expect(copy instanceof Regress.TestSimpleBoxedA).toBeTruthy();
            expect(copy.some_int).toEqual(42);
            expect(copy.some_int8).toEqual(43);
            expect(copy.some_double).toEqual(42.5);
            expect(copy.some_enum).toEqual(Regress.TestEnum.VALUE3);
        });
    });

    describe('nested', function () {
        beforeEach(function () {
            simple_boxed = new Regress.TestSimpleBoxedB();
        });

        it('reads fields and nested fields', function () {
            simple_boxed.some_int8 = 42;
            simple_boxed.nested_a.some_int = 43;
            expect(simple_boxed.some_int8).toEqual(42);
            expect(simple_boxed.nested_a.some_int).toEqual(43);
        });

        it('assigns nested struct field from an instance', function () {
            simple_boxed.nested_a = new Regress.TestSimpleBoxedA({ some_int: 53 });
            expect(simple_boxed.nested_a.some_int).toEqual(53);
        });

        it('assigns nested struct field directly from a hash of field values', function () {
            simple_boxed.nested_a = { some_int: 63 };
            expect(simple_boxed.nested_a.some_int).toEqual(63);
        });
    });

    it('constructs with a nested hash of field values', function () {
        let simple2 = new Regress.TestSimpleBoxedB({
            some_int8: 42,
            nested_a: {
                some_int: 43,
                some_int8: 44,
                some_double: 43.5
            }
        });
        expect(simple2.some_int8).toEqual(42);
        expect(simple2.nested_a.some_int).toEqual(43);
        expect(simple2.nested_a.some_int8).toEqual(44);
        expect(simple2.nested_a.some_double).toEqual(43.5);
    });

    it('constructs using a custom constructor', function () {
        let boxed = new Regress.TestBoxedD('abcd', 8);
        expect(boxed.get_magic()).toEqual(12);
    });

    // RegressTestBoxedB has a constructor that takes multiple
    // arguments, but since it is directly allocatable, we keep
    // the old style of passing an hash of fields.
    // The two real world structs that have this behavior are
    // Clutter.Color and Clutter.ActorBox.
    it('constructs using a custom constructor in backwards compatibility mode', function () {
        let boxed = new Regress.TestBoxedB({ some_int8: 7, some_long: 5 });
        expect(boxed.some_int8).toEqual(7);
        expect(boxed.some_long).toEqual(5);
    });
});

describe('Introspected GObject', function () {
    let obj;
    beforeEach(function () {
        obj = new Regress.TestObj({
            // These properties have backing public fields with different names
            int: 42,
            float: 3.1416,
            double: 2.71828,
        });
    });

    it('can access fields with simple types', function () {
        // Compare the values gotten through the GObject property getters to the
        // values of the backing fields
        expect(obj.some_int8).toEqual(obj.int);
        expect(obj.some_float).toEqual(obj.float);
        expect(obj.some_double).toEqual(obj.double);
    });

    it('cannot access fields with complex types (GI limitation)', function () {
        expect(() => obj.parent_instance).toThrow();
        expect(() => obj.function_ptr).toThrow();
    });

    it('throws when setting a read-only field', function () {
        expect(() => obj.some_int8 = 41).toThrow();
    });

    it('has normal Object methods', function () {
        obj.ownprop = 'foo';
        expect(obj.hasOwnProperty('ownprop')).toBeTruthy();
    });

    // This test is not meant to be normative; a GObject behaving like this is
    // doing something unsupported. However, we have been handling this so far
    // in a certain way, and we don't want to break user code because of badly
    // behaved libraries. This test ensures that any change to the behaviour
    // must be intentional.
    it('resolves properties when they are shadowed by methods', function () {
        expect(obj.name_conflict).toEqual(42);
        expect(obj.name_conflict instanceof Function).toBeFalsy();
    });

    it('sets write-only properties', function () {
        expect(obj.int).not.toEqual(0);
        obj.write_only = true;
        expect(obj.int).toEqual(0);
    });

    it('gives undefined for write-only properties', function () {
        expect(obj.write_only).not.toBeDefined();
    });

    it('can read fields from a parent class', function () {
        let subobj = new Regress.TestSubObj({
            int: 42,
            float: 3.1416,
            double: 2.71828,
        });

        // see "can access fields with simple types" above
        expect(subobj.some_int8).toEqual(subobj.int);
        expect(subobj.some_float).toEqual(subobj.float);
        expect(subobj.some_double).toEqual(subobj.double);
    });
});

describe('Introspected function length', function () {
    let obj;
    beforeEach(function () {
        obj = new Regress.TestObj();
    });

    it('skips over instance parameters of methods', function () {
        expect(obj.set_bare.length).toEqual(1);
    });

    it('skips over out and GError parameters', function () {
        expect(obj.torture_signature_1.length).toEqual(3);
    });

    it('does not skip over inout parameters', function () {
        expect(obj.skip_return_val.length).toEqual(5);
    });

    xit('skips over parameters annotated with skip', function () {
        expect(obj.skip_param.length).toEqual(4);
    }).pend('Not implemented yet');

    it('gives number of arguments for static methods', function () {
        expect(Regress.TestObj.new_from_file.length).toEqual(1);
    });

    it('skips over destroy-notify and user-data parameters', function () {
        expect(Regress.TestObj.new_callback.length).toEqual(1);
    });
});

describe('Introspected interface', function () {
    const Implementor = GObject.registerClass({
        Implements: [Regress.TestInterface],
    }, class Implementor extends GObject.Object {});

    it('correctly emits interface signals', function () {
        let obj = new Implementor();
        let handler = jasmine.createSpy('handler').and.callFake(() => {});
        obj.connect('interface-signal', handler);
        obj.emit_signal();
        expect(handler).toHaveBeenCalled();
    });
});
