function testUTF8() {
    const GLib = imports.gi.GLib;

    assertEquals(0x2664, GLib.utf8_get_char("\u2664 utf8"));
}

function testThrows() {
    const GLib = imports.gi.GLib;

    assertRaises(function() { return GLib.file_read_link(""); });
}

gjstestRun();
