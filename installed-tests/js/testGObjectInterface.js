// -*- mode: js; indent-tabs-mode: nil -*-

const Gio = imports.gi.Gio;
const GObject = imports.gi.GObject;
const JSUnit = imports.jsUnit;
const Lang = imports.lang;

const AnInterface = new Lang.Interface({
    Name: 'AnInterface',
});

const GObjectImplementingLangInterface = new Lang.Class({
    Name: 'GObjectImplementingLangInterface',
    Extends: GObject.Object,
    Implements: [ AnInterface ],

    _init: function (props={}) {
        this.parent(props);
    }
});

function testGObjectClassCanImplementInterface() {
    // Test considered passing if no exception thrown
    new GObjectImplementingLangInterface();
}

function testGObjectCanImplementInterfacesFromJSAndC() {
    // Test considered passing if no exception thrown
    const ObjectImplementingLangInterfaceAndCInterface = new Lang.Class({
        Name: 'ObjectImplementingLangInterfaceAndCInterface',
        Extends: GObject.Object,
        Implements: [ AnInterface, Gio.Initable ],

        _init: function (props={}) {
            this.parent(props);
        }
    });
    new ObjectImplementingLangInterfaceAndCInterface();
}

JSUnit.gjstestRun(this, JSUnit.setUp, JSUnit.tearDown);
