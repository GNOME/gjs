// application/javascript;version=1.8
function testImporter1() {
    var GLib = imports.gi.GLib;
    assertEquals(GLib.MAJOR_VERSION, 2);
}

gjstestRun();
