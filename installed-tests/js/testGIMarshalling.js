if (!('assertEquals' in this)) { /* allow running this test standalone */
    imports.lang.copyPublicProperties(imports.jsUnit, this);
    gjstestRun = function() { return imports.jsUnit.gjstestRun(this); };
}

function assertArrayEquals(expected, got) {
    assertEquals(expected.length, got.length);
    for (let i = 0; i < expected.length; i ++) {
        assertEquals(expected[i], got[i]);
    }
}

const GIMarshallingTests = imports.gi.GIMarshallingTests;

// We use Gio and GLib to have some objects that we know exist
const Gio = imports.gi.Gio;
const GLib = imports.gi.GLib;
const GObject = imports.gi.GObject;
const Lang = imports.lang;

function testCArray() {
    var array, sum;

    var result = GIMarshallingTests.init_function(null);
    assertEquals(result.length, 2);
    var success = result[0];
    var newArray = result[1];
    assertEquals(newArray.length, 0);

    array = GIMarshallingTests.array_zero_terminated_return();
    assertEquals("0", array[0]);
    assertEquals("1", array[1]);
    assertEquals("2", array[2]);
    assertEquals(3, array.length);

    array = GIMarshallingTests.array_zero_terminated_return_struct();
    assertEquals(3, array.length);
    assertEquals(42, array[0].long_);
    assertEquals(43, array[1].long_);
    assertEquals(44, array[2].long_);

    array = GIMarshallingTests.array_return();
    assertEquals(4, array.length);
    assertEquals(-1, array[0]);
    assertEquals(0, array[1]);
    assertEquals(1, array[2]);
    assertEquals(2, array[3]);

    [array, sum] = GIMarshallingTests.array_return_etc(9, 5);
    assertEquals(14, sum);
    assertEquals(4, array.length);
    assertEquals(9, array[0]);
    assertEquals(0, array[1]);
    assertEquals(1, array[2]);
    assertEquals(5, array[3]);

    array = GIMarshallingTests.array_out();
    assertEquals(4, array.length);
    assertEquals(-1, array[0]);
    assertEquals(0, array[1]);
    assertEquals(1, array[2]);
    assertEquals(2, array[3]);

    [array, sum] = GIMarshallingTests.array_out_etc(9, 5);
    assertEquals(14, sum);
    assertEquals(4, array.length);
    assertEquals(9, array[0]);
    assertEquals(0, array[1]);
    assertEquals(1, array[2]);
    assertEquals(5, array[3]);

    array = GIMarshallingTests.array_inout([-1, 0, 1, 2]);
    assertEquals(5, array.length);
    assertEquals(-2, array[0]);
    assertEquals(-1, array[1]);
    assertEquals(0, array[2]);
    assertEquals(1, array[3]);
    assertEquals(2, array[4]);

    [array, sum] = GIMarshallingTests.array_inout_etc(9, [-1, 0, 1, 2], 5);
    assertEquals(14, sum);
    assertEquals(5, array.length);
    assertEquals(9, array[0]);
    assertEquals(-1, array[1]);
    assertEquals(0, array[2]);
    assertEquals(1, array[3]);
    assertEquals(5, array[4]);

    GIMarshallingTests.array_string_in(["foo", "bar"]);

    array = [];
    for (var i = 0; i < 3; i++) {
	array[i] = new GIMarshallingTests.BoxedStruct();
	array[i].long_ = i + 1;
    }

    GIMarshallingTests.array_struct_in(array);

    // Run twice to ensure that copies are correctly made for (transfer full)
    GIMarshallingTests.array_struct_take_in(array);
    GIMarshallingTests.array_struct_take_in(array);

    GIMarshallingTests.array_uint8_in ("abcd");
    GIMarshallingTests.array_enum_in([GIMarshallingTests.Enum.VALUE1,
				      GIMarshallingTests.Enum.VALUE2,
				      GIMarshallingTests.Enum.VALUE3]);

    array = [-1, 0, 1, 2];
    GIMarshallingTests.array_in(array);
    GIMarshallingTests.array_in_len_before(array);
    GIMarshallingTests.array_in_len_zero_terminated(array);
    GIMarshallingTests.array_in_guint64_len(array);
    GIMarshallingTests.array_in_guint8_len(array);
}

function testGArray() {
    var array;
    array = GIMarshallingTests.garray_int_none_return();
    assertEquals(-1, array[0]);
    assertEquals(0, array[1]);
    assertEquals(1, array[2]);
    assertEquals(2, array[3]);
    array = GIMarshallingTests.garray_utf8_none_return()
    assertEquals("0", array[0]);
    assertEquals("1", array[1]);
    assertEquals("2", array[2]);
    array = GIMarshallingTests.garray_utf8_container_return()
    assertEquals("0", array[0]);
    assertEquals("1", array[1]);
    assertEquals("2", array[2]);
    array = GIMarshallingTests.garray_utf8_full_return()
    assertEquals("0", array[0]);
    assertEquals("1", array[1]);
    assertEquals("2", array[2]);

    GIMarshallingTests.garray_int_none_in([-1, 0, 1, 2]);
    GIMarshallingTests.garray_utf8_none_in(["0", "1", "2"]);

    array = GIMarshallingTests.garray_utf8_none_out();
    assertEquals("0", array[0]);
    assertEquals("1", array[1]);
    assertEquals("2", array[2]);
    array = GIMarshallingTests.garray_utf8_container_out();
    assertEquals("0", array[0]);
    assertEquals("1", array[1]);
    assertEquals("2", array[2]);
    array = GIMarshallingTests.garray_utf8_full_out();
    assertEquals("0", array[0]);
    assertEquals("1", array[1]);
    assertEquals("2", array[2]);
}

function testByteArray() {
    var i = 0;
    var refByteArray = new imports.byteArray.ByteArray();
    refByteArray[i++] = 0;
    refByteArray[i++] = 49;
    refByteArray[i++] = 0xFF;
    refByteArray[i++] = 51;
    var byteArray = GIMarshallingTests.bytearray_full_return();
    assertEquals(refByteArray.length, byteArray.length);
    for (i = 0; i < refByteArray.length; i++)
	assertEquals(refByteArray[i], byteArray[i]);
    GIMarshallingTests.bytearray_none_in(refByteArray);

    // Another test, with a normal array, to test conversion
    GIMarshallingTests.bytearray_none_in([0, 49, 0xFF, 51]);
}

function testGBytes() {
    var i = 0;
    var refByteArray = new imports.byteArray.ByteArray();
    refByteArray[i++] = 0;
    refByteArray[i++] = 49;
    refByteArray[i++] = 0xFF;
    refByteArray[i++] = 51;
    GIMarshallingTests.gbytes_none_in(refByteArray);

    var bytes = GIMarshallingTests.gbytes_full_return();
    GIMarshallingTests.gbytes_none_in(bytes);

    var array = bytes.toArray();
    assertEquals(array[0], 0);
    assertEquals(array[1], 49);
    assertEquals(array[2], 0xFF);
    assertEquals(array[3], 51); 
    
    bytes = GLib.Bytes.new([0, 49, 0xFF, 51]);
    GIMarshallingTests.gbytes_none_in(bytes);

    bytes = GLib.Bytes.new("const \u2665 utf8");
    GIMarshallingTests.utf8_as_uint8array_in(bytes.toArray());

    bytes = GIMarshallingTests.gbytes_full_return();    
    array = bytes.toArray(); // Array should just be holding a ref, not a copy
    assertEquals(array[1], 49);
    array[1] = 42;  // Assignment should force to GByteArray
    assertEquals(array[1], 42);
    array[1] = 49;  // Flip the value back
    GIMarshallingTests.gbytes_none_in(array.toGBytes()); // Now convert back to GBytes

    bytes = GLib.Bytes.new([97, 98, 99, 100]);
    GIMarshallingTests.array_uint8_in(bytes.toArray());
    assertRaises(function() {
	GIMarshallingTests.array_uint8_in(bytes);
    });
}

function testPtrArray() {
    var array;

    GIMarshallingTests.gptrarray_utf8_none_in(["0", "1", "2"]);

    var refArray = ["0", "1", "2"];

    assertArrayEquals(refArray, GIMarshallingTests.gptrarray_utf8_none_return());
    assertArrayEquals(refArray, GIMarshallingTests.gptrarray_utf8_container_return());
    assertArrayEquals(refArray, GIMarshallingTests.gptrarray_utf8_full_return());

    assertArrayEquals(refArray, GIMarshallingTests.gptrarray_utf8_none_out());
    assertArrayEquals(refArray, GIMarshallingTests.gptrarray_utf8_container_out());
    assertArrayEquals(refArray, GIMarshallingTests.gptrarray_utf8_full_out());
}

function testGValue() {
    assertEquals(42, GIMarshallingTests.gvalue_return());
    assertEquals(42, GIMarshallingTests.gvalue_out());

    GIMarshallingTests.gvalue_in(42);
    GIMarshallingTests.gvalue_flat_array([42, "42", true]);

    // gjs doesn't support native enum types
    // GIMarshallingTests.gvalue_in_enum(GIMarshallingTests.Enum.VALUE_3);

    // Test a flat GValue round-trip return
    let thing = GIMarshallingTests.return_gvalue_flat_array();
    assertArrayEquals([42, "42", true], thing);
}

function testGType() {
    assertEquals("void", GObject.TYPE_NONE.name);
    assertEquals("gchararray", GObject.TYPE_STRING.name);

    // Make sure "name" is readonly
    try {
        GObject.TYPE_STRING.name = "foo";
    } catch(e) {
    }
    assertEquals("gchararray", GObject.TYPE_STRING.name);

    // Make sure "name" is permanent
    try {
        delete GObject.TYPE_STRING.name;
    } catch(e) {
    }
    assertEquals("gchararray", GObject.TYPE_STRING.name);

    // Make sure "toString" works
    assertEquals("[object GType for 'void']", GObject.TYPE_NONE.toString());
    assertEquals("[object GType for 'gchararray']", GObject.TYPE_STRING.toString());

    // Marshalling tests
    assertEquals(GObject.TYPE_NONE, GIMarshallingTests.gtype_return());
    assertEquals(GObject.TYPE_STRING, GIMarshallingTests.gtype_string_return());

    GIMarshallingTests.gtype_in(GObject.TYPE_NONE);
    GIMarshallingTests.gtype_in(GObject.VoidType);
    GIMarshallingTests.gtype_string_in(GObject.TYPE_STRING);
    GIMarshallingTests.gtype_string_in(GObject.String);
    GIMarshallingTests.gtype_string_in(String);

    assertEquals(GObject.TYPE_NONE, GIMarshallingTests.gtype_out());
    assertEquals(GObject.TYPE_STRING, GIMarshallingTests.gtype_string_out());

    assertEquals(GObject.TYPE_INT, GIMarshallingTests.gtype_inout(GObject.TYPE_NONE));
}

function testGTypePrototype() {
    assertNull(GIRepositoryGType.name);
    assertEquals("[object GType prototype]", GIRepositoryGType.toString());
}

function testGValueGType() {
    // test that inferring the GType for a primitive value or an object works

    // Primitives (and primitive like)
    GIMarshallingTests.gvalue_in_with_type(42, GObject.TYPE_INT);
    GIMarshallingTests.gvalue_in_with_type(42.5, GObject.TYPE_DOUBLE);
    GIMarshallingTests.gvalue_in_with_type('42', GObject.TYPE_STRING);
    GIMarshallingTests.gvalue_in_with_type(GObject.TYPE_GTYPE, GObject.TYPE_GTYPE);

    GIMarshallingTests.gvalue_in_with_type(42, GObject.Int);
    GIMarshallingTests.gvalue_in_with_type(42.5, GObject.Double);
    GIMarshallingTests.gvalue_in_with_type(42.5, Number);

    // Object and interface
    GIMarshallingTests.gvalue_in_with_type(new Gio.SimpleAction, Gio.SimpleAction);
    GIMarshallingTests.gvalue_in_with_type(new Gio.SimpleAction, GObject.Object);
    GIMarshallingTests.gvalue_in_with_type(new Gio.SimpleAction, GObject.TYPE_OBJECT);
    GIMarshallingTests.gvalue_in_with_type(new Gio.SimpleAction, Gio.SimpleAction);

    // Boxed and union
    GIMarshallingTests.gvalue_in_with_type(new GLib.KeyFile, GLib.KeyFile);
    GIMarshallingTests.gvalue_in_with_type(new GLib.KeyFile, GObject.TYPE_BOXED);
    GIMarshallingTests.gvalue_in_with_type(GLib.Variant.new('u', 42), GLib.Variant);
    GIMarshallingTests.gvalue_in_with_type(GLib.Variant.new('u', 42), GObject.TYPE_VARIANT);
    GIMarshallingTests.gvalue_in_with_type(new GIMarshallingTests.BoxedStruct, GIMarshallingTests.BoxedStruct);
    GIMarshallingTests.gvalue_in_with_type(GIMarshallingTests.union_returnv(), GIMarshallingTests.Union);

    // Other
    GIMarshallingTests.gvalue_in_with_type(GObject.ParamSpec.string('my-param', '', '', GObject.ParamFlags.READABLE, ''),
					   GObject.TYPE_PARAM);

    // Foreign
    let Cairo;
    try {
        Cairo = imports.cairo;
    } catch(e) {
        return;
    }

    let surface = new Cairo.ImageSurface(Cairo.Format.ARGB32, 2, 2);
    let cr = new Cairo.Context(surface);
    GIMarshallingTests.gvalue_in_with_type(cr, Cairo.Context);
    GIMarshallingTests.gvalue_in_with_type(surface, Cairo.Surface);
}

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

function testCallbacks() {
    let a, b, c;
    a = GIMarshallingTests.callback_return_value_only(callback_return_value_only);
    assertEquals(42, a);

    a = GIMarshallingTests.callback_one_out_parameter(callback_one_out_parameter);
    assertEquals(43, a);

    [a, b] = GIMarshallingTests.callback_multiple_out_parameters(callback_multiple_out_parameters);
    assertEquals(44, a);
    assertEquals(45, b);

    [a, b] = GIMarshallingTests.callback_return_value_and_one_out_parameter(callback_return_value_and_one_out_parameter);
    assertEquals(46, a);
    assertEquals(47, b);

    [a, b, c] = GIMarshallingTests.callback_return_value_and_multiple_out_parameters(callback_return_value_and_multiple_out_parameters);
    assertEquals(48, a);
    assertEquals(49, b);
    assertEquals(50, c);
}

const VFuncTester = new Lang.Class({
    Name: 'VFuncTester',
    Extends: GIMarshallingTests.Object,

    vfunc_vfunc_return_value_only: callback_return_value_only,
    vfunc_vfunc_one_out_parameter: callback_one_out_parameter,
    vfunc_vfunc_multiple_out_parameters: callback_multiple_out_parameters,
    vfunc_vfunc_return_value_and_one_out_parameter: callback_return_value_and_one_out_parameter,
    vfunc_vfunc_return_value_and_multiple_out_parameters: callback_return_value_and_multiple_out_parameters
});

function testVFuncs() {
    let tester = new VFuncTester();
    let a, b, c;
    a = tester.vfunc_return_value_only();
    assertEquals(42, a);

    a = tester.vfunc_one_out_parameter();
    assertEquals(43, a);

    [a, b] = tester.vfunc_multiple_out_parameters();
    assertEquals(44, a);
    assertEquals(45, b);

    [a, b] = tester.vfunc_return_value_and_one_out_parameter();
    assertEquals(46, a);
    assertEquals(47, b);

    [a, b, c] = tester.vfunc_return_value_and_multiple_out_parameters();
    assertEquals(48, a);
    assertEquals(49, b);
    assertEquals(50, c);
}

function testInterfaces() {
    let ifaceImpl = new GIMarshallingTests.InterfaceImpl();
    let itself = ifaceImpl.get_as_interface();

    assertEquals(ifaceImpl, itself);
}

gjstestRun();
