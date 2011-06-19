// application/javascript;version=1.8
const GIMarshallingTests = imports.gi.GIMarshallingTests;

// We use Gio to have some objects that we know exist
const Gio = imports.gi.Gio;
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

    GIMarshallingTests.array_uint8_in ("abcd", 4);
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

    GIMarshallingTests.garray_int_none_in([-1, 0, 1, 2])
    GIMarshallingTests.garray_utf8_none_in(["0", "1", "2"])

    array = GIMarshallingTests.garray_utf8_none_out()
    assertEquals("0", array[0]);
    assertEquals("1", array[1]);
    assertEquals("2", array[2]);
    array = GIMarshallingTests.garray_utf8_container_out()
    assertEquals("0", array[0]);
    assertEquals("1", array[1]);
    assertEquals("2", array[2]);
    array = GIMarshallingTests.garray_utf8_full_out()
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

    // Another test, with a string, to test conversion
    GIMarshallingTests.bytearray_none_in("\x00\x31\xFF\x33");
}

function testPtrArray() {
    function arrayEqual(ref, val) {
	assertEquals(ref.length, val.length);
	for (i = 0; i < ref.length; i++)
	    assertEquals(ref[i], val[i]);
    }
    var array;

    GIMarshallingTests.gptrarray_utf8_none_in(["0", "1", "2"]);

    var refArray = ["0", "1", "2"];

    arrayEqual(refArray, GIMarshallingTests.gptrarray_utf8_none_return());
    arrayEqual(refArray, GIMarshallingTests.gptrarray_utf8_container_return());
    arrayEqual(refArray, GIMarshallingTests.gptrarray_utf8_full_return());

    arrayEqual(refArray, GIMarshallingTests.gptrarray_utf8_none_out());
    arrayEqual(refArray, GIMarshallingTests.gptrarray_utf8_container_out());
    arrayEqual(refArray, GIMarshallingTests.gptrarray_utf8_full_out());
}

gjstestRun();
