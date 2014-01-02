const JSUnit = imports.jsUnit;

function testImporter1() {
    var GLib = imports.gi.GLib;
    JSUnit.assertEquals(GLib.MAJOR_VERSION, 2);
}

JSUnit.gjstestRun(this, JSUnit.setUp, JSUnit.tearDown);
