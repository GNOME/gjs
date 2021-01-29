# Built-in Modules

GJS also exposes several built-in modules via the `imports` global.
These packages are not exposed via [ES Modules](ESModules.md) and may be deprecated in future GJS versions.

## [Mainloop](https://gitlab.gnome.org/GNOME/gjs/blob/master/modules/script/mainloop.js)

Mainloop is simply a layer of convenience and backwards-compatibility over some GLib functions (such as [`GLib.timeout_add()`][gjs-timeoutadd] which in GJS is mapped to [`g_timeout_add_full()`][c-timeoutaddfull]). It's use is not generally recommended anymore.

[c-timeoutaddfull]: https://developer.gnome.org/glib/stable/glib-The-Main-Event-Loop.html#g-timeout-add-full
[gjs-timeoutadd]: https://gjs-docs.gnome.org/glib20/glib.timeout_add

## [Package](https://gitlab.gnome.org/GNOME/gjs/blob/master/modules/script/package.js)

Infrastructure and utilities for [standalone applications](Home#standalone-applications).

## [Signals](https://gitlab.gnome.org/GNOME/gjs/blob/master/modules/script/signals.js)

**Import with `const Signals = imports.signals;`**

A GObject-like signal framework for native Javascript objects.

**NOTE:** Unlike [GObject signals](Mapping#signals), `this` within a signal callback will refer to the global object (ie. `window`).

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
    // Remember 'this' === 'window'
});
obj.disconnect(handlerId);

// A convenience function not in GObject
obj.disconnectAll();
```
