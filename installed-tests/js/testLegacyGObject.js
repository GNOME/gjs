// -*- mode: js; indent-tabs-mode: nil -*-
/* eslint-disable no-restricted-properties */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2011 Giovanni Campagna <gcampagna@src.gnome.org>
// SPDX-FileCopyrightText: 2015 Endless Mobile, Inc.

imports.gi.versions.Gtk = '3.0';

const Gio = imports.gi.Gio;
const GLib = imports.gi.GLib;
const GObject = imports.gi.GObject;
const Gtk = imports.gi.Gtk;
const Lang = imports.lang;
const Mainloop = imports.mainloop;

const MyObject = new GObject.Class({
    Name: 'MyObject',
    Properties: {
        'readwrite': GObject.ParamSpec.string('readwrite', 'ParamReadwrite',
            'A read write parameter', GObject.ParamFlags.READWRITE, ''),
        'readonly': GObject.ParamSpec.string('readonly', 'ParamReadonly',
            'A readonly parameter', GObject.ParamFlags.READABLE, ''),
        'construct': GObject.ParamSpec.string('construct', 'ParamConstructOnly',
            'A readwrite construct-only parameter',
            GObject.ParamFlags.READWRITE | GObject.ParamFlags.CONSTRUCT_ONLY,
            'default'),
    },
    Signals: {
        'empty': { },
        'minimal': {param_types: [GObject.TYPE_INT, GObject.TYPE_INT]},
        'full': {
            flags: GObject.SignalFlags.RUN_LAST,
            accumulator: GObject.AccumulatorType.FIRST_WINS,
            return_type: GObject.TYPE_INT,
            param_types: [],
        },
        'run-last': {flags: GObject.SignalFlags.RUN_LAST},
        'detailed': {
            flags: GObject.SignalFlags.RUN_FIRST | GObject.SignalFlags.DETAILED,
            param_types: [GObject.TYPE_STRING],
        },
    },

    _init(props) {
        // check that it's safe to set properties before
        // chaining up (priv is NULL at this point, remember)
        this._readwrite = 'foo';
        this._readonly = 'bar';
        this._constructProp = null;
        this._constructCalled = false;

        this.parent(props);
    },

    get readwrite() {
        return this._readwrite;
    },

    set readwrite(val) {
        if (val === 'ignore')
            return;

        this._readwrite = val;
    },

    get readonly() {
        return this._readonly;
    },

    set readonly(val) {
        // this should never be called
        this._readonly = 'bogus';
    },

    get construct() {
        return this._constructProp;
    },

    set construct(val) {
        this._constructProp = val;
    },

    notify_prop() {
        this._readonly = 'changed';

        this.notify('readonly');
    },

    emit_empty() {
        this.emit('empty');
    },

    emit_minimal(one, two) {
        this.emit('minimal', one, two);
    },

    emit_full() {
        return this.emit('full');
    },

    emit_detailed() {
        this.emit('detailed::one');
        this.emit('detailed::two');
    },

    emit_run_last(callback) {
        this._run_last_callback = callback;
        this.emit('run-last');
    },

    on_run_last() {
        this._run_last_callback();
    },

    on_empty() {
        this.empty_called = true;
    },

    on_full() {
        this.full_default_handler_called = true;
        return 79;
    },
});

const MyApplication = new Lang.Class({
    Name: 'MyApplication',
    Extends: Gio.Application,
    Signals: {'custom': {param_types: [GObject.TYPE_INT]}},

    _init(params) {
        this.parent(params);
    },

    emit_custom(n) {
        this.emit('custom', n);
    },
});

const MyInitable = new Lang.Class({
    Name: 'MyInitable',
    Extends: GObject.Object,
    Implements: [Gio.Initable],

    _init(params) {
        this.parent(params);

        this.inited = false;
    },

    vfunc_init(cancellable) { // error?
        if (!(cancellable instanceof Gio.Cancellable))
            throw new Error('Bad argument');

        this.inited = true;
    },
});

const Derived = new Lang.Class({
    Name: 'Derived',
    Extends: MyObject,

    _init() {
        this.parent({readwrite: 'yes'});
    },
});

const OddlyNamed = new Lang.Class({
    Name: 'Legacy.OddlyNamed',
    Extends: MyObject,
});

const MyCustomInit = new Lang.Class({
    Name: 'MyCustomInit',
    Extends: GObject.Object,

    _init() {
        this.foo = false;

        this.parent();
    },

    _instance_init() {
        this.foo = true;
    },
});

describe('GObject class', function () {
    let myInstance;
    beforeEach(function () {
        myInstance = new MyObject();
    });

    it('constructs with default values for properties', function () {
        expect(myInstance.readwrite).toEqual('foo');
        expect(myInstance.readonly).toEqual('bar');
        expect(myInstance.construct).toEqual('default');
    });

    it('constructs with a hash of property values', function () {
        let myInstance2 = new MyObject({readwrite: 'baz', construct: 'asdf'});
        expect(myInstance2.readwrite).toEqual('baz');
        expect(myInstance2.readonly).toEqual('bar');
        expect(myInstance2.construct).toEqual('asdf');
    });

    const ui = `<interface>
                <object class="Gjs_MyObject" id="MyObject">
                  <property name="readwrite">baz</property>
                  <property name="construct">quz</property>
                </object>
              </interface>`;

    it('constructs with property values from Gtk.Builder', function () {
        let builder = Gtk.Builder.new_from_string(ui, -1);
        let myInstance3 = builder.get_object('MyObject');
        expect(myInstance3.readwrite).toEqual('baz');
        expect(myInstance3.readonly).toEqual('bar');
        expect(myInstance3.construct).toEqual('quz');
    });

    it('does not allow changing CONSTRUCT_ONLY properties', function () {
        myInstance.construct = 'val';
        expect(myInstance.construct).toEqual('default');
    });

    it('has a name', function () {
        expect(MyObject.name).toEqual('MyObject');
    });

    // the following would (should) cause a CRITICAL:
    // myInstance.readonly = 'val';

    it('has a notify signal', function () {
        let notifySpy = jasmine.createSpy('notifySpy');
        myInstance.connect('notify::readonly', notifySpy);

        myInstance.notify_prop();
        myInstance.notify_prop();

        expect(notifySpy).toHaveBeenCalledTimes(2);
    });

    it('can define its own signals', function () {
        let emptySpy = jasmine.createSpy('emptySpy');
        myInstance.connect('empty', emptySpy);
        myInstance.emit_empty();

        expect(emptySpy).toHaveBeenCalled();
        expect(myInstance.empty_called).toBeTruthy();
    });

    it('passes emitted arguments to signal handlers', function () {
        let minimalSpy = jasmine.createSpy('minimalSpy');
        myInstance.connect('minimal', minimalSpy);
        myInstance.emit_minimal(7, 5);

        expect(minimalSpy).toHaveBeenCalledWith(myInstance, 7, 5);
    });

    it('can return values from signals', function () {
        let fullSpy = jasmine.createSpy('fullSpy').and.returnValue(42);
        myInstance.connect('full', fullSpy);
        let result = myInstance.emit_full();

        expect(fullSpy).toHaveBeenCalled();
        expect(result).toEqual(42);
    });

    it('does not call first-wins signal handlers after one returns a value', function () {
        let neverCalledSpy = jasmine.createSpy('neverCalledSpy');
        myInstance.connect('full', () => 42);
        myInstance.connect('full', neverCalledSpy);
        myInstance.emit_full();

        expect(neverCalledSpy).not.toHaveBeenCalled();
        expect(myInstance.full_default_handler_called).toBeFalsy();
    });

    it('gets the return value of the default handler', function () {
        let result = myInstance.emit_full();

        expect(myInstance.full_default_handler_called).toBeTruthy();
        expect(result).toEqual(79);
    });

    it('calls run-last default handler last', function () {
        let stack = [];
        let runLastSpy = jasmine.createSpy('runLastSpy')
            .and.callFake(() => {
                stack.push(1);
            });
        myInstance.connect('run-last', runLastSpy);
        myInstance.emit_run_last(() => {
            stack.push(2);
        });

        expect(stack).toEqual([1, 2]);
    });

    it("can inherit from something that's not GObject.Object", function () {
        // ...and still get all the goodies of GObject.Class
        let instance = new MyApplication({application_id: 'org.gjs.Application'});
        let customSpy = jasmine.createSpy('customSpy');
        instance.connect('custom', customSpy);

        instance.emit_custom(73);
        expect(customSpy).toHaveBeenCalledWith(instance, 73);
    });

    it('can implement an interface', function () {
        let instance = new MyInitable();
        expect(instance.constructor.implements(Gio.Initable)).toBeTruthy();
    });

    it('can implement interface vfuncs', function () {
        let instance = new MyInitable();
        expect(instance.inited).toBeFalsy();

        instance.init(new Gio.Cancellable());
        expect(instance.inited).toBeTruthy();
    });

    it('can be a subclass', function () {
        let derived = new Derived();

        expect(derived instanceof Derived).toBeTruthy();
        expect(derived instanceof MyObject).toBeTruthy();

        expect(derived.readwrite).toEqual('yes');
    });

    it('can have any valid Lang.Class name', function () {
        let obj = new OddlyNamed();

        expect(obj instanceof OddlyNamed).toBeTruthy();
        expect(obj instanceof MyObject).toBeTruthy();
    });

    it('calls its _instance_init() function while chaining up in constructor', function () {
        let instance = new MyCustomInit();
        expect(instance.foo).toBeTruthy();
    });

    it('can have an interface-valued property', function () {
        const InterfacePropObject = new Lang.Class({
            Name: 'InterfacePropObject',
            Extends: GObject.Object,
            Properties: {
                'file': GObject.ParamSpec.object('file', 'File', 'File',
                    GObject.ParamFlags.READWRITE | GObject.ParamFlags.CONSTRUCT_ONLY,
                    Gio.File.$gtype),
            },
        });
        let file = Gio.File.new_for_path('dummy');
        expect(() => new InterfacePropObject({file})).not.toThrow();
    });

    it('can override a property from the parent class', function () {
        const OverrideObject = new Lang.Class({
            Name: 'OverrideObject',
            Extends: MyObject,
            Properties: {
                'readwrite': GObject.ParamSpec.override('readwrite', MyObject),
            },
            get readwrite() {
                return this._subclass_readwrite;
            },
            set readwrite(val) {
                this._subclass_readwrite = `subclass${val}`;
            },
        });
        let obj = new OverrideObject();
        obj.readwrite = 'foo';
        expect(obj.readwrite).toEqual('subclassfoo');
    });

    it('cannot override a non-existent property', function () {
        expect(() => new Lang.Class({
            Name: 'BadOverride',
            Extends: GObject.Object,
            Properties: {
                'nonexistent': GObject.ParamSpec.override('nonexistent', GObject.Object),
            },
        })).toThrow();
    });

    it('handles gracefully forgetting to override a C property', function () {
        GLib.test_expect_message('GLib-GObject', GLib.LogLevelFlags.LEVEL_CRITICAL,
            "*Object class Gjs_ForgottenOverride doesn't implement property " +
            "'anchors' from interface 'GTlsFileDatabase'*");

        // This is a random interface in Gio with a read-write property
        const ForgottenOverride = new Lang.Class({
            Name: 'ForgottenOverride',
            Extends: Gio.TlsDatabase,
            Implements: [Gio.TlsFileDatabase],
        });
        const obj = new ForgottenOverride();
        expect(obj.anchors).not.toBeDefined();
        expect(() => (obj.anchors = 'foo')).not.toThrow();
        expect(obj.anchors).toEqual('foo');

        GLib.test_assert_expected_messages_internal('Gjs', 'testGObjectClass.js', 0,
            'testGObjectClassForgottenOverride');
    });

    it('handles gracefully overriding a C property but forgetting the accessors', function () {
        // This is a random interface in Gio with a read-write property
        const ForgottenAccessors = new Lang.Class({
            Name: 'ForgottenAccessors',
            Extends: Gio.TlsDatabase,
            Implements: [Gio.TlsFileDatabase],
            Properties: {
                'anchors': GObject.ParamSpec.override('anchors', Gio.TlsFileDatabase),
            },
        });
        const obj = new ForgottenAccessors();
        expect(obj.anchors).toBeNull();
        obj.anchors = 'foo';

        const ForgottenAccessors2 = new Lang.Class({
            Name: 'ForgottenAccessors2',
            Extends: ForgottenAccessors,
        });
        const obj2 = new ForgottenAccessors2();
        expect(obj2.anchors).toBeNull();
        obj2.anchors = 'foo';
    });
});

const AnInterface = new Lang.Interface({
    Name: 'AnInterface',
});

const GObjectImplementingLangInterface = new Lang.Class({
    Name: 'GObjectImplementingLangInterface',
    Extends: GObject.Object,
    Implements: [AnInterface],
});

const AGObjectInterface = new Lang.Interface({
    Name: 'AGObjectInterface',
    GTypeName: 'ArbitraryGTypeName',
    Requires: [GObject.Object],
    Properties: {
        'interface-prop': GObject.ParamSpec.string('interface-prop',
            'Interface property', 'Must be overridden in implementation',
            GObject.ParamFlags.READABLE,
            'foobar'),
    },
    Signals: {
        'interface-signal': {},
    },

    requiredG: Lang.Interface.UNIMPLEMENTED,
    optionalG() {
        return 'AGObjectInterface.optionalG()';
    },
});

const InterfaceRequiringGObjectInterface = new Lang.Interface({
    Name: 'InterfaceRequiringGObjectInterface',
    Requires: [AGObjectInterface],

    optionalG() {
        return `InterfaceRequiringGObjectInterface.optionalG()\n${
            AGObjectInterface.optionalG(this)}`;
    },
});

const GObjectImplementingGObjectInterface = new Lang.Class({
    Name: 'GObjectImplementingGObjectInterface',
    Extends: GObject.Object,
    Implements: [AGObjectInterface],
    Properties: {
        'interface-prop': GObject.ParamSpec.override('interface-prop',
            AGObjectInterface),
        'class-prop': GObject.ParamSpec.string('class-prop', 'Class property',
            'A property that is not on the interface',
            GObject.ParamFlags.READABLE, 'meh'),
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

    requiredG() {},
    optionalG() {
        return AGObjectInterface.optionalG(this);
    },
});

const MinimalImplementationOfAGObjectInterface = new Lang.Class({
    Name: 'MinimalImplementationOfAGObjectInterface',
    Extends: GObject.Object,
    Implements: [AGObjectInterface],
    Properties: {
        'interface-prop': GObject.ParamSpec.override('interface-prop',
            AGObjectInterface),
    },

    requiredG() {},
});

const ImplementationOfTwoInterfaces = new Lang.Class({
    Name: 'ImplementationOfTwoInterfaces',
    Extends: GObject.Object,
    Implements: [AGObjectInterface, InterfaceRequiringGObjectInterface],
    Properties: {
        'interface-prop': GObject.ParamSpec.override('interface-prop',
            AGObjectInterface),
    },

    requiredG() {},
    optionalG() {
        return InterfaceRequiringGObjectInterface.optionalG(this);
    },
});

describe('GObject interface', function () {
    it('class can implement a Lang.Interface', function () {
        let obj;
        expect(() => {
            obj = new GObjectImplementingLangInterface();
        }).not.toThrow();
        expect(obj.constructor.implements(AnInterface)).toBeTruthy();
    });

    it('throws when an interface requires a GObject interface but not GObject.Object', function () {
        expect(() => new Lang.Interface({
            Name: 'GObjectInterfaceNotRequiringGObject',
            GTypeName: 'GTypeNameNotRequiringGObject',
            Requires: [Gio.Initable],
        })).toThrow();
    });

    it('can be implemented by a GObject class along with a JS interface', function () {
        const ObjectImplementingLangInterfaceAndCInterface = new Lang.Class({
            Name: 'ObjectImplementingLangInterfaceAndCInterface',
            Extends: GObject.Object,
            Implements: [AnInterface, Gio.Initable],
        });
        let obj;
        expect(() => {
            obj = new ObjectImplementingLangInterfaceAndCInterface();
        }).not.toThrow();
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

    it('has a name', function () {
        expect(AGObjectInterface.name).toEqual('AGObjectInterface');
    });

    it('reports its type name', function () {
        expect(AGObjectInterface.$gtype.name).toEqual('ArbitraryGTypeName');
    });

    it('can be implemented by a GObject class', function () {
        let obj;
        expect(() => {
            obj = new GObjectImplementingGObjectInterface();
        }).not.toThrow();
        expect(obj.constructor.implements(AGObjectInterface)).toBeTruthy();
    });

    it('is implemented by a GObject class with the correct class object', function () {
        let obj = new GObjectImplementingGObjectInterface();
        expect(obj.constructor).toEqual(GObjectImplementingGObjectInterface);
        expect(obj.constructor.name)
            .toEqual('GObjectImplementingGObjectInterface');
    });

    it('can be implemented by a class also implementing a Lang.Interface', function () {
        const GObjectImplementingBothKindsOfInterface = new Lang.Class({
            Name: 'GObjectImplementingBothKindsOfInterface',
            Extends: GObject.Object,
            Implements: [AnInterface, AGObjectInterface],
            Properties: {
                'interface-prop': GObject.ParamSpec.override('interface-prop',
                    AGObjectInterface),
            },

            required() {},
            requiredG() {},
        });
        let obj;
        expect(() => {
            obj = new GObjectImplementingBothKindsOfInterface();
        }).not.toThrow();
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
            Implements: [AGObjectInterface],
            Properties: {
                'interface-prop': GObject.ParamSpec.override('interface-prop',
                    AGObjectInterface),
            },
        })).toThrow();
    });

    it("doesn't have to have its optional function implemented", function () {
        let obj;
        expect(() => {
            obj = new MinimalImplementationOfAGObjectInterface();
        }).not.toThrow();
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
        expect(() => {
            obj = new ImplementationOfTwoInterfaces();
        }).not.toThrow();
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
            Implements: [AGObjectInterface, InterfaceRequiringGObjectInterface],
            Properties: {
                'interface-prop': GObject.ParamSpec.override('interface-prop',
                    AGObjectInterface),
            },

            requiredG() {},
        });
        let obj = new MinimalImplementationOfTwoInterfaces();
        expect(obj.optionalG())
            .toEqual('InterfaceRequiringGObjectInterface.optionalG()\nAGObjectInterface.optionalG()');
    });

    it('must be implemented by a class that implements all required interfaces', function () {
        expect(() => new Lang.Class({
            Name: 'BadObject',
            Implements: [InterfaceRequiringGObjectInterface],
            required() {},
        })).toThrow();
    });

    it('must be implemented by a class that implements required interfaces in correct order', function () {
        expect(() => new Lang.Class({
            Name: 'BadObject',
            Implements: [InterfaceRequiringGObjectInterface, AGObjectInterface],
            required() {},
        })).toThrow();
    });

    it('can require an interface from C', function () {
        const InitableInterface = new Lang.Interface({
            Name: 'InitableInterface',
            Requires: [GObject.Object, Gio.Initable],
        });
        expect(() => new Lang.Class({
            Name: 'BadObject',
            Implements: [InitableInterface],
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
            Implements: [AGObjectInterface],

            requiredG() {},
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
            Extends: GObject.Class,
        });
        const MyMetaObject = new MyMeta({
            Name: 'MyMetaObject',
        });
        const MyMetaInterface = new Lang.Interface({
            Name: 'MyMetaInterface',
            Requires: [MyMetaObject],
        });
        expect(MyMetaInterface instanceof GObject.Interface).toBeTruthy();
    });

    it('can be implemented by a class as well as its parent class', function () {
        const SubObject = new Lang.Class({
            Name: 'SubObject',
            Extends: GObjectImplementingGObjectInterface,
        });
        let obj = new SubObject();
        expect(obj.constructor.implements(AGObjectInterface)).toBeTruthy();
        expect(obj.interface_prop).toEqual('foobar');  // override not needed
    });

    it('can be reimplemented by a subclass of a class that already implements it', function () {
        const SubImplementer = new Lang.Class({
            Name: 'SubImplementer',
            Extends: GObjectImplementingGObjectInterface,
            Implements: [AGObjectInterface],
        });
        let obj = new SubImplementer();
        expect(obj.constructor.implements(AGObjectInterface)).toBeTruthy();
        expect(obj.interface_prop).toEqual('foobar');  // override not needed
    });
});

const LegacyInterface1 = new Lang.Interface({
    Name: 'LegacyInterface1',
    Requires: [GObject.Object],
    Signals: {'legacy-iface1-signal': {}},
});

const LegacyInterface2 = new Lang.Interface({
    Name: 'LegacyInterface2',
    Requires: [GObject.Object],
    Signals: {'legacy-iface2-signal': {}},
});

const Legacy = new Lang.Class({
    Name: 'Legacy',
    Extends: GObject.Object,
    Implements: [LegacyInterface1],
    Properties: {
        'property': GObject.ParamSpec.int('property', 'Property',
            'A magic property', GObject.ParamFlags.READWRITE, 0, 100, 0),
        'override-property': GObject.ParamSpec.int('override-property',
            'Override property', 'Another magic property',
            GObject.ParamFlags.READWRITE, 0, 100, 0),
    },
    Signals: {
        'signal': {},
    },

    _init(someval) {
        this.constructorCalledWith = someval;
        this.parent();
    },

    instanceMethod() {},
    chainUpToMe() {},
    overrideMe() {},

    get property() {
        return this._property + 1;
    },
    set property(value) {
        this._property = value - 2;
    },

    get overrideProperty() {
        return this._overrideProperty + 1;
    },
    set overrideProperty(value) {
        this._overrideProperty = value - 2;
    },
});
Legacy.staticMethod = function () {};

const Shiny = GObject.registerClass({
    Implements: [LegacyInterface2],
    Properties: {
        'override-property': GObject.ParamSpec.override('override-property',
            Legacy),
    },
}, class Shiny extends Legacy {
    chainUpToMe() {
        super.chainUpToMe();
    }

    overrideMe() {}

    get overrideProperty() {
        return this._overrideProperty + 2;
    }

    set overrideProperty(value) {
        this._overrideProperty = value - 1;
    }
});

describe('ES6 GObject class inheriting from GObject.Class', function () {
    let instance;

    beforeEach(function () {
        spyOn(Legacy, 'staticMethod');
        spyOn(Legacy.prototype, 'instanceMethod');
        spyOn(Legacy.prototype, 'chainUpToMe');
        spyOn(Legacy.prototype, 'overrideMe');
        instance = new Shiny();
    });

    it('calls a static method on the parent class', function () {
        Shiny.staticMethod();
        expect(Legacy.staticMethod).toHaveBeenCalled();
    });

    it('calls a method on the parent class', function () {
        instance.instanceMethod();
        expect(Legacy.prototype.instanceMethod).toHaveBeenCalled();
    });

    it("passes arguments to the parent class's constructor", function () {
        instance = new Shiny(42);
        expect(instance.constructorCalledWith).toEqual(42);
    });

    it('chains up to a method on the parent class', function () {
        instance.chainUpToMe();
        expect(Legacy.prototype.chainUpToMe).toHaveBeenCalled();
    });

    it('overrides a method on the parent class', function () {
        instance.overrideMe();
        expect(Legacy.prototype.overrideMe).not.toHaveBeenCalled();
    });

    it('sets and gets a property from the parent class', function () {
        instance.property = 42;
        expect(instance.property).toEqual(41);
    });

    it('overrides a property from the parent class', function () {
        instance.overrideProperty = 42;
        expect(instance.overrideProperty).toEqual(43);
    });

    it('inherits a signal from the parent class', function () {
        let signalSpy = jasmine.createSpy('signalSpy');
        expect(() => {
            instance.connect('signal', signalSpy);
            instance.emit('signal');
        }).not.toThrow();
        expect(signalSpy).toHaveBeenCalled();
    });

    it('inherits legacy interfaces from the parent', function () {
        expect(() => instance.emit('legacy-iface1-signal')).not.toThrow();
        expect(instance instanceof LegacyInterface1).toBeTruthy();
    });

    it('can implement a legacy interface itself', function () {
        expect(() => instance.emit('legacy-iface2-signal')).not.toThrow();
        expect(instance instanceof LegacyInterface2).toBeTruthy();
    });
});
