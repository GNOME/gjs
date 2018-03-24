/* exported _init, interfaces, properties, registerClass, requires, signals */
// Copyright 2011 Jasper St. Pierre
// Copyright 2017 Philip Chimento <philip.chimento@gmail.com>, <philip@endlessm.com>
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to
// deal in the Software without restriction, including without limitation the
// rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.

const Gi = imports._gi;
const GjsPrivate = imports.gi.GjsPrivate;
const Legacy = imports._legacy;

let GObject;

var GTypeName = Symbol('GType name');
var interfaces = Symbol('GObject interfaces');
var properties = Symbol('GObject properties');
var requires = Symbol('GObject interface requires');
var signals = Symbol('GObject signals');

// These four will be aliased to GTK
var _children = Symbol('GTK widget template children');
var _cssName = Symbol('GTK widget CSS name');
var _internalChildren = Symbol('GTK widget template internal children');
var _template = Symbol('GTK widget template');

function registerClass(klass) {
    if (arguments.length == 2) {
        // The two-argument form is the convenient syntax without ESnext
        // decorators and class data properties. The first argument is an
        // object with meta info such as properties and signals. The second
        // argument is the class expression for the class itself.
        //
        //     var MyClass = GObject.registerClass({
        //         Properties: { ... },
        //         Signals: { ... },
        //     }, class MyClass extends GObject.Object {
        //         _init() { ... }
        //     });
        //
        // When decorators and class data properties become part of the JS
        // standard, this function can be used directly as a decorator.
        let metaInfo = arguments[0];
        klass = arguments[1];
        if ('GTypeName' in metaInfo)
            klass[GTypeName] = metaInfo.GTypeName;
        if ('Implements' in metaInfo)
            klass[interfaces] = metaInfo.Implements;
        if ('Properties' in metaInfo)
            klass[properties] = metaInfo.Properties;
        if ('Signals' in metaInfo)
            klass[signals] = metaInfo.Signals;
        if ('Requires' in metaInfo)
            klass[requires] = metaInfo.Requires;
        if ('CssName' in metaInfo)
            klass[_cssName] = metaInfo.CssName;
        if ('Template' in metaInfo)
            klass[_template] = metaInfo.Template;
        if ('Children' in metaInfo)
            klass[_children] = metaInfo.Children;
        if ('InternalChildren' in metaInfo)
            klass[_internalChildren] = metaInfo.InternalChildren;
    }

    if (!(klass.prototype instanceof GObject.Object) &&
        !(klass.prototype instanceof GObject.Interface))
        throw new TypeError('GObject.registerClass() used with invalid base ' +
            `class (is ${Object.getPrototypeOf(klass).name})`);

    // Find the "least derived" class with a _classInit static function; there
    // definitely is one, since this class must inherit from GObject
    let initclass = klass;
    while (typeof initclass._classInit === 'undefined')
        initclass = Object.getPrototypeOf(initclass.prototype).constructor;
    return initclass._classInit(klass);
}

// Some common functions between GObject.Class and GObject.Interface

function _createSignals(gtype, signals) {
    for (let signalName in signals) {
        let obj = signals[signalName];
        let flags = (obj.flags !== undefined) ? obj.flags : GObject.SignalFlags.RUN_FIRST;
        let accumulator = (obj.accumulator !== undefined) ? obj.accumulator : GObject.AccumulatorType.NONE;
        let rtype = (obj.return_type !== undefined) ? obj.return_type : GObject.TYPE_NONE;
        let paramtypes = (obj.param_types !== undefined) ? obj.param_types : [];

        try {
            obj.signal_id = Gi.signal_new(gtype, signalName, flags, accumulator, rtype, paramtypes);
        } catch (e) {
            throw new TypeError('Invalid signal ' + signalName + ': ' + e.message);
        }
    }
}

function _createGTypeName(klass) {
    if (klass.hasOwnProperty(GTypeName))
        return klass[GTypeName];
    return `Gjs_${klass.name}`;
}

function _propertiesAsArray(klass) {
    let propertiesArray = [];
    if (klass.hasOwnProperty(properties)) {
        for (let prop in klass[properties]) {
            propertiesArray.push(klass[properties][prop]);
        }
    }
    return propertiesArray;
}

function _copyAllDescriptors(target, source) {
    Object.getOwnPropertyNames(source)
    .filter(key => !['prototype', 'constructor'].includes(key))
    .concat(Object.getOwnPropertySymbols(source))
    .forEach(key => {
        let descriptor = Object.getOwnPropertyDescriptor(source, key);
        Object.defineProperty(target, key, descriptor);
    });
}

function _interfacePresent(required, klass) {
    if (!klass[interfaces])
        return false;
    if (klass[interfaces].includes(required))
        return true;  // implemented here
    // Might be implemented on a parent class
    return _interfacePresent(required, Object.getPrototypeOf(klass));
}

function _checkInterface(iface, proto) {
    // Check that proto implements all of this interface's required interfaces.
    // "proto" refers to the object's prototype (which implements the interface)
    // whereas "iface.prototype" is the interface's prototype (which may still
    // contain unimplemented methods.)
    if (typeof iface[requires] === 'undefined')
        return;

    let unfulfilledReqs = iface[requires].filter(required => {
        // Either the interface is not present or it is not listed before the
        // interface that requires it or the class does not inherit it. This is
        // so that required interfaces don't copy over properties from other
        // interfaces that require them.
        let ifaces = proto.constructor[interfaces];
        return ((!_interfacePresent(required, proto.constructor) ||
            ifaces.indexOf(required) > ifaces.indexOf(iface)) &&
            !(proto instanceof required));
    }).map(required =>
        // required.name will be present on JS classes, but on introspected
        // GObjects it will be the C name. The alternative is just so that
        // we print something if there is garbage in Requires.
        required.name || required);
    if (unfulfilledReqs.length > 0) {
        throw new Error('The following interfaces must be implemented before ' +
            `${iface.name}: ${unfulfilledReqs.join(', ')}`);
    }
}

function _init() {

    GObject = this;

    function _makeDummyClass(obj, name, upperName, gtypeName, actual) {
        let gtype = GObject.type_from_name(gtypeName);
        obj['TYPE_' + upperName] = gtype;
        obj[name] = function(v) { return new actual(v); };
        obj[name].$gtype = gtype;
    }

    _makeDummyClass(this, 'VoidType', 'NONE', 'void', function() {});
    _makeDummyClass(this, 'Char', 'CHAR', 'gchar', Number);
    _makeDummyClass(this, 'UChar', 'UCHAR', 'guchar', Number);
    _makeDummyClass(this, 'Unichar', 'UNICHAR', 'gint', String);

    this.TYPE_BOOLEAN = GObject.type_from_name('gboolean');
    this.Boolean = Boolean;
    Boolean.$gtype = this.TYPE_BOOLEAN;

    _makeDummyClass(this, 'Int', 'INT', 'gint', Number);
    _makeDummyClass(this, 'UInt', 'UINT', 'guint', Number);
    _makeDummyClass(this, 'Long', 'LONG', 'glong', Number);
    _makeDummyClass(this, 'ULong', 'ULONG', 'gulong', Number);
    _makeDummyClass(this, 'Int64', 'INT64', 'gint64', Number);
    _makeDummyClass(this, 'UInt64', 'UINT64', 'guint64', Number);

    this.TYPE_ENUM = GObject.type_from_name('GEnum');
    this.TYPE_FLAGS = GObject.type_from_name('GFlags');

    _makeDummyClass(this, 'Float', 'FLOAT', 'gfloat', Number);
    this.TYPE_DOUBLE = GObject.type_from_name('gdouble');
    this.Double = Number;
    Number.$gtype = this.TYPE_DOUBLE;

    this.TYPE_STRING = GObject.type_from_name('gchararray');
    this.String = String;
    String.$gtype = this.TYPE_STRING;

    this.TYPE_POINTER = GObject.type_from_name('gpointer');
    this.TYPE_BOXED = GObject.type_from_name('GBoxed');
    this.TYPE_PARAM = GObject.type_from_name('GParam');
    this.TYPE_INTERFACE = GObject.type_from_name('GInterface');
    this.TYPE_OBJECT = GObject.type_from_name('GObject');
    this.TYPE_VARIANT = GObject.type_from_name('GVariant');

    _makeDummyClass(this, 'Type', 'GTYPE', 'GType', GObject.type_from_name);

    this.ParamSpec.char = function(name, nick, blurb, flags, minimum, maximum, default_value) {
        return GObject.param_spec_char(name, nick, blurb, minimum, maximum, default_value, flags);
    };

    this.ParamSpec.uchar = function(name, nick, blurb, flags, minimum, maximum, default_value) {
        return GObject.param_spec_uchar(name, nick, blurb, minimum, maximum, default_value, flags);
    };

    this.ParamSpec.int = function(name, nick, blurb, flags, minimum, maximum, default_value) {
        return GObject.param_spec_int(name, nick, blurb, minimum, maximum, default_value, flags);
    };

    this.ParamSpec.uint = function(name, nick, blurb, flags, minimum, maximum, default_value) {
        return GObject.param_spec_uint(name, nick, blurb, minimum, maximum, default_value, flags);
    };

    this.ParamSpec.long = function(name, nick, blurb, flags, minimum, maximum, default_value) {
        return GObject.param_spec_long(name, nick, blurb, minimum, maximum, default_value, flags);
    };

    this.ParamSpec.ulong = function(name, nick, blurb, flags, minimum, maximum, default_value) {
        return GObject.param_spec_ulong(name, nick, blurb, minimum, maximum, default_value, flags);
    };

    this.ParamSpec.int64 = function(name, nick, blurb, flags, minimum, maximum, default_value) {
        return GObject.param_spec_int64(name, nick, blurb, minimum, maximum, default_value, flags);
    };

    this.ParamSpec.uint64 = function(name, nick, blurb, flags, minimum, maximum, default_value) {
        return GObject.param_spec_uint64(name, nick, blurb, minimum, maximum, default_value, flags);
    };

    this.ParamSpec.float = function(name, nick, blurb, flags, minimum, maximum, default_value) {
        return GObject.param_spec_float(name, nick, blurb, minimum, maximum, default_value, flags);
    };

    this.ParamSpec.boolean = function(name, nick, blurb, flags, default_value) {
        return GObject.param_spec_boolean(name, nick, blurb, default_value, flags);
    };

    this.ParamSpec.flags = function(name, nick, blurb, flags, flags_type, default_value) {
        return GObject.param_spec_flags(name, nick, blurb, flags_type, default_value, flags);
    };

    this.ParamSpec.enum = function(name, nick, blurb, flags, enum_type, default_value) {
        return GObject.param_spec_enum(name, nick, blurb, enum_type, default_value, flags);
    };

    this.ParamSpec.double = function(name, nick, blurb, flags, minimum, maximum, default_value) {
        return GObject.param_spec_double(name, nick, blurb, minimum, maximum, default_value, flags);
    };

    this.ParamSpec.string = function(name, nick, blurb, flags, default_value) {
        return GObject.param_spec_string(name, nick, blurb, default_value, flags);
    };

    this.ParamSpec.boxed = function(name, nick, blurb, flags, boxed_type) {
        return GObject.param_spec_boxed(name, nick, blurb, boxed_type, flags);
    };

    this.ParamSpec.object = function(name, nick, blurb, flags, object_type) {
        return GObject.param_spec_object(name, nick, blurb, object_type, flags);
    };

    this.ParamSpec.param = function(name, nick, blurb, flags, param_type) {
        return GObject.param_spec_param(name, nick, blurb, param_type, flags);
    };

    this.ParamSpec.override = Gi.override_property;

    Object.defineProperties(this.ParamSpec.prototype, {
        'name': { configurable: false,
                  enumerable: false,
                  get: function() { return this.get_name() } },
        '_nick': { configurable: false,
                   enumerable: false,
                   get: function() { return this.get_nick() } },
        'nick': { configurable: false,
                  enumerable: false,
                  get: function() { return this.get_nick() } },
        '_blurb': { configurable: false,
                    enumerable: false,
                    get: function() { return this.get_blurb() } },
        'blurb': { configurable: false,
                   enumerable: false,
                   get: function() { return this.get_blurb() } },
        'default_value': { configurable: false,
                           enumerable: false,
                           get: function() { return this.get_default_value() } },
        'flags':  { configurable: false,
                    enumerable: false,
                    get: function() { return GjsPrivate.param_spec_get_flags(this) } },
        'value_type':  { configurable: false,
                         enumerable: false,
                         get: function() { return GjsPrivate.param_spec_get_value_type(this) } },
        'owner_type':  { configurable: false,
                         enumerable: false,
                         get: function() { return GjsPrivate.param_spec_get_owner_type(this) } },
    });

    let {GObjectMeta, GObjectInterface} = Legacy.defineGObjectLegacyObjects(GObject);
    this.Class = GObjectMeta;
    this.Interface = GObjectInterface;
    this.Object.prototype.__metaclass__ = this.Class;

    // For compatibility with Lang.Class... we need a _construct
    // or the Lang.Class constructor will fail.
    this.Object.prototype._construct = function() {
        this._init.apply(this, arguments);
        return this;
    };

    GObject.registerClass = registerClass;

    GObject.Object._classInit = function(klass) {
        let gtypename = _createGTypeName(klass);
        let gobjectInterfaces = klass.hasOwnProperty(interfaces) ?
            klass[interfaces] : [];
        let propertiesArray = _propertiesAsArray(klass);
        let parent = Object.getPrototypeOf(klass);
        let gobjectSignals = klass.hasOwnProperty(signals) ?
            klass[signals] : [];

        let newClass = Gi.register_type(parent.prototype, gtypename,
            gobjectInterfaces, propertiesArray);
        Object.setPrototypeOf(newClass, parent);

        _createSignals(newClass.$gtype, gobjectSignals);

        _copyAllDescriptors(newClass, klass);
        gobjectInterfaces.forEach(iface =>
            _copyAllDescriptors(newClass.prototype, iface.prototype));
        _copyAllDescriptors(newClass.prototype, klass.prototype);

        Object.getOwnPropertyNames(newClass.prototype)
        .filter(name => name.startsWith('vfunc_') || name.startsWith('on_'))
        .forEach(name => {
            let descr = Object.getOwnPropertyDescriptor(newClass.prototype, name);
            if (typeof descr.value !== 'function')
                return;

            let func = newClass.prototype[name];

            if (name.startsWith('vfunc_')) {
                Gi.hook_up_vfunc(newClass.prototype, name.slice(6), func);
            } else if (name.startsWith('on_')) {
                let id = GObject.signal_lookup(name.slice(3).replace('_', '-'),
                    newClass.$gtype);
                if (id !== 0) {
                    GObject.signal_override_class_closure(id, newClass.$gtype, function() {
                        let argArray = Array.from(arguments);
                        let emitter = argArray.shift();

                        return func.apply(emitter, argArray);
                    });
                }
            }
        });

        gobjectInterfaces.forEach(iface =>
            _checkInterface(iface, newClass.prototype));

        // For backwards compatibility only. Use instanceof instead.
        newClass.implements = function(iface) {
            if (iface.$gtype)
                return GObject.type_is_a(newClass.$gtype, iface.$gtype);
            return false;
        };

        return newClass;
    };

    GObject.Interface._classInit = function(klass) {
        let gtypename = _createGTypeName(klass);
        let gobjectInterfaces = klass.hasOwnProperty(requires) ?
            klass[requires] : [];
        let properties = _propertiesAsArray(klass);
        let gobjectSignals = klass.hasOwnProperty(signals) ?
            klass[signals] : [];

        let newInterface = Gi.register_interface(gtypename, gobjectInterfaces,
            properties);

        _createSignals(newInterface.$gtype, gobjectSignals);

        _copyAllDescriptors(newInterface, klass);

        Object.getOwnPropertyNames(klass.prototype)
        .filter(key => key !== 'constructor')
        .concat(Object.getOwnPropertySymbols(klass.prototype))
        .forEach(key => {
            let descr = Object.getOwnPropertyDescriptor(klass.prototype, key);

            // Create wrappers on the interface object so that generics work (e.g.
            // SomeInterface.some_function(this, blah) instead of
            // SomeInterface.prototype.some_function.call(this, blah)
            if (typeof descr.value === 'function') {
                let interfaceProto = klass.prototype;  // capture in closure
                newInterface[key] = function () {
                    return interfaceProto[key].call.apply(interfaceProto[key],
                        arguments);
                };
            }

            Object.defineProperty(newInterface.prototype, key, descr);
        });

        return newInterface;
    };

    /**
     * Use this to signify a function that must be overridden in an
     * implementation of the interface.
     */
    GObject.NotImplementedError = class NotImplementedError extends Error {
        get name() { return 'NotImplementedError'; }
    };

    GObject._cssName = _cssName;
    GObject._template = _template;
    GObject._children = _children;
    GObject._internalChildren = _internalChildren;

    // fake enum for signal accumulators, keep in sync with gi/object.c
    this.AccumulatorType = {
        NONE: 0,
        FIRST_WINS: 1,
        TRUE_HANDLED: 2
    };

    this.Object.prototype.disconnect = function(id) {
        return GObject.signal_handler_disconnect(this, id);
    };

    // A simple workaround if you have a class with .connect, .disconnect or .emit
    // methods (such as Gio.Socket.connect or NMClient.Device.disconnect)
    // The original g_signal_* functions are not introspectable anyway, because
    // we need our own handling of signal argument marshalling
    this.signal_connect = function(object, name, handler) {
        return GObject.Object.prototype.connect.call(object, name, handler);
    };
    this.signal_connect_after = function(object, name, handler) {
        return GObject.Object.prototype.connect_after.call(object, name, handler);
    };
    this.signal_emit_by_name = function(object, ...nameAndArgs) {
        return GObject.Object.prototype.emit.apply(object, nameAndArgs);
    };
}
