// application/javascript;version=1.8
// This used to be called "Everything"
const Everything = imports.gi.Regress;
const GLib = imports.gi.GLib;

if (!('assertEquals' in this)) { /* allow running this test standalone */
    imports.lang.copyPublicProperties(imports.jsUnit, this);
    gjstestRun = function() { return imports.jsUnit.gjstestRun(window); };
}

function testStruct() {
    let struct = new Everything.TestStructA();
    struct.some_int = 42;
    struct.some_int8 = 43;
    struct.some_double = 42.5;
    struct.some_enum = Everything.TestEnum.VALUE3;
    assertEquals(42, struct.some_int);
    assertEquals(43, struct.some_int8);
    assertEquals(42.5, struct.some_double);
    assertEquals(Everything.TestEnum.VALUE3, struct.some_enum);
    let b = struct.clone();
    assertEquals(42, b.some_int);
    assertEquals(43, b.some_int8);
    assertEquals(42.5, b.some_double);
    assertEquals(Everything.TestEnum.VALUE3, b.some_enum);

    struct = new Everything.TestStructB();
    struct.some_int8 = 43;
    struct.nested_a.some_int8 = 66;
    assertEquals(43, struct.some_int8);
    assertEquals(66, struct.nested_a.some_int8);
    b = struct.clone();
    assertEquals(43, b.some_int8);
    assertEquals(66, struct.nested_a.some_int8);
}

function testStructConstructor()
{
    // "Copy" an object from a hash of field values
    let struct = new Everything.TestStructA({ some_int: 42,
                                              some_int8: 43,
                                              some_double: 42.5,
                                              some_enum: Everything.TestEnum.VALUE3 });

    assertEquals(42, struct.some_int);
    assertEquals(43, struct.some_int8);
    assertEquals(42.5, struct.some_double);
    assertEquals(Everything.TestEnum.VALUE3, struct.some_enum);

    // Make sure we catch bad field names
    assertRaises(function() {
        let t = new Everything.TestStructA({ junk: 42 });
    });

    // Copy an object from another object of the same type, shortcuts to memcpy()
    let copy = new Everything.TestStructA(struct);

    assertEquals(42, copy.some_int);
    assertEquals(43, copy.some_int8);
    assertEquals(42.5, copy.some_double);
    assertEquals(Everything.TestEnum.VALUE3, copy.some_enum);
}

function testSimpleBoxed() {
    let simple_boxed = new Everything.TestSimpleBoxedA();
    simple_boxed.some_int = 42;
    simple_boxed.some_int8 = 43;
    simple_boxed.some_double = 42.5;
    simple_boxed.some_enum = Everything.TestEnum.VALUE3;
    assertEquals(42, simple_boxed.some_int);
    assertEquals(43, simple_boxed.some_int8);
    assertEquals(42.5, simple_boxed.some_double);
    assertEquals(Everything.TestEnum.VALUE3, simple_boxed.some_enum);
}

function testBoxedCopyConstructor()
{
    // "Copy" an object from a hash of field values
    let simple_boxed = new Everything.TestSimpleBoxedA({ some_int: 42,
                                                         some_int8: 43,
                                                         some_double: 42.5,
                                                         some_enum: Everything.TestEnum.VALUE3 });

    assertEquals(42, simple_boxed.some_int);
    assertEquals(43, simple_boxed.some_int8);
    assertEquals(42.5, simple_boxed.some_double);
    assertEquals(Everything.TestEnum.VALUE3, simple_boxed.some_enum);

    // Make sure we catch bad field names
    assertRaises(function() {
        let t = new Everything.TestSimpleBoxedA({ junk: 42 });
    });

    // Copy an object from another object of the same type, shortcuts to the boxed copy
    let copy = new Everything.TestSimpleBoxedA(simple_boxed);

    assertTrue(copy instanceof Everything.TestSimpleBoxedA);
    assertEquals(42, copy.some_int);
    assertEquals(43, copy.some_int8);
    assertEquals(42.5, copy.some_double);
    assertEquals(Everything.TestEnum.VALUE3, copy.some_enum);
 }

function testNestedSimpleBoxed() {
    let simple_boxed = new Everything.TestSimpleBoxedB();

    // Test reading fields and nested fields
    simple_boxed.some_int8 = 42;
    simple_boxed.nested_a.some_int = 43;
    assertEquals(42, simple_boxed.some_int8);
    assertEquals(43, simple_boxed.nested_a.some_int);

    // Try assigning the nested struct field from an instance
    simple_boxed.nested_a = new Everything.TestSimpleBoxedA({ some_int: 53 });
    assertEquals(53, simple_boxed.nested_a.some_int);

    // And directly from a hash of field values
    simple_boxed.nested_a = { some_int: 63 };
    assertEquals(63, simple_boxed.nested_a.some_int);

    // Try constructing with a nested hash of field values
    let simple2 = new Everything.TestSimpleBoxedB({
        some_int8: 42,
        nested_a: {
            some_int: 43,
            some_int8: 44,
            some_double: 43.5
        }
    });
    assertEquals(42, simple2.some_int8);
    assertEquals(43, simple2.nested_a.some_int);
    assertEquals(44, simple2.nested_a.some_int8);
    assertEquals(43.5, simple2.nested_a.some_double);
}

function testBoxed() {
    let boxed = new Everything.TestBoxed();
    boxed.some_int8 = 42;
    assertEquals(42, boxed.some_int8);
}

function testTestStructFixedArray() {
    let struct = new Everything.TestStructFixedArray();
    struct.frob();
    assertEquals(7, struct.just_int);
    assertEquals(42, struct.array[0]);
    assertEquals(43, struct.array[1]);
    assertEquals(51, struct.array[9]);
}

function testComplexConstructor() {
    let boxed = new Everything.TestBoxedD('abcd', 8);

    assertEquals(12, boxed.get_magic());
}

function testComplexConstructorBackwardCompatibility() {
    // RegressTestBoxedB has a constructor that takes multiple
    // arguments, but since it is directly allocatable, we keep
    // the old style of passing an hash of fields.
    // The two real world structs that have this behavior are
    // Clutter.Color and Clutter.ActorBox.
    let boxed = new Everything.TestBoxedB({ some_int8: 7, some_long: 5 });

    assertEquals(7, boxed.some_int8);
    assertEquals(5, boxed.some_long);
}

function testVariantConstructor() {
    let str_variant = new GLib.Variant('s', 'mystring');
    assertEquals('mystring', str_variant.get_string()[0]);
    assertEquals('mystring', str_variant.deep_unpack());

    let str_variant_old = GLib.Variant.new('s', 'mystring');
    assertTrue(str_variant.equal(str_variant_old));

    let struct_variant = new GLib.Variant('(sogvau)',
					  [ 'a string',
					    '/a/object/path',
					    'asig', //nature
					    new GLib.Variant('s', 'variant'),
					    [ 7, 3 ]
					  ]);
    assertEquals(5, struct_variant.n_children());

    let unpacked = struct_variant.deep_unpack();
    assertEquals('a string', unpacked[0]);
    assertEquals('/a/object/path', unpacked[1]);
    assertEquals('asig', unpacked[2]);
    assertTrue(unpacked[3] instanceof GLib.Variant);
    assertEquals('variant', unpacked[3].deep_unpack());
    assertTrue(unpacked[4] instanceof Array);
    assertEquals(2, unpacked[4].length);
}

gjstestRun();
