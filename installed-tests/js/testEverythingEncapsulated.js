const Regress = imports.gi.Regress;

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
