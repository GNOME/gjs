// application/javascript;version=1.8
const GIMarshallingTests = imports.gi.GIMarshallingTests;

// We use Gio to have some objects that we know exist
const Gio = imports.gi.Gio;
const Lang = imports.lang;

function testCArray() {
    var array;
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

    GIMarshallingTests.array_string_in(["foo", "bar"], 2);

    array = [];
    for (var i = 0; i < 3; i++) {
	array[i] = new GIMarshallingTests.BoxedStruct();
	array[i].long_ = i + 1;
    }

    GIMarshallingTests.array_struct_in(array, array.length);

    // Run twice to ensure that copies are correctly made for (transfer full)
    GIMarshallingTests.array_struct_take_in(array, array.length);
    GIMarshallingTests.array_struct_take_in(array, array.length);

    GIMarshallingTests.array_uint8_in ("abcd", 4);
    GIMarshallingTests.array_enum_in([GIMarshallingTests.Enum.VALUE1,
				      GIMarshallingTests.Enum.VALUE2,
				      GIMarshallingTests.Enum.VALUE3], 3);

    array = [-1, 0, 1, 2];
    GIMarshallingTests.array_in(array, array.length);
    GIMarshallingTests.array_in_len_before(array.length, array);
    GIMarshallingTests.array_in_len_zero_terminated(array, array.length);
    GIMarshallingTests.array_in_guint64_len(array, array.length);
    GIMarshallingTests.array_in_guint8_len(array, array.length);
}

function testGArray() {
    var array;
    // Tests disabled due to do g-i typelib compilation bug
    // https://bugzilla.gnome.org/show_bug.cgi?id=622335
    //array = GIMarshallingTests.garray_int_none_return();
    //assertEquals(-1, array[0]);
    //assertEquals(0, array[1]);
    //assertEquals(1, array[2]);
    //assertEquals(2, array[3]);
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

    // GIMarshallingTests.garray_int_none_in([-1, 0, 1, 2])
    // GIMarshallingTests.garray_utf8_none_in(["0", "1", "2"])

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
    var byteArray = GIMarshallingTests.bytearray_full_return();
    assertEquals("arrayLength", 4, byteArray.length);
    assertEquals("a[0]", '0'.charCodeAt(0), byteArray[0]);
    assertEquals("a[1]", '1'.charCodeAt(0), byteArray[1])
    assertEquals("a[2]", '2'.charCodeAt(0), byteArray[2]);
    assertEquals("a[3]", '3'.charCodeAt(0), byteArray[3]);
    let ba = imports.byteArray.fromString("0123");
    GIMarshallingTests.bytearray_none_in(ba);
}

gjstestRun();
