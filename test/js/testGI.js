// application/javascript;version=1.8
function testUTF8() {
    const GLib = imports.gi.GLib;

    // gunichar is temporarily not-introspectable
    //assertEquals(0x2664, GLib.utf8_get_char("\u2664 utf8"));
}

function testThrows() {
    const GLib = imports.gi.GLib;

    assertRaises(function() { return GLib.file_read_link(""); });
}

function testMultiReturn() {
    const GLib = imports.gi.GLib;

    let [success, content, len] = GLib.file_get_contents('/etc/passwd')
    assertEquals(success, true);
}

gjstestRun();
