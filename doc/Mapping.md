# GObject Usage in GJS

This is general overview of how to use GObject in GJS. Whenever possible GJS
tries to use idiomatic JavaScript, so this document may be of more interest to
C or Python developers that are new to GJS.

## GObject Construction

GObjects can be constructed with the `new` operator, and usually take an `Object` map of properties:

```js
const label = new Gtk.Label({
    label: '<a href="https://www.gnome.org">gnome.org</a>',
    halign: Gtk.Align.CENTER,
    hexpand: true,
    use_markup: true,
    visible: true,
});
```

The object that you pass to `new` (`Gtk.Label` in the example above) is the
**constructor object**, which may also contain static methods and constructor
methods such as `Gio.File.new_for_path()`:

```js
const file = Gio.File.new_for_path('/proc/cpuinfo');
```

The **constructor object** is different from the **prototype object**
containing instance methods. For more information on JavaScript's prototypal
inheritance, this [blog post][understanding-javascript-prototypes] is a good
resource.

[understanding-javascript-prototypes]: https://javascriptweblog.wordpress.com/2010/06/07/understanding-javascript-prototypes/

## GObject Subclassing

> See also: [`GObject.registerClass()`](overrides.md#gobject-registerclass)

GObjects have facilities for defining properties, signals and implemented
interfaces. Additionally, Gtk objects support defining a CSS name and composite
template.

The **constructor object** is also passed to the `extends` keyword in class
declarations when subclassing GObjects.

```js
var MyLabel = GObject.registerClass({
    // GObject
    GTypeName: 'Gjs_MyLabel',                   // GType name (see below)
    Implements: [ Gtk.Orientable ],             // Interfaces the subclass implements
    Properties: {},                             // More below on custom properties
    Signals: {},                                // More below on custom signals
    // Gtk
    CssName: '',                                // CSS name
    Template: 'resource:///path/example.ui',    // Builder template
    Children: [ 'label-child' ],                // Template children
    InternalChildren: [ 'internal-box' ]        // Template internal (private) children
}, class MyLabel extends Gtk.Label {
    constructor(params) {
        // Chaining up
        super(params);
    }
});
```

Note that before GJS 1.72 (GNOME 42), you had to override `_init()` and
chain-up with `super._init()`. This behaviour is still supported for
backwards-compatibility, but new code should use the standard `constructor()`
and chain-up with `super()`.

For a more complete introduction to GObject subclassing in GJS, see the
[GObject Tutorial][gobject-subclassing].

[gobject-subclassing]: https://gjs.guide/guides/gobject/subclassing.html#subclassing-gobject

## GObject Properties

GObject properties may be retrieved and set using native property style access
or GObject get/set methods. Note that variables in JavaScript can't contain
hyphens (-) so when a property name is *unquoted* use an underscore (_).

```js
let value;

value = label.use_markup;
value = label.get_use_markup();
value = label['use-markup'];

label.use_markup = value;
label.set_use_markup(value);
label['use-markup'] = value;

label.connect('notify::use-markup', () => {});
```

GObject subclasses can register properties, which is necessary if you want to
use `GObject.notify()` or `GObject.bind_property()`.

> Warning: Never use underscores in property names in the ParamSpec, because of
> the conversion between underscores and hyphens mentioned above.

```js
var MyLabel = GObject.registerClass({
    Properties: {
        'example-prop': GObject.ParamSpec.string(
            'example-prop',                     // property name
            'ExampleProperty',                  // nickname
            'An example read write property',   // description
            GObject.ParamFlags.READWRITE,       // READABLE/READWRITE/CONSTRUCT/etc
            'A default'  // default value if omitting getter/setter
        )
    }
}, class MyLabel extends Gtk.Label {
    get example_prop() {
        if (!('_example_prop' in this)
            return 'A default';
        return this._example_prop;
    }

    set example_prop(value) {
        if (this._example_prop !== value) {
            this._example_prop = value;
            this.notify('example-prop');
        }
    }
});
```

If you just want a simple property that you can get change notifications from,
you can leave out the getter and setter and GJS will attempt to do the right
thing. However, if you define one, you have to define both (unless the property
is read-only or write-only).

The 'default value' parameter passed to `GObject.ParamSpec` will be taken into
account if you omit the getter and setter. If you write your own getter and
setter, you have to implement the default value yourself, as in the above
example.

## GObject Signals

> See also: The [`Signals`][signals-module] module contains an GObject-like
> signal framework for native Javascript classes

Every object inherited from GObject has `connect()`, `connect_after()`,
`disconnect()` and `emit()` methods.

```js
// Connecting a signal handler
let handlerId = label.connect('activate-link', (label, uri) => {
    Gtk.show_uri_on_window(label.get_toplevel(), uri,
        Gdk.get_current_time());

    return true;
});

// Emitting a signal
label.emit('activate-link', 'https://www.gnome.org');

// Disconnecting a signal handler
label.disconnect(handlerId);
```

GObject subclasses can also register their own signals.

```js
var MyLabel = GObject.registerClass({
    Signals: {
        'my-signal': {
            flags: GObject.SignalFlags.RUN_FIRST,
            param_types: [ GObject.TYPE_STRING ]
        }
    }
}, class ExampleApplication extends GObject.Object {
    constructor() {
        super();
        this.emit('my-signal', 'a string parameter');
    }
});
```

[signals-module]: https://gjs-docs.gnome.org/gjs/signals.md

## GType Objects

> See also: [`GObject.Object.$gtype`][gobject-object-gtype] and
> [`GObject.registerClass()`][gobject-registerclass]

This is the object that represents a type in the GObject type system. Internally
a GType is an integer, but you can't access that integer in GJS.

The GType object is simple wrapper with two members:

* name (`String`) — A read-only string property, such as `"GObject"`
* toString() (`Function`) — Returns a string representation of the GType, such
  as `"[object GType for 'GObject']"`

Generally this object is not useful and better alternatives exist. Whenever a
GType is expected as an argument, you can simply pass a **constructor object**:

```js
// Passing a "constructor object" in place of a GType
const listInstance = Gio.ListStore.new(Gtk.Widget);

// This also works for GObject.Interface types, such as Gio.ListModel
const pspec = Gio.ParamSpec.object('list', '', '', GObject.ParamFlags.READABLE,
    Gio.ListModel);
```

To confirm the GType of an object instance, you can just use the standard
[`instanceof` operator][mdn-instanceof]:

```js
// Comparing an instance to a "constructor object"
const objectInstance = new GObject.Object();

// Comparing an instance to a "constructor object"
if (objectInstance instanceof GObject.Object)
    log(true);

// GtkLabel inherits from GObject.Object, so both of these are true
const labelInstance = new Gtk.Label();

if (labelInstance instance of GObject.Object)
    log(true);

if (labelInstance instance of Gtk.Label)
    log(true);
```

[gobject-object-gtype]: https://gjs-docs.gnome.org/gjs/overrides.md#gobject-object-gtype
[gobject-registerclass]: https://gjs-docs.gnome.org/gjs/overrides.md#gobject-registerclass
[mdn-instanceof]: https://developer.mozilla.org/docs/Web/JavaScript/Reference/Operators/instanceof

## Enumerations and Flags

Both enumerations and flags appear as entries under the namespace, with
associated member properties. These are available in the official GJS
[GNOME API documentation][gjs-docs].

Examples:

```js
// enum GtkAlign, member GTK_ALIGN_CENTER
Gtk.Align.CENTER;

// enum GtkWindowType, member GTK_WINDOW_TOPLEVEL
Gtk.WindowType.TOPLEVEL;

// enum GApplicationFlags, member G_APPLICATION_FLAGS_NONE
Gio.ApplicationFlags.FLAGS_NONE
```

Flags can be manipulated using native [bitwise operators][mdn-bitwise]:

```js
// Setting a flags property with a combination of flags
const myApp = new Gio.Application({
    flags: Gio.ApplicationFlags.HANDLES_OPEN |
           Gio.ApplicationFlags.HANDLES_COMMAND_LINE
});

// Checking if a flag is set, and removing it if so
if (myApp.flags & Gio.ApplicationFlags.HANDLES_OPEN)
    myApp.flags &= ~Gio.ApplicationFlags.HANDLES_OPEN;
```

[gjs-docs]: https://gjs-docs.gnome.org
[mdn-bitwise]: https://developer.mozilla.org/docs/Web/JavaScript/Reference/Operators/Bitwise_Operators

## Structs and Unions

Structures and unions are documented in the [GNOME API documentation][gjs-docs]
(e.g. [Gdk.Event][gdk-event]) and generally have either JavaScript properties or
getter methods for each member. Results may vary when trying to modify structs
or unions.

An example from GTK3:

```js
widget.connect('key-press-event', (widget, event) => {
    // expected output: [union instance proxy GIName:Gdk.Event jsobj@0x7f19a00b6400 native@0x5620c6a7c6e0]
    log(event);

    // expected output: true
    log(event.get_event_type() === Gdk.EventType.KEY_PRESS);

    // example output: 65507
    const [, keyval] = event.get_keyval();
    log(keyval);
});
```

[gdk-event]: https://gjs-docs.gnome.org/gdk40/gdk.event

## Return Values and `caller-allocates`

> Note: This information is intended for C programmers. Most developers can
> simply check the documentation for the function in question.

In GJS functions with "out" parameters (`caller-allocates`) are returned as an
array of values. For example, in C you may use function like this:

```c
GtkRequisition min_size, max_size;

gtk_widget_get_preferred_size (widget, &min_size, &max_size);
```

While in GJS it is returned as an array of those values instead:

```js
const [minSize, maxSize] = widget.get_preferred_size();
```

If the function has both a return value and "out" parameters, the return value
will be the first element of the array:

```js
try {
    const file = new Gio.File({ path: '/proc/cpuinfo' });

    // In the C API, `ok` is the only return value of this method
    const [ok, contents, etag_out] = file.load_contents(null);
} catch(e) {
    log('Failed to read file: ' + e.message);
}
```

Note that because JavaScript throws exceptions, rather than setting a `GError`
structure, it is common practice to elide the success boolean in GJS:

```js
try {
    const file = new Gio.File({ path: '/proc/cpuinfo' });

    // Eliding success boolean
    const [, contents, etag] = file.load_contents(null);
} catch(e) {
    log('Failed to read file: ' + e.message);
}
```

