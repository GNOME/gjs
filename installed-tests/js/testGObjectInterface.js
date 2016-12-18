// -*- mode: js; indent-tabs-mode: nil -*-

const Gio = imports.gi.Gio;
const GLib = imports.gi.GLib;
const GObject = imports.gi.GObject;
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

describe('GObject interface', function () {
    it('class can implement a Lang.Interface', function () {
        let obj;
        expect(() => { obj = new GObjectImplementingLangInterface(); })
            .not.toThrow();
        expect(obj.constructor.implements(AnInterface)).toBeTruthy();
    });

    it('throws when an interface requires a GObject interface but not GObject.Object', function () {
        expect(() => new Lang.Interface({
            Name: 'GObjectInterfaceNotRequiringGObject',
            GTypeName: 'GTypeNameNotRequiringGObject',
            Requires: [ Gio.Initable ]
        })).toThrow();
    });

    it('can be implemented by a GObject class along with a JS interface', function () {
        const ObjectImplementingLangInterfaceAndCInterface = new Lang.Class({
            Name: 'ObjectImplementingLangInterfaceAndCInterface',
            Extends: GObject.Object,
            Implements: [ AnInterface, Gio.Initable ],

            _init: function (props={}) {
                this.parent(props);
            }
        });
        let obj;
        expect(() => { obj = new ObjectImplementingLangInterfaceAndCInterface(); })
            .not.toThrow();
        expect(obj.constructor.implements(AnInterface)).toBeTruthy();
        expect(obj.constructor.implements(Gio.Initable)).toBeTruthy();
    });

    it('is an instance of the interface classes', function () {
        expect(AGObjectInterface instanceof Lang.Interface).toBeTruthy();
        expect(AGObjectInterface instanceof GObject.Interface).toBeTruthy();
    });

    it('cannot be instantiated', function () {
        expect(() => new AGObjectInterface()).toThrow();
    });

    it('reports its type name', function () {
        expect(AGObjectInterface.$gtype.name).toEqual('ArbitraryGTypeName');
    });

    it('can be implemented by a GObject class', function () {
        let obj;
        expect(() => { obj = new GObjectImplementingGObjectInterface(); })
            .not.toThrow();
        expect(obj.constructor.implements(AGObjectInterface)).toBeTruthy();
    });

    it('is implemented by a GObject class with the correct class object', function () {
        expect(GObjectImplementingGObjectInterface.toString())
            .toEqual('[object GObjectClass for GObjectImplementingGObjectInterface]');
        let obj = new GObjectImplementingGObjectInterface();
        expect(obj.constructor).toEqual(GObjectImplementingGObjectInterface);
        expect(obj.constructor.toString())
            .toEqual('[object GObjectClass for GObjectImplementingGObjectInterface]');
    });

    it('can be implemented by a class also implementing a Lang.Interface', function () {
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
        let obj;
        expect(() => { obj = new GObjectImplementingBothKindsOfInterface(); })
            .not.toThrow();
        expect(obj.constructor.implements(AnInterface)).toBeTruthy();
        expect(obj.constructor.implements(AGObjectInterface)).toBeTruthy();
    });

    it('can have its required function implemented', function () {
        expect(() => {
            let obj = new GObjectImplementingGObjectInterface();
            obj.requiredG();
        }).not.toThrow();
    });

    it('must have its required function implemented', function () {
        expect(() => new Lang.Class({
            Name: 'BadObject',
            Extends: GObject.Object,
            Implements: [ AGObjectInterface ],
            Properties: {
                'interface-prop': GObject.ParamSpec.override('interface-prop',
                    AGObjectInterface)
            }
        })).toThrow();
    });

    it("doesn't have to have its optional function implemented", function () {
        let obj;
        expect(() => { obj = new MinimalImplementationOfAGObjectInterface(); })
            .not.toThrow();
        expect(obj.constructor.implements(AGObjectInterface)).toBeTruthy();
    });

    it('can have its optional function deferred to by the implementation', function () {
        let obj = new MinimalImplementationOfAGObjectInterface();
        expect(obj.optionalG()).toEqual('AGObjectInterface.optionalG()');
    });

    it('can have its function chained up to', function () {
        let obj = new GObjectImplementingGObjectInterface();
        expect(obj.optionalG()).toEqual('AGObjectInterface.optionalG()');
    });

    it('can require another interface', function () {
        let obj;
        expect(() => { obj = new ImplementationOfTwoInterfaces(); }).not.toThrow();
        expect(obj.constructor.implements(AGObjectInterface)).toBeTruthy();
        expect(obj.constructor.implements(InterfaceRequiringGObjectInterface))
            .toBeTruthy();
    });

    it('can chain up to another interface', function () {
        let obj = new ImplementationOfTwoInterfaces();
        expect(obj.optionalG())
            .toEqual('InterfaceRequiringGObjectInterface.optionalG()\nAGObjectInterface.optionalG()');
    });

    it("defers to the last interface's optional function", function () {
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
        expect(obj.optionalG())
            .toEqual('InterfaceRequiringGObjectInterface.optionalG()\nAGObjectInterface.optionalG()');
    });

    it('must be implemented by a class that implements all required interfaces', function () {
        expect(() => new Lang.Class({
            Name: 'BadObject',
            Implements: [ InterfaceRequiringGObjectInterface ],
            required: function () {}
        })).toThrow();
    });

    it('must be implemented by a class that implements required interfaces in correct order', function () {
        expect(() => new Lang.Class({
            Name: 'BadObject',
            Implements: [ InterfaceRequiringGObjectInterface, AGObjectInterface ],
            required: function () {}
        })).toThrow();
    });

    it('can require an interface from C', function () {
        const InitableInterface = new Lang.Interface({
            Name: 'InitableInterface',
            Requires: [ GObject.Object, Gio.Initable ]
        });
        expect(() => new Lang.Class({
            Name: 'BadObject',
            Implements: [ InitableInterface ]
        })).toThrow();
    });

    it('can define signals on the implementing class', function () {
        function quitLoop() {
            Mainloop.quit('signal');
        }
        let obj = new GObjectImplementingGObjectInterface();
        let interfaceSignalSpy = jasmine.createSpy('interfaceSignalSpy')
            .and.callFake(quitLoop);
        let classSignalSpy = jasmine.createSpy('classSignalSpy')
            .and.callFake(quitLoop);
        obj.connect('interface-signal', interfaceSignalSpy);
        obj.connect('class-signal', classSignalSpy);
        GLib.idle_add(GLib.PRIORITY_DEFAULT, () => {
            obj.emit('interface-signal');
            return GLib.SOURCE_REMOVE;
        });
        Mainloop.run('signal');
        GLib.idle_add(GLib.PRIORITY_DEFAULT, () => {
            obj.emit('class-signal');
            return GLib.SOURCE_REMOVE;
        });
        Mainloop.run('signal');
        expect(interfaceSignalSpy).toHaveBeenCalled();
        expect(classSignalSpy).toHaveBeenCalled();
    });

    it('can define properties on the implementing class', function () {
        let obj = new GObjectImplementingGObjectInterface();
        expect(obj.interface_prop).toEqual('foobar');
        expect(obj.class_prop).toEqual('meh');
    });

    it('must have its properties overridden', function () {
        // Failing to override an interface property doesn't raise an error but
        // instead logs a critical warning.
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
    });

    // This makes sure that we catch the case where the metaclass (e.g.
    // GtkWidgetClass) doesn't specify a meta-interface. In that case we get the
    // meta-interface from the metaclass's parent.
    it('gets the correct type for its metaclass', function () {
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
        expect(MyMetaInterface instanceof GObject.Interface).toBeTruthy();
    });

    it('can be implemented by a class as well as its parent class', function () {
        const SubObject = new Lang.Class({
            Name: 'SubObject',
            Extends: GObjectImplementingGObjectInterface
        });
        let obj = new SubObject();
        expect(obj.constructor.implements(AGObjectInterface)).toBeTruthy();
        expect(obj.interface_prop).toEqual('foobar');  // override not needed
    });

    it('can be reimplemented by a subclass of a class that already implements it', function () {
        const SubImplementer = new Lang.Class({
            Name: 'SubImplementer',
            Extends: GObjectImplementingGObjectInterface,
            Implements: [ AGObjectInterface ]
        });
        let obj = new SubImplementer();
        expect(obj.constructor.implements(AGObjectInterface)).toBeTruthy();
        expect(obj.interface_prop).toEqual('foobar');  // override not needed
    });
});
