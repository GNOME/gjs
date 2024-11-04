# Overrides

Like other binding languages, GJS includes a number of overrides for various
libraries, like GIO and GTK. These overrides include implementations of
functions not normally available to language bindings, as well as convenience
functions and support for native JavaScript features such as iteration.

The library headings below are links to the JavaScript source for each override,
which may clarify particular behaviour or contain extra implementation notes.


## [Gio](https://gitlab.gnome.org/GNOME/gjs/blob/HEAD/modules/core/overrides/Gio.js)

The `Gio` override includes a number of utilities and conveniences, in
particular a number of helpers for working with D-Bus in GJS.

For a longer introduction to the D-Bus utilities listed here, see the
[D-Bus Tutorial][dbus-tutorial].

[dbus-tutorial]: https://gjs.guide/guides/gio/dbus.html

### Gio.DBus.session

> Warning: It is a programmer error to call `close()` on this object instance

Type:
* [`Gio.DBusConnection`][gdbusconnection]

Convenience for getting the session [`Gio.DBusConnection`][gdbusconnection].
This always returns the same object and is equivalent to calling:

```js
const connection = Gio.bus_get_sync(Gio.BusType.SESSION, null);
```

[gdbusconnection]: https://gjs-docs.gnome.org/gio20/gio.dbusconnection

### Gio.DBus.system

> Warning: It is a programmer error to call `close()` on this object instance

Type:
* [`Gio.DBusConnection`][gdbusconnection]

Convenience for getting the system [`Gio.DBusConnection`][gdbusconnection].
This always returns the same object and is equivalent to calling:

```js
const connection = Gio.bus_get_sync(Gio.BusType.SYSTEM, null);
```

[gdbusconnection]: https://gjs-docs.gnome.org/gio20/gio.dbusconnection

### Gio.DBusNodeInfo.new_for_xml(xmlData)

Type:
* Static

Parameters:
* xmlData (`String`) — Valid D-Bus introspection XML

Returns:
* (`Gio.DBusNodeInfo`) — A [`Gio.DBusNodeInfo`][gdbusnodeinfo] structure

> Note: This is an override for function normally available in GIO

Parses `xmlData` and returns a [`Gio.DBusNodeInfo`][gdbusnodeinfo] representing
the data.

The introspection XML must contain exactly one top-level `<node>` element.

Note that this routine is using a GMarkup-based parser that only accepts a
subset of valid XML documents.

[gdbusnodeinfo]: https://docs.gtk.org/gio/struct.DBusNodeInfo.html

### Gio.DBusInterfaceInfo.new_for_xml(xmlData)

Type:
* Static

Parameters:
* xmlData (`String`) — Valid D-Bus introspection XML

Returns:
* (`Gio.DBusInterfaceInfo`) — A [`Gio.DBusInterfaceInfo`][gdbusinterfaceinfo]
  structure

Parses `xmlData` and returns a [`Gio.DBusInterfaceInfo`][gdbusinterfaceinfo]
representing the first `<interface>` element of the data.

This is a convenience wrapper around `Gio.DBusNodeInfo.new_for_xml()` for the
common case of a [`Gio.DBusNodeInfo`][gdbusnodeinfo] with a single interface.

[gdbusinterfaceinfo]: https://gjs-docs.gnome.org/gio20/gio.dbusinterfaceinfo

### Gio.DBusProxy.makeProxyWrapper(interfaceInfo)

Type:
* Static

Parameters:
* interfaceInfo (`String`|`Gio.DBusInterfaceInfo`) — Valid D-Bus introspection
  XML or [`Gio.DBusInterfaceInfo`][gdbusinterfaceinfo] structure

Returns:
* (`Function`) — A `Function` used to create a [`Gio.DBusProxy`][gdbusproxy]

Returns a `Function` that can be used to create a [`Gio.DBusProxy`][gdbusproxy]
for `interfaceInfo` if it is a [`Gio.DBusInterfaceInfo`][gdbusinterfaceinfo]
structure, or the first `<interface>` element if it is introspection XML.

The returned `Function` has the following signature:

```js
@param {Gio.DBusConnection} bus — A bus connection
@param {String} name — A well-known name
@param {String} object — An object path
@param {Function} [asyncCallback] — Optional callback
@param {Gio.Cancellable} [cancellable] — Optional cancellable
@param {Gio.DBusProxyFlags} flags — Optional flags
```

The signature for `asyncCallback` is:

```js
@param {Gio.DBusProxy|null} proxy — A D-Bus proxy, or null on failure
@param {Error} error — An exception, or null on success
```

See the [D-Bus Tutorial][make-proxy-wrapper] for an example of how to use this
function and the resulting [`Gio.DBusProxy`][gdbusproxy].

[gdbusproxy]: https://gjs-docs.gnome.org/gio20/gio.dbusproxy
[make-proxy-wrapper]: https://gjs.guide/guides/gio/dbus.html#high-level-proxies

### Gio.DBusExportedObject.wrapJSObject(interfaceInfo, jsObj)

Type:
* Static

Parameters:
* interfaceInfo (`String`|`Gio.DBusInterfaceInfo`) — Valid D-Bus introspection
  XML or [`Gio.DBusInterfaceInfo`][gdbusinterfaceinfo] structure
* jsObj (`Object`) — A `class` instance implementing `interfaceInfo`

Returns:
* (`Gio.DBusInterfaceSkeleton`) — A [`Gio.DBusInterfaceSkeleton`][gdbusinterfaceskeleton]

Takes `jsObj`, an object instance implementing the interface described by
[`Gio.DBusInterfaceInfo`][gdbusinterfaceinfo], and returns an instance of
[`Gio.DBusInterfaceSkeleton`][gdbusinterfaceskeleton].

The returned object has two additional methods not normally found on a
`Gio.DBusInterfaceSkeleton` instance:

* `emit_property_changed(propertyName, propertyValue)`
  * propertyName (`String`) — A D-Bus property name
  * propertyValue (`GLib.Variant`) — A [`GLib.Variant`][gvariant]

* `emit_signal(signalName, signalParameters)`
  * signalName (`String`) — A D-Bus signal name
  * signalParameters (`GLib.Variant`) — A [`GLib.Variant`][gvariant]

See the [D-Bus Tutorial][wrap-js-object] for an example of how to use this
function and the resulting [`Gio.DBusInterfaceSkeleton`][gdbusinterfaceskeleton].

[gdbusinterfaceskeleton]: https://gjs-docs.gnome.org/gio20/gio.dbusinterfaceskeleton
[gvariant]: https://gjs-docs.gnome.org/glib20/glib.variant
[wrap-js-object]: https://gjs.guide/guides/gio/dbus.html#exporting-interfaces

### Gio._promisify(prototype, startFunc, finishFunc)

> Warning: This is a tech-preview and not guaranteed to be stable

Type:
* Static

Parameters:
* prototype (`Object`) — The prototype of a GObject class
* startFunc (`Function`) — The "async" or "start" method
* finishFunc (`Function`) — The "finish" method

Replaces the original `startFunc` on a GObject class prototype, so that it
returns a `Promise` and can be used as a JavaScript `async` function.

The function may then be used like any other `Promise` without the need for a
customer wrapper, simply by invoking `startFunc` without the callback argument:

```js
Gio._promisify(Gio.InputStream.prototype, 'read_bytes_async',
    'read_bytes_finish');

try {
    const inputBytes = new GLib.Bytes('content');
    const inputStream = Gio.MemoryInputStream.new_from_bytes(inputBytes);
    const result = await inputStream.read_bytes_async(inputBytes.get_size(),
        GLib.PRIORITY_DEFAULT, null);
} catch (e) {
    logError(e, 'Failed to read bytes');
}
```

Note that for "finish" methods that normally return an array with a success
boolean, a wrapped function will automatically remove it from the return value:

```js
Gio._promisify(Gio.File.prototype, 'load_contents_async',
    'load_contents_finish');

try {
    const file = Gio.File.new_for_path('file.txt');
    const [contents, etag] = await file.load_contents_async(null);
} catch (e) {
    logError(e, 'Failed to load file contents');
}
```

### Gio.FileEnumerator[Symbol.asyncIterator]

[Gio.FileEnumerator](gio-fileenumerator) are [async iterators](async-iterators).

Each iteration returns a [Gio.FileInfo](gio-fileinfo):

```js
import Gio from "gi://Gio";

const dir = Gio.File.new_for_path("/");
const enumerator = dir.enumerate_children(
  "standard::name",
  Gio.FileQueryInfoFlags.NOFOLLOW_SYMLINKS,
  null
);

for await (const file_info of enumerator) {
  console.log(file_info.get_name());
}
```

[gio-fileenumerator]: https://gjs-docs.gnome.org/gio20/gio.fileenumerator
[async-iterator]: https://developer.mozilla.org/docs/Web/JavaScript/Reference/Iteration_protocols#the_async_iterator_and_async_iterable_protocols
[gio-fileinfo]: https://gjs-docs.gnome.org/gio20/gio.fileinfo

### Gio.FileEnumerator[Symbol.iterator]

[Gio.FileEnumerator](gio-fileenumerator) are [sync iterators](sync-iterators).

Each iteration returns a [Gio.FileInfo](gio-fileinfo):

```js
import Gio from "gi://Gio";

const dir = Gio.File.new_for_path("/");
const enumerator = dir.enumerate_children(
  "standard::name",
  Gio.FileQueryInfoFlags.NOFOLLOW_SYMLINKS,
  null
);

for (const file_info of enumerator) {
  console.log(file_info.get_name());
}
```

[gio-fileenumerator]: https://gjs-docs.gnome.org/gio20/gio.fileenumerator
[sync-iterator]: https://developer.mozilla.org/docs/Web/JavaScript/Reference/Iteration_protocols#the_iterable_protocol
[gio-fileinfo]: https://gjs-docs.gnome.org/gio20/gio.fileinfo

### Gio.InputStream.createAsyncIterator(count, priority)

Parameters:
* count (`Number`) — Number of bytes to read per iteration see [read_bytes]
* priority (`Number`) — Optional priority (i.e. `GLib.PRIORITY_DEFAULT`)

Returns:
* (`Object`) — An [asynchronous iterator][async-iterator]

Return an asynchronous iterator for a [`Gio.InputStream`][ginputstream].

Each iteration will return a [`GLib.Bytes`][gbytes] object:

```js
import Gio from "gi://Gio";

const textDecoder = new TextDecoder("utf-8");

const file = Gio.File.new_for_path("/etc/os-release");
const inputStream = file.read(null);

for await (const bytes of inputStream.createAsyncIterator(4)) {
  log(textDecoder.decode(bytes.toArray()));
}
```

[read_bytes]: https://gjs-docs.gnome.org/gio20/gio.inputstream#method-read_bytes
[async-iterator]: https://developer.mozilla.org/docs/Web/JavaScript/Reference/Iteration_protocols#the_async_iterator_and_async_iterable_protocols
[gbytes]: https://gjs-docs.gnome.org/glib20/glib.bytes
[ginputstream]: https://gjs-docs.gnome.org/gio20/gio.inputstream

### Gio.InputStream.createSyncIterator(count, priority)

Parameters:
* count (`Number`) — Number of bytes to read per iteration see [read_bytes]
* priority (`Number`) — Optional priority (i.e. `GLib.PRIORITY_DEFAULT`)

Returns:
* (`Object`) — An [synchronous iterator][sync-iterator]

Return a synchronous iterator for a [`Gio.InputStream`][ginputstream].

Each iteration will return a [`GLib.Bytes`][gbytes] object:

```js
import Gio from "gi://Gio";

const textDecoder = new TextDecoder("utf-8");

const file = Gio.File.new_for_path("/etc/os-release");
const inputStream = file.read(null);

for (const bytes of inputStream.createSyncIterator(4)) {
  log(textDecoder.decode(bytes.toArray()));
}
```

[read_bytes]: https://gjs-docs.gnome.org/gio20/gio.inputstream#method-read_bytes
[sync-iterator]: https://developer.mozilla.org/docs/Web/JavaScript/Reference/Iteration_protocols#the_iterable_protocol
[gbytes]: https://gjs-docs.gnome.org/glib20/glib.bytes
[ginputstream]: https://gjs-docs.gnome.org/gio20/gio.inputstream

### Gio.Application.runAsync()

Returns:
* (`Promise`)

Similar to [`Gio.Application.run`][gio-application-run] but return a Promise which resolves when
the main loop ends, instead of blocking while the main loop runs.

This helps avoid the situation where Promises never resolved if you didn't
run the application inside a callback.

[gio-application-run]: https://gjs-docs.gnome.org/gio20~2.0/gio.application#method-run

## [GLib](https://gitlab.gnome.org/GNOME/gjs/blob/HEAD/modules/core/overrides/GLib.js)

The `GLib` override includes a number of utilities and conveniences for working
with [`GLib.Variant`][gvariant], [`GLib.Bytes`][gbytes] and others.

See the [GVariant Tutorial][make-proxy-wrapper] for examples of working with
[`GLib.Variant`][gvariant] objects and the functions here.

### GLib.Bytes.toArray()

Returns:
* (`Uint8Array`) — A `Uint8Array`

Convert a [`GLib.Bytes`][gbytes] object to a `Uint8Array` object.

[gbytes]: https://gjs-docs.gnome.org/glib20/glib.bytes

### GLib.log_structured(logDomain, logLevel, stringFields)

> Note: This is an override for function normally available in GLib

Type:
* Static

Parameters:
* logDomain (`String`)  — A log domain, usually G_LOG_DOMAIN
* logLevel (`GLib.LogLevelFlags`)  — A log level, either from
  [`GLib.LogLevelFlags`][gloglevelflags], or a user-defined level
* stringFields (`{String: Any}`) — Key–value pairs of structured data to add to
  the log message

Log a message with structured data.

For more information about this function, see the upstream documentation
for [g_log_structured()][glogstructured].

[glogdomain]: https://gjs-docs.gnome.org/glib20/glib.log_domain
[gloglevelflags]: https://gjs-docs.gnome.org/glib20/glib.loglevelflags
[glogstructured]: https://docs.gtk.org/glib/func.log_structured.html

### GLib.Variant.unpack()

Returns:
* (`Any`) — A native JavaScript value, corresponding to the type of variant

A convenience for unpacking a single level of a [`GLib.Variant`][gvariant].

### GLib.Variant.deepUnpack()

Returns:
* (`Any`) — A native JavaScript value, corresponding to the type of variant

A convenience for unpacking a [`GLib.Variant`][gvariant] and its children, but
only up to one level.

### GLib.Variant.recursiveUnpack()

Returns:
* (`Any`) — A native JavaScript value, corresponding to the type of variant

A convenience for recursively unpacking a [`GLib.Variant`][gvariant] and all its
descendants.

Note that this method will unpack source values (e.g. `uint32`) to native values
(e.g. `Number`), so some type information may not be fully represented in the
result.

### GLib.MainLoop.runAsync()

Returns:
* (`Promise`)

Similar to [`GLib.MainLoop.run`][glib-mainloop-run] but return a Promise which resolves when
the main loop ends, instead of blocking while the main loop runs.

This helps avoid the situation where Promises never resolved if you didn't
run the main loop inside a callback.

[glib-mainloop-run]: https://gjs-docs.gnome.org/glib20/glib.mainloop#method-run

## [GObject](https://gitlab.gnome.org/GNOME/gjs/blob/HEAD/modules/core/overrides/GObject.js)

> See also: The [Mapping][mapping] documentation, for general GObject usage

The `GObject` override mostly provides aliases for constants and types normally
found in GObject, as well as [`GObject.registerClass()`](#gobject-registerclass)
for registering subclasses.

[mapping]: https://gjs-docs.gnome.org/gjs/mapping.md

### GObject.Object.$gtype

> See also: [GType Objects][gtype-objects]

Type:
* `GObject.Type`

The `GObject.Type` object for the given type.

This is the proper way to find the GType given an object instance or a class.
For a class, [`GObject.type_from_name()`][gtypefromname] can also be used.

```js
// expected output: [object GType for 'GObject']

// GType for an object class
log(GObject.Object.$gtype);

// GType for an object instance
const objectInstance = GObject.Object.new()
log(objectInstance.constructor.$gtype);

// GType from C type name
log(GObject.type_from_name('GObject'));
```

Note that the GType name for user-defined subclasses will be prefixed with
`Gjs_` (i.e. `Gjs_MyObject`), unless the `GTypeName` class property is specified
when calling [`GObject.registerClass()`](#gobject-registerclass).

Some applications, notably GNOME Shell, may set
[`GObject.gtypeNameBasedOnJSPath`](#gobject-gtypenamebasedonjspath) to `true`
which changes the prefix from `Gjs_` to `Gjs_<import path>`. For example, the
GNOME Shell class `Notification` in `ui/messageTray.js` has the GType name
`Gjs_ui_messageTray_Notification`.

[gtypefromname]: https://gjs-docs.gnome.org/gobject20/gobject.type_from_name
[gtype-objects]: https://gjs-docs.gnome.org/gjs/mapping.md#gtype-objects

### GObject.registerClass(metaInfo, klass)

Type:
* Static

Parameters:
* metaInfo (`Object`) — An optional dictionary of class properties
* klass (`class`) — A JavaScript class expression

Returns:
* (`GObject.Class`) — A registered `GObject.Class`

Registers a JavaScript class expression with the GObject type system. This
function supports both a two-argument and one-argument form.

In the two-argument form, the first argument is an object with meta info such as
properties and signals. The second argument is the class expression for the
class itself.

```js
var MyObject = GObject.registerClass({
    GTypeName: 'MyObject',
    Properties: { ... },
    Signals: { ... },
}, class MyObject extends GObject.Object {
    constructor() { ... }
});
```

In the one-argument form, the meta info object is omitted and only the class
expression is required.

```js
var MyObject = GObject.registerClass(
class MyObject extends GObject.Object {
    constructor() { ... }
});
```

See the [GObject Tutorial][gobject-subclassing] for examples of subclassing
GObject and declaring class properties.

[gobject-subclassing]: https://gjs.guide/guides/gobject/subclassing.html#subclassing-gobject

### GObject.ParamSpec

The `GObject` override contains aliases for the various `GParamSpec` types,
which are used when defining properties for a subclass. Be aware that the
arguments for `flags` and default values are reversed:

```js
// Original function
const pspec1 = GObject.param_spec_boolean('property1', 'nick', 'blurb',
    true,                         // default value
    GObject.ParamFlags.READABLE); // flags

// GJS alias
const pspec2 = GObject.ParamSpec.boolean('property2', 'nick', 'blurb',
    GObject.ParamFlags.READABLE,  // flags
    true);                        // default value
```

### GObject Signal Matches

This is an object passed to a number of signal matching functions. It has three
properties:

* signalId (`Number`) — A signal ID. Note that this is the signal ID, not a
  handler ID as returned from `GObject.Object.connect()`.
* detail (`String`) — A signal detail, such as `prop` in `notify::prop`.
* func (`Function`) — A signal callback function.

For example:

```js
// Note that `Function.prototype.bind()` creates a new function instance, so
// you must pass the correct instance to successfully match a handler
function notifyCallback(obj, pspec) {
    log(pspec.name);
}

const objectInstance = new GObject.Object();
const handlerId = objectInstance.connect('notify::property-name',
    notifyCallback);

const result = GObject.signal_handler_find(objectInstance, {
    detail: 'property-name',
    func: notifyCallback,
});

console.assert(result === handlerId);
```

### GObject.Object.connect(name, callback)

> See also: [GObject Signals Tutorial][gobject-signals-tutorial]

Parameters:
* name (`String`) — A detailed signal name
* callback (`Function`) — A callback function

Returns:
* (`Number`) — A signal handler ID

Connects a callback function to a signal for a particular object.

The first argument of the callback will be the object emitting the signal, while
the remaining arguments are the signal parameters.

The handler will be called synchronously, before the default handler of the
signal. `GObject.Object.emit()` will not return control until all handlers are
called.

For example:

```js
// A signal connection (emitted when any property changes)
let handler1 = obj.connect('notify', (obj, pspec) => {
    log(`${pspec.name} changed on ${obj.constructor.$gtype.name} object`);
});

// A signal name with detail (emitted when "property-name" changes)
let handler2 = obj.connect('notify::property-name', (obj, pspec) => {
    log(`${pspec.name} changed on ${obj.constructor.$gtype.name} object`);
});
```

[gobject-signals-tutorial]: https://gjs.guide/guides/gobject/basics.html#signals

### GObject.Object.connect_after(name, callback)

> See also: [GObject Signals Tutorial][gobject-signals-tutorial]

Parameters:
* name (`String`) — A detailed signal name
* callback (`Function`) — A callback function

Returns:
* (`Number`) — A signal handler ID

Connects a callback function to a signal for a particular object.

The first argument of the callback will be the object emitting the signal, while
the remaining arguments are the signal parameters.

The handler will be called synchronously, after the default handler of the
signal.

[gobject-signals-tutorial]: https://gjs.guide/guides/gobject/basics.html#signals

### GObject.Object.connect_object(name, callback, gobject, flags)

> See also: [GObject Signals Tutorial][gobject-signals-tutorial]

Parameters:
* name (`String`) — A detailed signal name
* callback (`Function`) — A callback function
* gobject (`GObject.Object`) — A [`GObject.Object`][gobject] instance
* flags (`GObject.ConnectFlags`) — Flags

Returns:
* (`Number`) — A signal handler ID

Connects a callback function to a signal for a particular object.

The `gobject` parameter is used to limit the lifetime of the connection. When the
object is destroyed, the callback will be disconnected automatically. The
`gobject` parameter is not otherwise used.

The first argument of the callback will be the object emitting the signal, while
the remaining arguments are the signal parameters.

If `GObject.ConnectFlags.AFTER` is specified in `flags`, the handler will be
called after the default handler of the signal. Otherwise, it will be called
before. `GObject.ConnectFlags.SWAPPED` is not supported and its use will
throw an exception.

[gobject-signals-tutorial]: https://gjs.guide/guides/gobject/basics.html#signals

### GObject.Object.disconnect(id)

> See also: [GObject Signals Tutorial][gobject-signals-tutorial]

Parameters:
* id (`Number`) — A signal handler ID

Disconnects a handler from an instance so it will not be called during any
future or currently ongoing emissions of the signal it has been connected to.

The `id` has to be a valid signal handler ID, connected to a signal of the
object.

For example:

```js
let handlerId = obj.connect('notify', (obj, pspec) => {
    log(`${pspec.name} changed on ${obj.constructor.$gtype.name} object`);
});

if (handlerId) {
    obj.disconnect(handlerId);
    handlerId = null;
}
```

[gobject-signals-tutorial]: https://gjs.guide/guides/gobject/basics.html#signals

### GObject.Object.emit(name, ...args)

> See also: [GObject Signals Tutorial][gobject-signals-tutorial]

Parameters:
* name (`String`) — A detailed signal name
* args (`Any`) — Signal parameters

Returns:
* (`Any`|`undefined`) — Optional return value

Emits a signal. Signal emission is done synchronously. The method will only
return control after all handlers are called or signal emission was stopped.

In some cases, signals expect a return value (usually a `Boolean`). The effect
of the return value will be described in the documentation for the signal.

For example:

```js
// Emitting a signal
obj.emit('signal-name', arg1, arg2);

// Emitting a signal that returns a boolean
if (obj.emit('signal-name', arg1, arg2))
    log('signal emission was handled!');
else
    log('signal emission was unhandled!');
```

[gobject-signals-tutorial]: https://gjs.guide/guides/gobject/basics.html#signals

### GObject.signal_handler_find(instance, match)

> Note: This function has a different signature that the original

Type:
* Static

Parameters:
* instance (`GObject.Object`) — A [`GObject.Object`][gobject] instance
* match (`Object`) — A dictionary of properties to match

Returns:
* (`Number`|`BigInt`|`Object`|`null`) — A valid non-0 signal handler ID for a
  successful match.

Finds the first signal handler that matches certain selection criteria.

The criteria are passed as properties of a match object. The match object has to
be non-empty for successful matches. If no handler was found, a falsy value is
returned.

[gobject]: https://gjs-docs.gnome.org/gobject20/gobject.object

### GObject.signal_handlers_block_matched(instance, match)

> Note: This function has a different signature that the original

Type:
* Static

Parameters:
* instance (`GObject.Object`) — A [`GObject.Object`][gobject] instance
* match (`Object`) — A dictionary of properties to match

Returns:
* (`Number`) — The number of handlers that matched.

Blocks all handlers on an instance that match certain selection criteria.

The criteria are passed as properties of a match object. The match object has to
have at least `func` for successful matches. If no handlers were found, 0 is
returned, the number of blocked handlers otherwise.

[gobject]: https://gjs-docs.gnome.org/gobject20/gobject.object

### GObject.signal_handlers_unblock_matched(instance, match)

> Note: This function has a different signature that the original

Type:
* Static

Parameters:
* instance (`GObject.Object`) — A [`GObject.Object`][gobject] instance
* match (`Object`) — A dictionary of properties to match

Returns:
* (`Number`) — The number of handlers that matched.

Unblocks all handlers on an instance that match certain selection criteria.

The criteria are passed as properties of a match object. The match object has to
have at least `func` for successful matches. If no handlers were found, 0 is
returned, the number of blocked handlers otherwise.

[gobject]: https://gjs-docs.gnome.org/gobject20/gobject.object

### GObject.signal_handlers_disconnect_matched(instance, match)

> Note: This function has a different signature that the original

Type:
* Static

Parameters:
* instance (`GObject.Object`) — A [`GObject.Object`][gobject] instance
* match (`Object`) — A dictionary of properties to match

Returns:
* (`Number`) — The number of handlers that matched.

Disconnects all handlers on an instance that match certain selection criteria.

The criteria are passed as properties of a match object. The match object has to
have at least `func` for successful matches. If no handlers were found, 0 is
returned, the number of blocked handlers otherwise.

[gobject]: https://gjs-docs.gnome.org/gobject20/gobject.object

### GObject.signal_handlers_block_by_func(instance, func)

Type:
* Static

Parameters:
* instance (`GObject.Object`) — A [`GObject.Object`][gobject] instance
* func (`Function`) — The callback function

Returns:
* (`Number`) — The number of handlers that matched.

Blocks all handlers on an instance that match `func`.

[gobject]: https://gjs-docs.gnome.org/gobject20/gobject.object

### GObject.signal_handlers_unblock_by_func(instance, func)

Type:
* Static

Parameters:
* instance (`GObject.Object`) — A [`GObject.Object`][gobject] instance
* func (`Function`) — The callback function

Returns:
* (`Number`) — The number of handlers that matched.

Unblocks all handlers on an instance that match `func`.

[gobject]: https://gjs-docs.gnome.org/gobject20/gobject.object

### GObject.signal_handlers_disconnect_by_func(instance, func)

Type:
* Static

Parameters:
* instance (`GObject.Object`) — A [`GObject.Object`][gobject] instance
* func (`Function`) — The callback function

Returns:
* (`Number`) — The number of handlers that matched.

Disconnects all handlers on an instance that match `func`.

[gobject]: https://gjs-docs.gnome.org/gobject20/gobject.object

### GObject.signal_handlers_disconnect_by_data(instance, data)

> Warning: This function does not work in GJS

Type:
* Static

Parameters:
* instance (`GObject.Object`) — A [`GObject.Object`][gobject] instance
* data (`void`) — The callback data

Returns:
* (`Number`) — The number of handlers that matched.

Disconnects all handlers on an instance that match `data`.

[gobject]: https://gjs-docs.gnome.org/gobject20/gobject.object

### GObject.gtypeNameBasedOnJSPath

> Warning: This property is for advanced use cases. Never set this property in
> a GNOME Shell Extension, or a loadable script in a GJS application.

Type:
* `Boolean`

Flags:
* Read / Write

The property controls the default prefix for the [GType name](#gtype-objects) of
a user-defined class, if not set manually.

By default this property is set to `false`, and any class that does not define
`GTypeName` when calling [`GObject.registerClass()`](#gobject-registerclass)
will be assigned a GType name of `Gjs_<JavaScript class name>`.

If set to `true`, the prefix will include the import path, which can avoid
conflicts if the application has multiple modules containing classes with the
same name. For example, the GNOME Shell class `Notification` in
`ui/messageTray.js` has the GType name `Gjs_ui_messageTray_Notification`.


## [Gtk](https://gitlab.gnome.org/GNOME/gjs/blob/HEAD/modules/core/overrides/Gtk.js)

Mostly GtkBuilder/composite template implementation. May be useful as a reference.

> Reminder: You should specify a version prior to importing a library with
> multiple versions.

```js
// GTK3
import Gtk from 'gi://Gtk?version=3.0';

// GTK4
import Gtk from 'gi://Gtk?version=4.0';
```

### Gtk.Container.list_child_properties(widget)

> Note: This GTK3 function requires different usage in GJS than other languages

Type:
* Static

Parameters:
* widget (`Gtk.Container`) — A [`Gtk.Container`][gtkcontainer]

Returns:
* (`Array(GObject.ParamSpec)`) — A list of the container's child properties as
  [`GObject.ParamSpec`][gparamspec] objects

Returns all child properties of a container class.

Note that in GJS, this is a static function on [`Gtk.Container`][gtkcontainer]
that must be called with `Function.prototype.call()`, either on a widget
instance or a widget class:

```js
// Calling on a widget instance
const box = new Gtk.Box();
const properties = Gtk.Container.list_child_properties.call(box);

for (let pspec of properties)
    log(pspec.name);

// Calling on a widget class
const properties = Gtk.Container.list_child_properties.call(Gtk.Box);

for (let pspec of properties)
    log(pspec.name);
```

For more information about this function, see the upstream documentation
for [gtk_container_class_list_child_properties()][gtkcontainerclasslistchildproperties].

[gtkwidget]: https://gjs-docs.gnome.org/gtk30/gtk.widget
[gtkcontainer]: https://gjs-docs.gnome.org/gtk30/gtk.container
[gtkcontainerclasslistchildproperties]: https://docs.gtk.org/gtk3/class_method.Container.list_child_properties.html
[gparamspec]: https://gjs-docs.gnome.org/gobject20/gobject.paramspec


## GObject Introspection

> See also: [ECMAScript Modules][esmodules]

The `gi` override is a wrapper for `libgirepository` for importing native
GObject-Introspection libraries.

[esmodules]: https://gjs-docs.gnome.org/gjs/esmodules.md

#### Import

```js
import gi from 'gi';
```

### gi.require(library, version)

Type:
* Static

Parameters:
* library (`String`) — A introspectable library
* version (`String`) — A library version, if applicable

> New in GJS 1.72 (GNOME 42)

Loads a native gobject-introspection library.
Version is required if more than one version of a library is installed.

You can also import libraries through the `gi://` URL scheme.

This function is only intended to be used when you want to import a library
conditionally, since top-level import statements are resolved statically.


## Legacy Imports

Prior to the introduction of [ES Modules](ESModules.md), GJS had its own import
system.

**imports** is a global object that you can use to import any js file or GObject
Introspection lib as module, there are 4 special properties of **imports**:

 * `searchPath`

    An array of path that used to look for files, if you want to prepend a path
    you can do something like `imports.searchPath.unshift(myPath)`.

 * `__modulePath__`
 * `__moduleName__`
 * `__parentModule__`

    These 3 properties is intended to be used internally, you should not use
    them.

Any other properties of **imports** is treated as a module, if you access these
properties, an import is attempted. Gjs try to look up a js file or directory by
property name from each location in `imports.searchPath`. For `imports.foo`, if
a file named `foo.js` is found, this file is executed and then imported as a
module object; else if a directory `foo` is found, a new importer object is
returned and its `searchPath` property is replaced by the path of `foo`.

Note that any variable, function and class declared at the top level, except
those declared by `let` or `const`, are exported as properties of the module
object, and one js file is executed only once at most even if it is imported
multiple times.

