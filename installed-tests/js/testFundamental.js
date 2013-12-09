// application/javascript;version=1.8

const JSUnit = imports.jsUnit;
const Everything = imports.gi.Regress;
const WarnLib = imports.gi.WarnLib;

const GLib = imports.gi.GLib;
const Gio = imports.gi.Gio;
const GObject = imports.gi.GObject;
const Lang = imports.lang;

function testFundamental() {
    let f = new Everything.TestFundamentalSubObject('plop');
}

JSUnit.gjstestRun(this, JSUnit.setUp, JSUnit.tearDown);
