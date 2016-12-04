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

function testGType() {
    JSUnit.assertEquals("void", GObject.TYPE_NONE.name);
    JSUnit.assertEquals("gchararray", GObject.TYPE_STRING.name);

    // Make sure "name" is readonly
    try {
        GObject.TYPE_STRING.name = "foo";
    } catch(e) {
    }
    JSUnit.assertEquals("gchararray", GObject.TYPE_STRING.name);

    // Make sure "name" is permanent
    try {
        delete GObject.TYPE_STRING.name;
    } catch(e) {
    }
    JSUnit.assertEquals("gchararray", GObject.TYPE_STRING.name);

    // Make sure "toString" works
    JSUnit.assertEquals("[object GType for 'void']", GObject.TYPE_NONE.toString());
    JSUnit.assertEquals("[object GType for 'gchararray']", GObject.TYPE_STRING.toString());
}

function testGTypePrototype() {
    JSUnit.assertNull(GIRepositoryGType.name);
    JSUnit.assertEquals("[object GType prototype]", GIRepositoryGType.toString());
}

JSUnit.gjstestRun(this, JSUnit.setUp, JSUnit.tearDown);
