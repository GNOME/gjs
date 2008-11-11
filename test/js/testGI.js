function testUTF8() {
    const GLib = imports.gi.GLib;

    assertEquals(0x2664, GLib.utf8_get_char("\u2664 utf8"));
}

gjstestRun();
