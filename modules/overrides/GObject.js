// application/javascript;version=1.8
// Copyright 2011 Jasper St. Pierre
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

const Lang = imports.lang;
const Gi = imports._gi;

let GObject;

const GObjectMeta = new Lang.Class({
    Name: 'GObjectClass',
    Extends: Lang.Class,

    _init: function(params) {
        if (!params.Extends)
            params.Extends = GObject.Object;

        if (!this._isValidClass(params.Extends))
            throw new TypeError('GObject.Class used with invalid base class (is ' + params.Extends.prototype + ')');

        this.parent(params);

        Gi.register_type(params.Extends.prototype, this.prototype, params.Name);

        if (params.Properties) {
            for (let prop in params.Properties) {
                Gi.register_property(this.prototype, params.Properties[prop]);
            }
        }

        if (params.Signals) {
            for (let signalName in params.Signals) {
                let obj = params.Signals[signalName];
                let flags = (obj.flags !== undefined) ? obj.flags : GObject.SignalFlags.RUN_FIRST;
                let accumulator = (obj.accumulator !== undefined) ? obj.accumulator : GObject.AccumulatorType.NONE;
                let rtype = (obj.return_type !== undefined) ? obj.return_type : GObject.TYPE_NONE;
                let paramtypes = (obj.param_types !== undefined) ? obj.param_types : [];

                try {
                    obj.signal_id = Gi.signal_new(this.prototype, signal_name, flags, accumulator, rtype, paramtypes);
                } catch(e) {
                    throw new TypeError('Invalid signal ' + signal_name + ': ' + e.message);
                }
            }
        }

        if (params.Implements) {
            for (let i = 0; i < params.Implements.length; i++)
                Gi.add_interface(this.prototype, ifaces[i]);
        }

        delete params.Properties;
        delete params.Signals;
        delete params.Implements;

        for (let prop in params) {
            let value = this.prototype[prop];
            if (typeof value === 'function') {
                if (prop.slice(0, 6) == 'vfunc_') {
                    Gi.hook_up_vfunc(this.prototype, prop.slice(6), value);
                } else if (prop.slice(0, 3) == 'on_') {
                    let id = GObject.signal_lookup(prop.slice(3).replace('_', '-'), this.$gtype);
                    if (id != 0) {
                        GObject.signal_override_class_closure(id, this.$gtype, function() {
                            let argArray = Array.prototype.slice.call(arguments);
                            let emitter = argArray.shift();

                            value.apply(emitter, argArray);
                        });
                    }
                }
            }
        }
    },

    _isValidClass: function(klass) {
        let proto = klass.prototype;

        if (!proto)
            return false;

        // If proto == GObject.Object.prototype, then
        // proto.__proto__ is Object, so "proto instanceof GObject.Object"
        // will return false.
        return proto == GObject.Object.prototype ||
            proto instanceof GObject.Object;
    }
});

function _init() {

    GObject = this;

    this.TYPE_NONE = GObject.type_from_name('void');
    this.TYPE_INTERFACE = GObject.type_from_name('GInterface');
    this.TYPE_CHAR = GObject.type_from_name('gchar');
    this.TYPE_UCHAR = GObject.type_from_name('guchar');
    this.TYPE_BOOLEAN = GObject.type_from_name('gboolean');
    this.TYPE_INT = GObject.type_from_name('gint');
    this.TYPE_UINT = GObject.type_from_name('guint');
    this.TYPE_LONG = GObject.type_from_name('glong');
    this.TYPE_ULONG = GObject.type_from_name('gulong');
    this.TYPE_INT64 = GObject.type_from_name('gint64');
    this.TYPE_UINT64 = GObject.type_from_name('guint64');
    this.TYPE_ENUM = GObject.type_from_name('GEnum');
    this.TYPE_FLAGS = GObject.type_from_name('GFlags');
    this.TYPE_FLOAT = GObject.type_from_name('gfloat');
    this.TYPE_DOUBLE = GObject.type_from_name('gdouble');
    this.TYPE_STRING = GObject.type_from_name('gchararray');
    this.TYPE_POINTER = GObject.type_from_name('gpointer');
    this.TYPE_BOXED = GObject.type_from_name('GBoxed');
    this.TYPE_PARAM = GObject.type_from_name('GParam');
    this.TYPE_OBJECT = GObject.type_from_name('GObject');
    this.TYPE_UNICHAR = this.TYPE_UINT;

    this.ParamSpec.char = function(name, nick, blurb, flags, minimum, maximum, default_value) {
        return GObject.ParamSpec._new_internal(name, GObject.TYPE_CHAR,
                                               nick, blurb, flags, minimum, maximum, default_value);
    };

    this.ParamSpec.uchar = function(name, nick, blurb, flags, minimum, maximum, default_value) {
        return GObject.ParamSpec._new_internal(name, GObject.TYPE_UCHAR,
                                               nick, blurb, flags, minimum, maximum, default_value);
    };

    this.ParamSpec.int = function(name, nick, blurb, flags, minimum, maximum, default_value) {
        return GObject.ParamSpec._new_internal(name, GObject.TYPE_INT,
                                               nick, blurb, flags, minimum, maximum, default_value);
    };

    this.ParamSpec.uint = function(name, nick, blurb, flags, minimum, maximum, default_value) {
        return GObject.ParamSpec._new_internal(name, GObject.TYPE_UINT,
                                               nick, blurb, flags, minimum, maximum, default_value);
    };

    this.ParamSpec.long = function(name, nick, blurb, flags, minimum, maximum, default_value) {
        return GObject.ParamSpec._new_internal(name, GObject.TYPE_LONG,
                                               nick, blurb, flags, minimum, maximum, default_value);
    };

    this.ParamSpec.ulong = function(name, nick, blurb, flags, minimum, maximum, default_value) {
        return GObject.ParamSpec._new_internal(name, GObject.TYPE_ULONG,
                                               nick, blurb, flags, minimum, maximum, default_value);
    };

    this.ParamSpec.int64 = function(name, nick, blurb, flags, minimum, maximum, default_value) {
        return GObject.ParamSpec._new_internal(name, GObject.TYPE_INT64,
                                               nick, blurb, flags, minimum, maximum, default_value);
    };

    this.ParamSpec.uint64 = function(name, nick, blurb, flags, minimum, maximum, default_value) {
        return GObject.ParamSpec._new_internal(name, GObject.TYPE_UINT64,
                                               nick, blurb, flags, minimum, maximum, default_value);
    };

    this.ParamSpec.float = function(name, nick, blurb, flags, minimum, maximum, default_value) {
        return GObject.ParamSpec._new_internal(name, GObject.TYPE_FLOAT,
                                               nick, blurb, flags, minimum, maximum, default_value);
    };

    this.ParamSpec.boolean = function(name, nick, blurb, flags, default_value) {
        return GObject.ParamSpec._new_internal(name, GObject.TYPE_BOOLEAN,
                                               nick, blurb, flags, default_value);
    };

    this.ParamSpec.flags = function(name, nick, blurb, flags, flags_type, default_value) {
        return GObject.ParamSpec._new_internal(name, GObject.TYPE_FLAGS,
                                               nick, blurb, flags, flags_type, default_value);
    };

    this.ParamSpec.enum = function(name, nick, blurb, flags, enum_type, default_value) {
        return GObject.ParamSpec._new_internal(name, GObject.TYPE_ENUM,
                                               nick, blurb, flags, enum_type, default_value);
    };

    this.ParamSpec.double = function(name, nick, blurb, flags, minimum, maximum, default_value) {
        return GObject.ParamSpec._new_internal(name, GObject.TYPE_DOUBLE,
                                               nick, blurb, flags, minimum, maximum, default_value);
    };

    this.ParamSpec.string = function(name, nick, blurb, flags, default_value) {
        return GObject.ParamSpec._new_internal(name, GObject.TYPE_STRING,
                                               nick, blurb, flags, default_value);
    };

    this.Class = GObjectMeta;
    this.Object.prototype.__metaclass__ = this.Class;

    // For compatibility with Lang.Class... we need a _construct
    // or the Lang.Class constructor will fail.
    this.Object.prototype._construct = function() {
        this._init.apply(this, arguments);
        return this;
    };

    // fake enum for signal accumulators, keep in sync with gi/object.c
    this.AccumulatorType = {
        NONE: 0,
        FIRST_WINS: 1,
        TRUE_HANDLED: 2
    };
}
