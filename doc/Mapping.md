## GObject Construction, Subclassing, Templates and GType

### Constructing GObjects

GObjects can be constructed with the `new` operator, just like JavaScript objects, and usually take an Object map of properties.

The object that you pass to `new` (e.g. `Gtk.Label` in `let label = new Gtk.Label()`) is the **constructor object**, that contains constructor methods and static methods such as `Gio.File.new_for_path()`.
It's different from the **prototype object** containing instance methods.
For more information on JavaScript's prototypal inheritance, this [blog post][understanding-javascript-prototypes] is a good resource.

```js
let label = new Gtk.Label({
    label: '<a href="https://www.gnome.org">gnome.org</a>',
    halign: Gtk.Align.CENTER,
    hexpand: true,
    use_markup: true,
    visible: true
});

let file = Gio.File.new_for_path('/proc/cpuinfo');
```

[understanding-javascript-prototypes]: https://javascriptweblog.wordpress.com/2010/06/07/understanding-javascript-prototypes/

### Subclassing GObjects

GObjects have facilities for defining properties, signals and implemented interfaces. Additionally, Gtk objects support defining a CSS name and composite template.

The **constructor object** is also passed to the `extends` keyword in class declarations when subclassing GObjects.

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
    // Currently GObjects use _init, not construct
    _init(params) {
        // Chaining up
        super._init(params);
    }
});
```

### GType Objects

This is the object that represents a type in the GObject type system. Internally a GType is an integer, but you can't access that integer in GJS.

The `$gtype` property gives the GType object for the given type. This is the proper way to find the GType given an object or a class. For a class, `GObject.type_from_name('GtkLabel')` would work too if you know the GType name, but only if you had previously constructed a Gtk.Label object.

```js
log(Gtk.Label.$gtype);
log(labelInstance.constructor.$gtype);
// expected output: [object GType for 'GtkLabel']
```

The `name` property of GType objects gives the GType name as a string ('GtkLabel'). This is the proper way to find the type name given an object or a class.

User defined subclasses' GType name will be the class name prefixed with `Gjs_`by default.
If you want to specify your own name, you can pass it as the value for the `GTypeName` property to `GObject.registerClass()`.
This will be relevant in situations such as defining a composite template for a GtkWidget subclass.

```js
log(Gtk.Label.$gtype.name);
log(labelInstance.constructor.$gtype.name);
// expected output: GtkLabel

log(MyLabel.$gtype.name);
// expected output: Gjs_MyLabel
```

[`instanceof`][mdn-instanceof] can be used to compare an object instance to a **constructor object**.

```js
log(typeof labelInstance);
// expected output: object

log(labelInstance instanceof Gtk.Label);
// expected output: true
```

[mdn-instanceof]: https://developer.mozilla.org/docs/Web/JavaScript/Reference/Operators/instanceof

## Properties

GObject properties may be retrieved and set using native property style access or GObject get/set methods. Note that variables in JavaScript can't contain hyphens (-) so when a property name is *unquoted* use an underscore (_).

```js
if (!label.use_markup) {
    label.connect('notify::use-markup', () => { ... });
    label.use_markup = true;
    label['use-markup'] = true;
    label.set_use_markup(true);
}
```

GObject subclasses can register properties, which is necessary if you want to use `GObject.notify()` or `GObject.bind_property()`.

**NOTE:** Never use underscores in property names in the ParamSpec, because of the conversion between underscores and hyphens mentioned above.

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

If you just want a simple property that you can get change notifications from, you can leave out the getter and setter and GJS will attempt to do the right thing.
However, if you define one, you have to define both (unless the property is read-only or write-only).

The 'default value' parameter passed to `GObject.ParamSpec` will be taken into account if you omit the getter and setter.
If you write your own getter and setter, you have to implement the default value yourself, as in the above example.

## Signals

Every object inherited from GObject has `connect()`, `connect_after()`, `disconnect()` and `emit()` methods.

```js
let handlerId = label.connect('activate-link', (label, uri) => {
    Gtk.show_uri_on_window(
        label.get_toplevel(),
        uri,
        Gdk.get_current_time()
    );
    return true;
});

label.emit('activate-link', 'https://www.gnome.org');

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
    _init() {
        super._init();
        this.emit('my-signal', 'a string parameter');
    }
});
```

**NOTE:** GJS also includes a built-in [`signals`](Modules#signals) module for applying signals to native JavaScript classes.

## Enumerations and Flags

Both enumerations and flags appear as entries under the namespace, with associated member properties. These are available in the official GJS [GNOME API documentation][gjs-docs].

```js
// enum GtkAlign, member GTK_ALIGN_CENTER
Gtk.Align.CENTER;
// enum GtkWindowType, member GTK_WINDOW_TOPLEVEL
Gtk.WindowType.TOPLEVEL;
// enum GApplicationFlags, member G_APPLICATION_FLAGS_NONE
Gio.ApplicationFlags.FLAGS_NONE
```

Flags can be manipulated using native [bitwise operators][mdn-bitwise].

```js
let myApp = new Gio.Application({
    flags: Gio.ApplicationFlags.HANDLES_OPEN | Gio.ApplicationFlags.HANDLES_COMMAND_LINE
});

if (myApp.flags & Gio.ApplicationFlags.HANDLES_OPEN) {
    myApp.flags &= ~Gio.ApplicationFlags.HANDLES_OPEN;
}
```

[gjs-docs]: https://gjs-docs.gnome.org
[mdn-bitwise]: https://developer.mozilla.org/docs/Web/JavaScript/Reference/Operators/Bitwise_Operators

## Structs and Unions

C structures and unions are documented in the [GNOME API documentation][gjs-docs] (e.g. [Gdk.Event][gdk-event]) and generally have either JavaScript properties or getter methods for each member. Results may vary when trying to modify structs or unions.

```js
widget.connect("key-press-event", (widget, event) => {
    log(event);
    // expected output: [union instance proxy GIName:Gdk.Event jsobj@0x7f19a00b6400 native@0x5620c6a7c6e0]
    log(event.get_event_type() === Gdk.EventType.KEY_PRESS);
    // expected output: true
    let [ok, keyval] = event.get_keyval();
    log(keyval);
    // example output: 65507
});
```

[gdk-event]: https://gjs-docs.gnome.org/gdk40/gdk.event

## Multiple return values (caller-allocates)

In GJS caller-allocates (variables passed into a function) and functions with multiple out parameters are returned as an array of return values. If the function has a return value, it will be the first element of that array.

```js
let [minimumSize, naturalSize] = label.get_preferred_size();

// Functions with boolean 'success' returns often still throw an Error on failure
try {
    let file = new Gio.File({ path: '/proc/cpuinfo' });
    let [ok, contents, etag_out] = file.load_contents(null);
    // "ok" is actually useless in this scenario, since if it is false,
    // an exception will have been thrown. You can skip return values
    // you don't want with array elision:
    let [, contents2] = file.load_contents(null);
} catch(e) {
    log('Failed to read file: ' + e.message);
}
```