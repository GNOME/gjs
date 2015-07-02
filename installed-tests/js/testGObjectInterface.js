// -*- mode: js; indent-tabs-mode: nil -*-

const Gio = imports.gi.Gio;
const GLib = imports.gi.GLib;
const GObject = imports.gi.GObject;
const JSUnit = imports.jsUnit;
const Lang = imports.lang;
const Mainloop = imports.mainloop;

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

const AGObjectInterface = new Lang.Interface({
    Name: 'AGObjectInterface',
    GTypeName: 'ArbitraryGTypeName',
    Requires: [ GObject.Object ],
    Properties: {
        'interface-prop': GObject.ParamSpec.string('interface-prop',
            'Interface property', 'Must be overridden in implementation',
            GObject.ParamFlags.READABLE,
            'foobar')
    },
    Signals: {
        'interface-signal': {}
    },

    requiredG: Lang.Interface.UNIMPLEMENTED,
    optionalG: function () {
        return 'AGObjectInterface.optionalG()';
    }
});

const InterfaceRequiringGObjectInterface = new Lang.Interface({
    Name: 'InterfaceRequiringGObjectInterface',
    Requires: [ AGObjectInterface ],

    optionalG: function () {
        return 'InterfaceRequiringGObjectInterface.optionalG()\n' +
            AGObjectInterface.optionalG(this);
    }
});

const GObjectImplementingGObjectInterface = new Lang.Class({
    Name: 'GObjectImplementingGObjectInterface',
    Extends: GObject.Object,
    Implements: [ AGObjectInterface ],
    Properties: {
        'interface-prop': GObject.ParamSpec.override('interface-prop',
            AGObjectInterface),
        'class-prop': GObject.ParamSpec.string('class-prop', 'Class property',
            'A property that is not on the interface',
            GObject.ParamFlags.READABLE, 'meh')
    },
    Signals: {
        'class-signal': {},
    },

    get interface_prop() {
        return 'foobar';
    },

    get class_prop() {
        return 'meh';
    },

    _init: function (props={}) {
        this.parent(props);
    },
    requiredG: function () {},
    optionalG: function () {
        return AGObjectInterface.optionalG(this);
    }
});

const MinimalImplementationOfAGObjectInterface = new Lang.Class({
    Name: 'MinimalImplementationOfAGObjectInterface',
    Extends: GObject.Object,
    Implements: [ AGObjectInterface ],
    Properties: {
        'interface-prop': GObject.ParamSpec.override('interface-prop',
            AGObjectInterface)
    },

    _init: function (props={}) {
        this.parent(props);
    },
    requiredG: function () {}
});

const ImplementationOfTwoInterfaces = new Lang.Class({
    Name: 'ImplementationOfTwoInterfaces',
    Extends: GObject.Object,
    Implements: [ AGObjectInterface, InterfaceRequiringGObjectInterface ],
    Properties: {
        'interface-prop': GObject.ParamSpec.override('interface-prop',
            AGObjectInterface)
    },

    _init: function (props={}) {
        this.parent(props);
    },
    requiredG: function () {},
    optionalG: function () {
        return InterfaceRequiringGObjectInterface.optionalG(this);
    }
});

function testGObjectClassCanImplementInterface() {
    // Test will fail if the constructor throws an exception
    let obj = new GObjectImplementingLangInterface();
    JSUnit.assertTrue(obj.constructor.implements(AnInterface));
}

function testRaisesWhenInterfaceRequiresGObjectInterfaceButNotGObject() {
    JSUnit.assertRaises(() => new Lang.Interface({
        Name: 'GObjectInterfaceNotRequiringGObject',
        GTypeName: 'GTypeNameNotRequiringGObject',
        Requires: [ Gio.Initable ]
    }));
}

function testGObjectCanImplementInterfacesFromJSAndC() {
    // Test will fail if the constructor throws an exception
    const ObjectImplementingLangInterfaceAndCInterface = new Lang.Class({
        Name: 'ObjectImplementingLangInterfaceAndCInterface',
        Extends: GObject.Object,
        Implements: [ AnInterface, Gio.Initable ],

        _init: function (props={}) {
            this.parent(props);
        }
    });
    let obj = new ObjectImplementingLangInterfaceAndCInterface();
    JSUnit.assertTrue(obj.constructor.implements(AnInterface));
    JSUnit.assertTrue(obj.constructor.implements(Gio.Initable));
}

function testGObjectInterfaceIsInstanceOfInterfaces() {
    JSUnit.assertTrue(AGObjectInterface instanceof Lang.Interface);
    JSUnit.assertTrue(AGObjectInterface instanceof GObject.Interface);
}

function testGObjectInterfaceCannotBeInstantiated() {
    JSUnit.assertRaises(() => new AGObjectInterface());
}

function testGObjectInterfaceTypeName() {
    JSUnit.assertEquals('ArbitraryGTypeName', AGObjectInterface.$gtype.name);
}

function testGObjectCanImplementInterface() {
    // Test will fail if the constructor throws an exception
    let obj = new GObjectImplementingGObjectInterface();
    JSUnit.assertTrue(obj.constructor.implements(AGObjectInterface));
}

function testGObjectImplementingInterfaceHasCorrectClassObject() {
    JSUnit.assertEquals('[object GObjectClass for GObjectImplementingGObjectInterface]', GObjectImplementingGObjectInterface.toString());
    let obj = new GObjectImplementingGObjectInterface();
    JSUnit.assertEquals(GObjectImplementingGObjectInterface, obj.constructor);
    JSUnit.assertEquals('[object GObjectClass for GObjectImplementingGObjectInterface]',
        obj.constructor.toString());
}

function testGObjectCanImplementBothGObjectAndNonGObjectInterfaces() {
    // Test will fail if the constructor throws an exception
    const GObjectImplementingBothKindsOfInterface = new Lang.Class({
        Name: 'GObjectImplementingBothKindsOfInterface',
        Extends: GObject.Object,
        Implements: [ AnInterface, AGObjectInterface ],
        Properties: {
            'interface-prop': GObject.ParamSpec.override('interface-prop',
                AGObjectInterface)
        },

        _init: function (props={}) {
            this.parent(props);
        },
        required: function () {},
        requiredG: function () {}
    });
    let obj = new GObjectImplementingBothKindsOfInterface();
    JSUnit.assertTrue(obj.constructor.implements(AnInterface));
    JSUnit.assertTrue(obj.constructor.implements(AGObjectInterface));
}

function testGObjectCanImplementRequiredFunction() {
    // Test considered passing if no exception thrown
    let obj = new GObjectImplementingGObjectInterface();
    obj.requiredG();
}

function testGObjectMustImplementRequiredFunction () {
    JSUnit.assertRaises(() => new Lang.Class({
        Name: 'BadObject',
        Extends: GObject.Object,
        Implements: [ AGObjectInterface ],
        Properties: {
            'interface-prop': GObject.ParamSpec.override('interface-prop',
                AGObjectInterface)
        }
    }));
}

function testGObjectDoesntHaveToImplementOptionalFunction() {
    // Test will fail if the constructor throws an exception
    let obj = new MinimalImplementationOfAGObjectInterface();
    JSUnit.assertTrue(obj.constructor.implements(AGObjectInterface));
}

function testGObjectCanDeferToInterfaceOptionalFunction() {
    let obj = new MinimalImplementationOfAGObjectInterface();
    JSUnit.assertEquals('AGObjectInterface.optionalG()', obj.optionalG());
}

function testGObjectCanChainUpToInterface() {
    let obj = new GObjectImplementingGObjectInterface();
    JSUnit.assertEquals('AGObjectInterface.optionalG()', obj.optionalG());
}

function testGObjectInterfaceCanRequireOtherInterface() {
    // Test will fail if the constructor throws an exception
    let obj = new ImplementationOfTwoInterfaces();
    JSUnit.assertTrue(obj.constructor.implements(AGObjectInterface));
    JSUnit.assertTrue(obj.constructor.implements(InterfaceRequiringGObjectInterface));
}

function testGObjectInterfaceCanChainUpToOtherInterface() {
    let obj = new ImplementationOfTwoInterfaces();
    JSUnit.assertEquals('InterfaceRequiringGObjectInterface.optionalG()\nAGObjectInterface.optionalG()',
        obj.optionalG());
}

function testGObjectDefersToLastInterfaceOptionalFunction() {
    const MinimalImplementationOfTwoInterfaces = new Lang.Class({
        Name: 'MinimalImplementationOfTwoInterfaces',
        Extends: GObject.Object,
        Implements: [ AGObjectInterface, InterfaceRequiringGObjectInterface ],
        Properties: {
            'interface-prop': GObject.ParamSpec.override('interface-prop',
                AGObjectInterface)
        },

        _init: function (props={}) {
            this.parent(props);
        },
        requiredG: function () {}
    });
    let obj = new MinimalImplementationOfTwoInterfaces();
    JSUnit.assertEquals('InterfaceRequiringGObjectInterface.optionalG()\nAGObjectInterface.optionalG()',
        obj.optionalG());
}

function testGObjectClassMustImplementAllRequiredInterfaces() {
    JSUnit.assertRaises(() => new Lang.Class({
        Name: 'BadObject',
        Implements: [ InterfaceRequiringGObjectInterface ],
        required: function () {}
    }));
}

function testGObjectClassMustImplementRequiredInterfacesInCorrectOrder() {
    JSUnit.assertRaises(() => new Lang.Class({
        Name: 'BadObject',
        Implements: [ InterfaceRequiringGObjectInterface, AGObjectInterface ],
        required: function () {}
    }));
}

function testGObjectInterfaceCanRequireInterfaceFromC() {
    const InitableInterface = new Lang.Interface({
        Name: 'InitableInterface',
        Requires: [ GObject.Object, Gio.Initable ]
    });
    JSUnit.assertRaises(() => new Lang.Class({
        Name: 'BadObject',
        Implements: [ InitableInterface ]
    }));
}

function testGObjectHasInterfaceSignalsAndClassSignals() {
    let obj = new GObjectImplementingGObjectInterface();
    let interface_signal_emitted = false, class_signal_emitted = false;
    obj.connect('interface-signal', () => {
        interface_signal_emitted = true;
        Mainloop.quit('signal');
    });
    obj.connect('class-signal', () => {
        class_signal_emitted = true;
        Mainloop.quit('signal');
    });
    GLib.idle_add(GLib.PRIORITY_DEFAULT, () => obj.emit('interface-signal'));
    Mainloop.run('signal');
    GLib.idle_add(GLib.PRIORITY_DEFAULT, () => obj.emit('class-signal'));
    Mainloop.run('signal');
    JSUnit.assertTrue(interface_signal_emitted);
    JSUnit.assertTrue(class_signal_emitted);
}

function testGObjectHasInterfacePropertiesAndClassProperties() {
    let obj = new GObjectImplementingGObjectInterface();
    JSUnit.assertEquals('foobar', obj.interface_prop);
    JSUnit.assertEquals('meh', obj.class_prop);
}

// Failing to override an interface property doesn't raise an error but instead
// logs a critical warning.
function testGObjectMustOverrideInterfaceProperties() {
    GLib.test_expect_message('GLib-GObject', GLib.LogLevelFlags.LEVEL_CRITICAL,
        "Object class * doesn't implement property 'interface-prop' from " +
        "interface 'ArbitraryGTypeName'");
    new Lang.Class({
        Name: 'MyNaughtyObject',
        Extends: GObject.Object,
        Implements: [ AGObjectInterface ],
        _init: function (props={}) {
            this.parent(props);
        },
        requiredG: function () {}
    });
    // g_test_assert_expected_messages() is a macro, not introspectable
    GLib.test_assert_expected_messages_internal('Gjs', 'testGObjectInterface.js',
        416, 'testGObjectMustOverrideInterfaceProperties');
}

// This makes sure that we catch the case where the metaclass (e.g.
// GtkWidgetClass) doesn't specify a meta-interface. In that case we get the
// meta-interface from the metaclass's parent.
function testInterfaceIsOfCorrectTypeForMetaclass() {
    const MyMeta = new Lang.Class({
        Name: 'MyMeta',
        Extends: GObject.Class
    });
    const MyMetaObject = new MyMeta({
        Name: 'MyMetaObject'
    });
    const MyMetaInterface = new Lang.Interface({
        Name: 'MyMetaInterface',
        Requires: [ MyMetaObject ]
    });
    JSUnit.assertTrue(MyMetaInterface instanceof GObject.Interface);
}

function testSubclassImplementsTheSameInterfaceAsItsParent() {
    const SubObject = new Lang.Class({
        Name: 'SubObject',
        Extends: GObjectImplementingGObjectInterface
    });
    let obj = new SubObject();
    JSUnit.assertTrue(obj.constructor.implements(AGObjectInterface));
    JSUnit.assertEquals('foobar', obj.interface_prop);  // override not needed
}

function testSubclassCanReimplementTheSameInterfaceAsItsParent() {
    const SubImplementer = new Lang.Class({
        Name: 'SubImplementer',
        Extends: GObjectImplementingGObjectInterface,
        Implements: [ AGObjectInterface ]
    });
    let obj = new SubImplementer();
    JSUnit.assertTrue(obj.constructor.implements(AGObjectInterface));
    JSUnit.assertEquals('foobar', obj.interface_prop);  // override not needed
}

JSUnit.gjstestRun(this, JSUnit.setUp, JSUnit.tearDown);
