// application/javascript;version=1.8

const JSUnit = imports.jsUnit;
const Everything = imports.gi.Regress;
const WarnLib = imports.gi.WarnLib;

// We use Gio to have some objects that we know exist
const GLib = imports.gi.GLib;
const Gio = imports.gi.Gio;
const GObject = imports.gi.GObject;

function testGObjectClass() {
    let find_property = GObject.Object.find_property;

    let p1 = find_property.call(Gio.ThemedIcon, 'name');

    JSUnit.assert(p1 instanceof GObject.ParamSpec);
    JSUnit.assertEquals('name', p1.name);

    let p2 = find_property.call(Gio.SimpleAction, 'enabled');

    JSUnit.assert(p2 instanceof GObject.ParamSpec);
    JSUnit.assertEquals('enabled', p2.name);
    JSUnit.assertEquals(true, p2.default_value);
}

JSUnit.gjstestRun(this, JSUnit.setUp, JSUnit.tearDown);
