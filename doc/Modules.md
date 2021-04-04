GJS includes some built-in modules, as well as helpers for some core APIs like DBus like Variants. The headings below are links to the JavaScript source, which are decently documented and informative of usage.

## [Gio](https://gitlab.gnome.org/GNOME/gjs/blob/HEAD/modules/core/overrides/Gio.js)

**Import with `const Gio = gi.require('Gio');` or `import Gio from 'gi://Gio'`**

The `Gio` override includes a number of utilities for DBus that will be documented further at a later date. Below is a reasonable overview.

* `Gio.DBus.session`, `Gio.DBus.system`

    Convenience properties that wrap `Gio.bus_get_sync()` to return a DBus connection
* `Gio.DBusNodeInfo.new_for_xml(xmlString)`

    Return a new `Gio.DBusNodeInfo` for xmlString
* `Gio.DBusInterfaceInfo.new_for_xml(xmlString)`

    Return a new `Gio.DBusInterfaceInfo` for the first interface node of xmlString
* `Gio.DBusProxy.makeProxyWrapper(xmlString)`

    Returns a `function(busConnection, busName, objectPath, asyncCallback, cancellable)` which can be called to return a new `Gio.DBusProxy` for the first interface node of `xmlString`. See [here][old-dbus-example] for the original example.
* `Gio.DBusExportedObject.wrapJSObject(Gio.DbusInterfaceInfo, jsObj)`

    Takes `jsObj`, an object instance implementing the interface described by `Gio.DbusInterfaceInfo`, and returns an implementation object with these methods:

    * `export(busConnection, objectPath)`
    * `unexport()`
    * `flush()`
    * `emit_signal(name, variant)`
    * `emit_property_changed(name, variant)`

[old-dbus-example]: https://wiki.gnome.org/Gjs/Examples/DBusClient

## [GLib](https://gitlab.gnome.org/GNOME/gjs/blob/HEAD/modules/core/overrides/GLib.js)

**Import with `const GLib = gi.require('GLib');` or `import GLib from 'gi://GLib'`**

Mostly GVariant and GBytes compatibility.

* `GLib.log_structured()`: Wrapper for g_log_variant()
* `GLib.Bytes.toArray()`: Convert a GBytes object to a ByteArray object
* `GLib.Variant.unpack()`: Unpack a variant to a native type
* `GLib.Variant.deep_unpack()`: Deep unpack a variant.

## [GObject](https://gitlab.gnome.org/GNOME/gjs/blob/HEAD/modules/core/overrides/GObject.js)

**Import with `const GObject = gi.require('GObject');` or `import GObject from 'gi://GObject'`**

Mostly GObject implementation (properties, signals, GType mapping). May be useful as a reference.

## [Gtk](https://gitlab.gnome.org/GNOME/gjs/blob/HEAD/modules/core/overrides/Gtk.js)

**Import with `const Gtk = gi.require('Gtk', '3.0');` or `import Gtk from 'gi://Gtk'`**

Mostly GtkBuilder/composite template implementation. May be useful as a reference.

>>>
**REMINDER:** You should specify a version prior to importing a library with multiple versions:

```js
import Gtk from 'gi://Gtk?version=3.0';
```
>>>

## Cairo

**Import with `import Cairo from 'cairo';`**

Mostly API compatible with [cairo](https://www.cairographics.org/documentation/), but using camelCase function names. There is list of constants in [cairo.js][cairo-const] and functions for each object in its corresponding C++ file (eg. [cairo-context.cpp][cairo-func]). A simple example drawing a 32x32 red circle:

```js
import Gtk from 'gi://Gtk?version=3.0';
import Cairo from 'cairo';

let drawingArea = new Gtk.DrawingArea({
    height_request: 32,
    width_request: 32
});

drawingArea.connect("draw", (widget, cr) => {
    // Cairo in GJS uses camelCase function names
    cr.setSourceRGB(1.0, 0.0, 0.0);
    cr.setOperator(Cairo.Operator.DEST_OVER);
    cr.arc(16, 16, 16, 0, 2*Math.PI);
    cr.fill();
    // currently when you connect to a draw signal you have to call
    // cr.$dispose() on the Cairo context or the memory will be leaked.
    cr.$dispose();
    return false;
});
```

[cairo-const]: https://gitlab.gnome.org/GNOME/gjs/blob/HEAD/modules/script/cairo.js
[cairo-func]: https://gitlab.gnome.org/GNOME/gjs/blob/HEAD/modules/cairo-context.cpp#L825

## [Format](https://gitlab.gnome.org/GNOME/gjs/blob/HEAD/modules/script/format.js)

**Import with `const Format = imports.format;`**

The format import is mostly obsolete, providing `vprintf()`, `printf()` and `format()`. Native [template literals][template-literals] should be preferred now, except in few situations like Gettext (See [Bug #50920][bug-50920]).

```js
let foo = "Pi";
let bar = 1;
let baz = Math.PI;

// Using native template literals (Output: Pi to 2 decimal points: 3.14)
`${foo} to ${bar*2} decimal points: ${baz.toFixed(bar*2)}`

// Applying format() to the string prototype
const Format = imports.format;
String.prototype.format = Format.format;

// Using format() (Output: Pi to 2 decimal points: 3.14)
"%s to %d decimal points: %.2f".format(foo, bar*2, baz);

// Using format() with Gettext
_("%d:%d").format(11, 59);
Gettext.ngettext("I have %d apple", "I have %d apples", num).format(num);

```

[template-literals]: https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Template_literals
[bug-50920]: https://savannah.gnu.org/bugs/?50920

## [Gettext](https://gitlab.gnome.org/GNOME/gjs/blob/HEAD/modules/script/gettext.js)

**Import with `import gettext from 'gettext';`**

Helper functions for gettext. See also [examples/gettext.js][example-gettext] for usage.

[example-gettext]: https://gitlab.gnome.org/GNOME/gjs/blob/HEAD/examples/gettext.js

### Legacy Imports (`imports.gettext`)

Gettext is also exposed via `imports.gettext` on the global `imports` object.

## [jsUnit](https://gitlab.gnome.org/GNOME/gjs/blob/HEAD/modules/script/jsUnit.js)

**DEPRECATED**

Deprecated unit test functions. [Jasmine][jasmine-gjs] for GJS should now be preferred, as demonstrated in the GJS [test suite][gjs-tests].

[jasmine-gjs]: https://github.com/ptomato/jasmine-gjs
[gjs-tests]: https://gitlab.gnome.org/GNOME/gjs/blob/HEAD/installed-tests/js

## [`Lang`](https://gitlab.gnome.org/GNOME/gjs/blob/HEAD/modules/script/lang.js)

**DEPRECATED**

Lang is a mostly obsolete library, that should only be used in cases where older versions of GJS must be supported. For example, `Lang.bind()` was necessary to bind `this` to the function context before the availability of arrow functions:

```js
const Lang = imports.lang;
const FnorbLib = imports.fborbLib;

const MyLegacyClass = new Lang.Class({
    _init: function() {
        let fnorb = new FnorbLib.Fnorb();
        fnorb.connect('frobate', Lang.bind(this, this._onFnorbFrobate));
    },

    _onFnorbFrobate: function(fnorb) {
        this._updateFnorb();
    }
});

var MyNewClass = class {
    constructor() {
        let fnorb = new FnorbLib.Fnorb();
        fnorb.connect('frobate', fnorb => this._onFnorbFrobate);
    }

    _onFnorbFrobate(fnorb) {
        this._updateFnorb();
    }
}
```

## [Mainloop](https://gitlab.gnome.org/GNOME/gjs/blob/HEAD/modules/script/mainloop.js)

**DEPRECATED**

Mainloop is simply a layer of convenience and backwards-compatibility over some GLib functions (such as [`GLib.timeout_add()`][gjs-timeoutadd] which in GJS is mapped to [`g_timeout_add_full()`][c-timeoutaddfull]). It's use is not generally recommended anymore.

[c-timeoutaddfull]: https://developer.gnome.org/glib/stable/glib-The-Main-Event-Loop.html#g-timeout-add-full
[gjs-timeoutadd]: https://gjs-docs.gnome.org/glib20/glib.timeout_add

## [Package](https://gitlab.gnome.org/GNOME/gjs/blob/HEAD/modules/script/package.js)

Infrastructure and utilities for [standalone applications](Home#standalone-applications).

## [Signals](https://gitlab.gnome.org/GNOME/gjs/blob/HEAD/modules/script/signals.js)

**Import with `const Signals = imports.signals;`**

A GObject-like signal framework for native Javascript objects.

**NOTE:** Unlike [GObject signals](Mapping#signals), `this` within a signal callback will refer to the global object (ie. `globalThis`).

```js
const Signals = imports.signals;

var MyJSClass = class {
    testSignalEmission () {
        this.emit("exampleSignal", "stringArg", 42);
    }
}
Signals.addSignalMethods(MyJSClass.prototype);

let obj = new MyJSObject();

// Connect and disconnect like standard GObject signals
let handlerId = obj.connect("exampleSignal", (obj, stringArg, intArg) => {
    // Remember 'this' === 'globalThis'
});
obj.disconnect(handlerId);

// A convenience function not in GObject
obj.disconnectAll();
```

## [System](https://gitlab.gnome.org/GNOME/gjs/blob/HEAD/modules/system.cpp)

**Import with `import system from 'system';`**

The System module offers a number of useful functions and properties for debugging and shell interaction (eg. ARGV):

  * `addressOf(object)`

    Return the memory address of any object as a string in hexadecimal, e.g. `0xb4f170f0`.
    Caution, don't use this as a unique identifier!
    JavaScript's garbage collector can move objects around in memory, or deduplicate identical objects, so this value may change during the execution of a program.

  * `refcount(gobject)`

    Return the reference count of any GObject-derived type (almost any class from GTK, Clutter, GLib, Gio, etc.). When an object's reference count is zero, it is cleaned up and erased from memory.

  * `breakpoint()`

    This is the real gem of the System module! It allows just the tiniest amount of decent debugging practice in GJS. Put `System.breakpoint()` in your code and run it under GDB like so:

    ```
    gdb --args gjs my_program.js
    ```

    When GJS reaches the breakpoint, it will stop executing and return you to the GDB prompt, where you can examine the stack or other things, or type `cont` to continue running. Note that if you run the program outside of GDB, it will abort at the breakpoint, so make sure to remove the breakpoint when you're done debugging.

  * `gc()`

    Run the garbage collector.

  * `exit(error_code)`

    This works the same as C's `exit()` function; exits the program, passing a certain error code to the shell. The shell expects the error code to be zero if there was no error, or non-zero (any value you please) to indicate an error. This value is used by other tools such as `make`; if `make` calls a program that returns a non-zero error code, then `make` aborts the build.

  * `version`

    This property contains version information about GJS.

  * `programInvocationName`

    This property contains the name of the script as it was invoked from the command line. In C and other languages, this information is contained in the first element of the platform's equivalent of `argv`, but GJS's `ARGV` only contains the subsequent command-line arguments, so `ARGV[0]` in GJS is the same as `argv[1]` in C.

    For example, passing ARGV to a `Gio.Application`/`Gtk.Application` (See also:
 [examples/gtk-application.js][example-application]):

    ```js
    import Gtk from 'gi://Gtk?version=3.0';
    import system from 'system';

    let myApp = new Gtk.Application();
    myApp.connect("activate", () => log("activated"));
    myApp.run([System.programInvocationName].concat(ARGV));
    ```

[example-application]: https://gitlab.gnome.org/GNOME/gjs/blob/HEAD/examples/gtk-application.js

## [Tweener](https://gitlab.gnome.org/GNOME/gjs/blob/HEAD/modules/script/tweener/)

**Import with `const Tweener = imports.tweener.tweener;`**

Built-in version of the well-known [Tweener][tweener-www] animation/property transition library.

[tweener-www]: http://hosted.zeh.com.br/tweener/docs/

## GObject Introspection

**Import with `import gi from 'gi';`**

A wrapper of **libgirepository** to import native gobject-introspection libraries.

* `gi.require(library: string, version?: string)`

Loads a native gobject-introspection library.
Version is required if more than one version of a library is installed.

You can also import libraries through the `gi://` URL scheme.
This function is only intended to be used when you want to import a
library conditionally, since top-level import statements are resolved
statically.

### Legacy Imports (`imports.gi`)

**Import with `const gi = imports.gi;`**

A wrapper for **libgirepository** is also available via the global `imports` object.
This object has a property `versions` which is an object on which you can set string-valued
properties indicating the version of that gobject-introspection library you want to load,
and loading multiple versions in the same process is forbidden. So if you want to
use gtk-3.0, set `imports.gi.versions.Gtk = '3.0';`.

Any other properties of `imports.gi` will attempt to import a gobject-introspection library
with the property name, picking the latest version if there is no entry for it in `imports.gi.versions`.

## Legacy Imports

Prior to the introduction of [ES Modules](ESModules.md), GJS had its own import system.

**imports** is a global object that you can use to import any js file or GObject
Introspection lib as module, there are 4 special properties of **imports**:

 * `searchPath`

    An array of path that used to look for files, if you want to prepend a path
    you can do something like `imports.searchPath.unshift(myPath)`.

 * `__modulePath__`
 * `__moduleName__`
 * `__parentModule__`

    These 3 properties is intended to be used internally, you should not use them.

Any other properties of **imports** is treated as a module, if you access these
properties, an import is attempted. Gjs try to look up a js file or directory by property name
from each location in `imports.searchPath`. For `imports.foo`, if a file named
`foo.js` is found, this file is executed and then imported as a module object; else if
a directory `foo` is found, a new importer object is returned and its `searchPath` property
is replaced by the path of `foo`.

Note that any variable, function and class declared at the top level,
except those declared by `let` or `const`, are exported as properties of the module object,
and one js file is executed only once at most even if it is imported multiple times.
