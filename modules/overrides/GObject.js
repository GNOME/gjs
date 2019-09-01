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
var GTypeFlags = Symbol('GType flags');
var interfaces = Symbol('GObject interfaces');
var properties = Symbol('GObject properties');
var requires = Symbol('GObject interface requires');
var signals = Symbol('GObject signals');

// These four will be aliased to GTK
var _gtkChildren = Symbol('GTK widget template children');
var _gtkCssName = Symbol('GTK widget CSS name');
var _gtkInternalChildren = Symbol('GTK widget template internal children');
var _gtkTemplate = Symbol('GTK widget template');

function registerClass(...args) {
    let klass = args[0];
    if (args.length === 2) {
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
        let metaInfo = args[0];
        klass = args[1];
        if ('GTypeName' in metaInfo)
            klass[GTypeName] = metaInfo.GTypeName;
        if ('GTypeFlags' in metaInfo)
            klass[GTypeFlags] = metaInfo.GTypeFlags;
        if ('Implements' in metaInfo)
            klass[interfaces] = metaInfo.Implements;
        if ('Properties' in metaInfo)
            klass[properties] = metaInfo.Properties;
        if ('Signals' in metaInfo)
            klass[signals] = metaInfo.Signals;
        if ('Requires' in metaInfo)
            klass[requires] = metaInfo.Requires;
        if ('CssName' in metaInfo)
            klass[_gtkCssName] = metaInfo.CssName;
        if ('Template' in metaInfo)
            klass[_gtkTemplate] = metaInfo.Template;
        if ('Children' in metaInfo)
            klass[_gtkChildren] = metaInfo.Children;
        if ('InternalChildren' in metaInfo)
            klass[_gtkInternalChildren] = metaInfo.InternalChildren;
    }

    if (!(klass.prototype instanceof GObject.Object) &&
        !(klass.prototype instanceof GObject.Interface)) {
        throw new TypeError('GObject.registerClass() used with invalid base ' +
            `class (is ${Object.getPrototypeOf(klass).name})`);
    }

    // Find the "least derived" class with a _classInit static function; there
    // definitely is one, since this class must inherit from GObject
    let initclass = klass;
    while (typeof initclass._classInit === 'undefined')
        initclass = Object.getPrototypeOf(initclass.prototype).constructor;
    return initclass._classInit(klass);
}

// Some common functions between GObject.Class and GObject.Interface

function _createSignals(gtype, sigs) {
    for (let signalName in sigs) {
        let obj = sigs[signalName];
        let flags = obj.flags !== undefined ? obj.flags : GObject.SignalFlags.RUN_FIRST;
        let accumulator = obj.accumulator !== undefined ? obj.accumulator : GObject.AccumulatorType.NONE;
        let rtype = obj.return_type !== undefined ? obj.return_type : GObject.TYPE_NONE;
        let paramtypes = obj.param_types !== undefined ? obj.param_types : [];

        try {
            obj.signal_id = Gi.signal_new(gtype, signalName, flags, accumulator, rtype, paramtypes);
        } catch (e) {
            throw new TypeError(`Invalid signal ${signalName}: ${e.message}`);
        }
    }
}

function _getCallerBasename() {
    let stackLines = new Error().stack.trim().split('\n');
    let regex = new RegExp(/@(.+:\/\/)?(.*\/)?(.+)\.js:\d+(:[\d]+)?$/);
    let thisFile = null;
    let thisDir = null;

    for (let line of stackLines) {
        let match = line.match(regex);
        if (match) {
            let scriptDir = match[2];
            let scriptBasename = match[3];

            if (!thisFile) {
                thisDir = scriptDir;
                thisFile = scriptBasename;
                continue;
            }

            if (scriptDir === thisDir && scriptBasename === thisFile)
                continue;

            if (scriptDir && scriptDir.startsWith('/org/gnome/gjs/'))
                continue;

            let basename = scriptBasename;
            if (scriptDir) {
                scriptDir = scriptDir.replace(/^\/|\/$/g, '');
                basename = `${scriptDir.split('/').reverse()[0]}_${basename}`;
            }
            return basename;
        }
    }

    return null;
}

function _createGTypeName(klass) {
    const sanitizeGType = s => s.replace(/[^a-z0-9+_-]/gi, '_');

    if (klass.hasOwnProperty(GTypeName))
        return klass[GTypeName];

    let callerBasename = _getCallerBasename() || '';
    if (callerBasename)
        callerBasename = `${sanitizeGType(callerBasename)}_`;

    return `Gjs_${callerBasename}${sanitizeGType(klass.name)}`;
}

function _propertiesAsArray(klass) {
    let propertiesArray = [];
    if (klass.hasOwnProperty(properties)) {
        for (let prop in klass[properties])
            propertiesArray.push(klass[properties][prop]);
    }
    return propertiesArray;
}

function _copyAllDescriptors(target, source, filter) {
    Object.getOwnPropertyNames(source)
    .filter(key => !['prototype', 'constructor'].concat(filter).includes(key))
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
        return (!_interfacePresent(required, proto.constructor) ||
            ifaces.indexOf(required) > ifaces.indexOf(iface)) &&
            !(proto instanceof required);
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
        obj[`TYPE_${upperName}`] = gtype;
        obj[name] = function (v) {
            return new actual(v);
        };
        obj[name].$gtype = gtype;
    }

    _makeDummyClass(GObject, 'VoidType', 'NONE', 'void', function () {});
    _makeDummyClass(GObject, 'Char', 'CHAR', 'gchar', Number);
    _makeDummyClass(GObject, 'UChar', 'UCHAR', 'guchar', Number);
    _makeDummyClass(GObject, 'Unichar', 'UNICHAR', 'gint', String);

    GObject.TYPE_BOOLEAN = GObject.type_from_name('gboolean');
    GObject.Boolean = Boolean;
    Boolean.$gtype = GObject.TYPE_BOOLEAN;

    _makeDummyClass(GObject, 'Int', 'INT', 'gint', Number);
    _makeDummyClass(GObject, 'UInt', 'UINT', 'guint', Number);
    _makeDummyClass(GObject, 'Long', 'LONG', 'glong', Number);
    _makeDummyClass(GObject, 'ULong', 'ULONG', 'gulong', Number);
    _makeDummyClass(GObject, 'Int64', 'INT64', 'gint64', Number);
    _makeDummyClass(GObject, 'UInt64', 'UINT64', 'guint64', Number);

    GObject.TYPE_ENUM = GObject.type_from_name('GEnum');
    GObject.TYPE_FLAGS = GObject.type_from_name('GFlags');

    _makeDummyClass(GObject, 'Float', 'FLOAT', 'gfloat', Number);
    GObject.TYPE_DOUBLE = GObject.type_from_name('gdouble');
    GObject.Double = Number;
    Number.$gtype = GObject.TYPE_DOUBLE;

    GObject.TYPE_STRING = GObject.type_from_name('gchararray');
    GObject.String = String;
    String.$gtype = GObject.TYPE_STRING;

    GObject.TYPE_POINTER = GObject.type_from_name('gpointer');
    GObject.TYPE_BOXED = GObject.type_from_name('GBoxed');
    GObject.TYPE_PARAM = GObject.type_from_name('GParam');
    GObject.TYPE_INTERFACE = GObject.type_from_name('GInterface');
    GObject.TYPE_OBJECT = GObject.type_from_name('GObject');
    GObject.TYPE_VARIANT = GObject.type_from_name('GVariant');

    _makeDummyClass(GObject, 'Type', 'GTYPE', 'GType', GObject.type_from_name);

    GObject.ParamSpec.char = function (name, nick, blurb, flags, minimum, maximum, defaultValue) {
        return GObject.param_spec_char(name, nick, blurb, minimum, maximum, defaultValue, flags);
    };

    GObject.ParamSpec.uchar = function (name, nick, blurb, flags, minimum, maximum, defaultValue) {
        return GObject.param_spec_uchar(name, nick, blurb, minimum, maximum, defaultValue, flags);
    };

    GObject.ParamSpec.int = function (name, nick, blurb, flags, minimum, maximum, defaultValue) {
        return GObject.param_spec_int(name, nick, blurb, minimum, maximum, defaultValue, flags);
    };

    GObject.ParamSpec.uint = function (name, nick, blurb, flags, minimum, maximum, defaultValue) {
        return GObject.param_spec_uint(name, nick, blurb, minimum, maximum, defaultValue, flags);
    };

    GObject.ParamSpec.long = function (name, nick, blurb, flags, minimum, maximum, defaultValue) {
        return GObject.param_spec_long(name, nick, blurb, minimum, maximum, defaultValue, flags);
    };

    GObject.ParamSpec.ulong = function (name, nick, blurb, flags, minimum, maximum, defaultValue) {
        return GObject.param_spec_ulong(name, nick, blurb, minimum, maximum, defaultValue, flags);
    };

    GObject.ParamSpec.int64 = function (name, nick, blurb, flags, minimum, maximum, defaultValue) {
        return GObject.param_spec_int64(name, nick, blurb, minimum, maximum, defaultValue, flags);
    };

    GObject.ParamSpec.uint64 = function (name, nick, blurb, flags, minimum, maximum, defaultValue) {
        return GObject.param_spec_uint64(name, nick, blurb, minimum, maximum, defaultValue, flags);
    };

    GObject.ParamSpec.float = function (name, nick, blurb, flags, minimum, maximum, defaultValue) {
        return GObject.param_spec_float(name, nick, blurb, minimum, maximum, defaultValue, flags);
    };

    GObject.ParamSpec.boolean = function (name, nick, blurb, flags, defaultValue) {
        return GObject.param_spec_boolean(name, nick, blurb, defaultValue, flags);
    };

    GObject.ParamSpec.flags = function (name, nick, blurb, flags, flagsType, defaultValue) {
        return GObject.param_spec_flags(name, nick, blurb, flagsType, defaultValue, flags);
    };

    GObject.ParamSpec.enum = function (name, nick, blurb, flags, enumType, defaultValue) {
        return GObject.param_spec_enum(name, nick, blurb, enumType, defaultValue, flags);
    };

    GObject.ParamSpec.double = function (name, nick, blurb, flags, minimum, maximum, defaultValue) {
        return GObject.param_spec_double(name, nick, blurb, minimum, maximum, defaultValue, flags);
    };

    GObject.ParamSpec.string = function (name, nick, blurb, flags, defaultValue) {
        return GObject.param_spec_string(name, nick, blurb, defaultValue, flags);
    };

    GObject.ParamSpec.boxed = function (name, nick, blurb, flags, boxedType) {
        return GObject.param_spec_boxed(name, nick, blurb, boxedType, flags);
    };

    GObject.ParamSpec.object = function (name, nick, blurb, flags, objectType) {
        return GObject.param_spec_object(name, nick, blurb, objectType, flags);
    };

    GObject.ParamSpec.param = function (name, nick, blurb, flags, paramType) {
        return GObject.param_spec_param(name, nick, blurb, paramType, flags);
    };

    GObject.ParamSpec.override = Gi.override_property;

    Object.defineProperties(GObject.ParamSpec.prototype, {
        'name': {
            configurable: false,
            enumerable: false,
            get() {
                return this.get_name();
            },
        },
        '_nick': {
            configurable: false,
            enumerable: false,
            get() {
                return this.get_nick();
            },
        },
        'nick': {
            configurable: false,
            enumerable: false,
            get() {
                return this.get_nick();
            },
        },
        '_blurb': {
            configurable: false,
            enumerable: false,
            get() {
                return this.get_blurb();
            },
        },
        'blurb': {
            configurable: false,
            enumerable: false,
            get() {
                return this.get_blurb();
            },
        },
        'default_value': {
            configurable: false,
            enumerable: false,
            get() {
                return this.get_default_value();
            },
        },
        'flags': {
            configurable: false,
            enumerable: false,
            get() {
                return GjsPrivate.param_spec_get_flags(this);
            },
        },
        'value_type': {
            configurable: false,
            enumerable: false,
            get() {
                return GjsPrivate.param_spec_get_value_type(this);
            },
        },
        'owner_type': {
            configurable: false,
            enumerable: false,
            get() {
                return GjsPrivate.param_spec_get_owner_type(this);
            },
        },
    });

    let {GObjectMeta, GObjectInterface} = Legacy.defineGObjectLegacyObjects(GObject);
    GObject.Class = GObjectMeta;
    GObject.Interface = GObjectInterface;
    GObject.Object.prototype.__metaclass__ = GObject.Class;

    // For compatibility with Lang.Class... we need a _construct
    // or the Lang.Class constructor will fail.
    GObject.Object.prototype._construct = function (...args) {
        this._init(...args);
        return this;
    };

    GObject.registerClass = registerClass;

    GObject.Object._classInit = function (klass) {
        let gtypename = _createGTypeName(klass);
        let gflags = klass.hasOwnProperty(GTypeFlags) ? klass[GTypeFlags] : 0;
        let gobjectInterfaces = klass.hasOwnProperty(interfaces) ? klass[interfaces] : [];
        let propertiesArray = _propertiesAsArray(klass);
        let parent = Object.getPrototypeOf(klass);
        let gobjectSignals = klass.hasOwnProperty(signals) ? klass[signals] : [];

        let newClass = Gi.register_type(parent.prototype, gtypename, gflags,
            gobjectInterfaces, propertiesArray);
        Object.setPrototypeOf(newClass, parent);

        _createSignals(newClass.$gtype, gobjectSignals);

        _copyAllDescriptors(newClass, klass);
        gobjectInterfaces.forEach(iface =>
            _copyAllDescriptors(newClass.prototype, iface.prototype,
                ['toString']));
        _copyAllDescriptors(newClass.prototype, klass.prototype);

        Object.getOwnPropertyNames(newClass.prototype)
        .filter(name => name.startsWith('vfunc_') || name.startsWith('on_'))
        .forEach(name => {
            let descr = Object.getOwnPropertyDescriptor(newClass.prototype, name);
            if (typeof descr.value !== 'function')
                return;

            let func = newClass.prototype[name];

            if (name.startsWith('vfunc_')) {
                newClass.prototype[Gi.hook_up_vfunc_symbol](name.slice(6), func);
            } else if (name.startsWith('on_')) {
                let id = GObject.signal_lookup(name.slice(3).replace('_', '-'),
                    newClass.$gtype);
                if (id !== 0) {
                    GObject.signal_override_class_closure(id, newClass.$gtype, function (...argArray) {
                        let emitter = argArray.shift();

                        return func.apply(emitter, argArray);
                    });
                }
            }
        });

        gobjectInterfaces.forEach(iface =>
            _checkInterface(iface, newClass.prototype));

        // For backwards compatibility only. Use instanceof instead.
        newClass.implements = function (iface) {
            if (iface.$gtype)
                return GObject.type_is_a(newClass.$gtype, iface.$gtype);
            return false;
        };

        return newClass;
    };

    GObject.Interface._classInit = function (klass) {
        let gtypename = _createGTypeName(klass);
        let gobjectInterfaces = klass.hasOwnProperty(requires) ? klass[requires] : [];
        let props = _propertiesAsArray(klass);
        let gobjectSignals = klass.hasOwnProperty(signals) ? klass[signals] : [];

        let newInterface = Gi.register_interface(gtypename, gobjectInterfaces,
            props);

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
                newInterface[key] = function (thisObj, ...args) {
                    return interfaceProto[key].call(thisObj, ...args);
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
        get name() {
            return 'NotImplementedError';
        }
    };

    // These will be copied in the Gtk overrides
    // Use __X__ syntax to indicate these variables should not be used publicly.

    GObject.__gtkCssName__ = _gtkCssName;
    GObject.__gtkTemplate__ = _gtkTemplate;
    GObject.__gtkChildren__ = _gtkChildren;
    GObject.__gtkInternalChildren__ = _gtkInternalChildren;

    // Expose GObject static properties for ES6 classes

    GObject.GTypeName = GTypeName;
    GObject.requires = requires;
    GObject.interfaces = interfaces;
    GObject.properties = properties;
    GObject.signals = signals;

    // Replacement for non-introspectable g_object_set()
    GObject.Object.prototype.set = function (params) {
        Object.assign(this, params);
    };

    // fake enum for signal accumulators, keep in sync with gi/object.c
    GObject.AccumulatorType = {
        NONE: 0,
        FIRST_WINS: 1,
        TRUE_HANDLED: 2,
    };

    GObject.Object.prototype.disconnect = function (id) {
        return GObject.signal_handler_disconnect(this, id);
    };

    // A simple workaround if you have a class with .connect, .disconnect or .emit
    // methods (such as Gio.Socket.connect or NMClient.Device.disconnect)
    // The original g_signal_* functions are not introspectable anyway, because
    // we need our own handling of signal argument marshalling
    GObject.signal_connect = function (object, name, handler) {
        return GObject.Object.prototype.connect.call(object, name, handler);
    };
    GObject.signal_connect_after = function (object, name, handler) {
        return GObject.Object.prototype.connect_after.call(object, name, handler);
    };
    GObject.signal_emit_by_name = function (object, ...nameAndArgs) {
        return GObject.Object.prototype.emit.apply(object, nameAndArgs);
    };
}
