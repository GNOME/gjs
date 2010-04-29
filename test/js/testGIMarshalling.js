const GIMarshallingTests = imports.gi.GIMarshallingTests;

// We use Gio to have some objects that we know exist
const Gio = imports.gi.Gio;
const Lang = imports.lang;

function testGArray() {
    var array = GIMarshallingTests.garray_int_none_return();
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

gjstestRun();
