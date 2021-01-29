# GObject Introspection

**Import with `import gi from 'gi';`**

A wrapper of **libgirepository** to import native gobject-introspection libraries.

* `gi.require(library: string, version?: string)`

Loads a native gobject-introspection library. Version is required if more than one version of a library is installed.
You can also import libraries through the `gi://` URL scheme.

## GJS API Additions

GJS adds several convenience APIs to core GNOME libraries like GLib, Gio, GObject, and GTK.
These APIs make integrating GObject Introspection based libraries with JavaScript simpler and more natural.

### [Gio](https://gitlab.gnome.org/GNOME/gjs/blob/master/modules/core/overrides/Gio.js)

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

### [GLib](https://gitlab.gnome.org/GNOME/gjs/blob/master/modules/core/overrides/GLib.js)

**Import with `const GLib = gi.require('GLib');` or `import GLib from 'gi://GLib'`**

Mostly GVariant and GBytes compatibility.

* `GLib.log_structured()`: Wrapper for g_log_variant()
* `GLib.Bytes.toArray()`: Convert a GBytes object to a ByteArray object
* `GLib.Variant.unpack()`: Unpack a variant to a native type
* `GLib.Variant.deep_unpack()`: Deep unpack a variant.

### [GObject](https://gitlab.gnome.org/GNOME/gjs/blob/master/modules/core/overrides/GObject.js)

**Import with `const GObject = gi.require('GObject');` or `import GObject from 'gi://GObject'`**

Mostly GObject implementation (properties, signals, GType mapping). May be useful as a reference.

### [Gtk](https://gitlab.gnome.org/GNOME/gjs/blob/master/modules/core/overrides/Gtk.js)

**Import with `const Gtk = gi.require('Gtk', '3.0');` or `import Gtk from 'gi://Gtk'`**

Mostly GtkBuilder/composite template implementation. May be useful as a reference.

>>>
**REMINDER:** You should specify a version prior to importing a library with multiple versions:

```js
import Gtk from 'gi://Gtk?version=3.0';
```
>>>

## Legacy Imports (`imports.gi`)

**Import with `const gi = imports.gi;`**

A wrapper for **libgirepository** is also available via the global `imports` object.
This object has a property `versions` which is an object on which you can set string-valued
properties indicating the version of that gobject-introspection library you want to load,
and loading multiple versions in the same process is forbidden. So if you want to
use gtk-3.0, set `imports.gi.versions.Gtk = '3.0';`.

Any other properties of `imports.gi` will attempt to import a gobject-introspection library
with the property name, picking the latest version if there is no entry for it in `imports.gi.versions`.