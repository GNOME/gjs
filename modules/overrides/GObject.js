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

const Gi = imports._gi;
const GjsPrivate = imports.gi.GjsPrivate;
const Legacy = imports._legacy;

let GObject;

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

    // fake enum for signal accumulators, keep in sync with gi/object.c
    this.AccumulatorType = {
        NONE: 0,
        FIRST_WINS: 1,
        TRUE_HANDLED: 2
    };

    this.Object.prototype.disconnect = function(id) {
        return GObject.signal_handler_disconnect(this, id);
    };
}
