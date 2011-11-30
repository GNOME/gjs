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

let GObject;

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

}
