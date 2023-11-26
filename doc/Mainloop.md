# Mainloop

The `Mainloop` module is a convenience layer for some common event loop methods
in GLib, such as [`GLib.timeout_add()`][gtimeoutadd].

This module is not generally recommended, but is documented for the sake of
existing code. Each method below contains links to the corresponding GLib
method for reference.

For an introduction to the GLib event loop, see the
[Asynchronous Programming Tutorial][async-tutorial].

[async-tutorial]: https://gjs.guide/guides/gjs/asynchronous-programming.html
[gtimeoutadd]: https://gjs-docs.gnome.org/glib20/glib.timeout_add

#### Import

> Attention: This module is not available as an ECMAScript Module

The `Mainloop` module is available on the global `imports` object:

```js
const Mainloop = imports.mainloop
```

### Mainloop.idle_add(handler, priority)

> See also: [`GLib.idle_add()`][gidleadd]

Type:
* Static

Parameters:
* handler (`Function`) — The function to call
* priority (`Number`) — Optional priority

Returns:
* (`GLib.Source`) — The newly-created idle source

Adds a function to be called whenever there are no higher priority events
pending. If the function returns `false` it is automatically removed from the
list of event sources and will not be called again.

If not given, `priority` defaults to `GLib.PRIORITY_DEFAULT_IDLE`.

[gidleadd]: https://gjs-docs.gnome.org/glib20/glib.idle_add

### Mainloop.idle_source(handler, priority)

> See also: [`GLib.idle_source_new()`][gidlesourcenew]

Type:
* Static

Parameters:
* handler (`Function`) — The function to call
* priority (`Number`) — Optional priority

Returns:
* (`GLib.Source`) — The newly-created idle source

Creates a new idle source.

If not given, `priority` defaults to `GLib.PRIORITY_DEFAULT_IDLE`.

[gidlesourcenew]: https://gjs-docs.gnome.org/glib20/glib.idle_source_new

### Mainloop.quit(name)

> See also: [`GLib.MainLoop.quit()`][gmainloopquit]

Type:
* Static

Parameters:
* name (`String`) — Optional name

Stops a main loop from running. Any calls to `Mainloop.run(name)` for the loop
will return.

If `name` is given, this function will create a new [`GLib.MainLoop`][gmainloop]
if necessary.

[gmainloop]: https://gjs-docs.gnome.org/glib20/glib.mainloop
[gmainloopquit]: https://gjs-docs.gnome.org/glib20/glib.mainloop#method-quit

### Mainloop.run(name)

> See also: [`GLib.MainLoop.run()`][gmainlooprun]

Type:
* Static

Parameters:
* name (`String`) — Optional name

Runs a main loop until `Mainloop.quit()` is called on the loop.

If `name` is given, this function will create a new [`GLib.MainLoop`][gmainloop]
if necessary.

[gmainloop]: https://gjs-docs.gnome.org/glib20/glib.mainloop
[gmainlooprun]: https://gjs-docs.gnome.org/glib20/glib.mainloop#method-run

### Mainloop.source_remove(id)

> See also: [`GLib.Source.remove()`][gsourceremove]

Type:
* Static

Parameters:
* id (`Number`) — The ID of the source to remove

Returns:
* (`Boolean`) — For historical reasons, this function always returns `true`

Removes the source with the given ID from the default main context.

[gsourceremove]: https://gjs-docs.gnome.org/glib20/glib.source#function-remove

### Mainloop.timeout_add(timeout, handler, priority)

> See also: [`GLib.timeout_add()`][gtimeoutadd]

Type:
* Static

Parameters:
* timeout (`Number`) — The timeout interval in milliseconds
* handler (`Function`) — The function to call
* priority (`Number`) — Optional priority

Returns:
* (`GLib.Source`) — The newly-created timeout source

Sets a function to be called at regular intervals, with the given priority. The
function is called repeatedly until it returns `false`, at which point the
timeout is automatically destroyed and the function will not be called again.

The scheduling granularity/accuracy of this source will be in milliseconds. If
not given, `priority` defaults to `GLib.PRIORITY_DEFAULT`.

[gtimeoutadd]: https://gjs-docs.gnome.org/glib20/glib.timeout_add

### Mainloop.timeout_add_seconds(timeout, handler, priority)

> See also: [`GLib.timeout_add_seconds()`][gtimeoutaddseconds]

Type:
* Static

Parameters:
* timeout (`Number`) — The timeout interval in seconds
* handler (`Function`) — The function to call
* priority (`Number`) — Optional priority

Returns:
* (`GLib.Source`) — The newly-created timeout source

Sets a function to be called at regular intervals, with the given priority. The
function is called repeatedly until it returns `false`, at which point the
timeout is automatically destroyed and the function will not be called again.

The scheduling granularity/accuracy of this source will be in seconds. If not
given, `priority` defaults to `GLib.PRIORITY_DEFAULT`.

[gtimeoutaddseconds]: https://gjs-docs.gnome.org/glib20/glib.timeout_add_seconds

### Mainloop.timeout_source(timeout, handler, priority)

> See also: [`GLib.timeout_source_new()`][gtimeoutsourcenew]

Type:
* Static

Parameters:
* timeout (`Number`) — The timeout interval in milliseconds
* handler (`Function`) — The function to call
* priority (`Number`) — Optional priority

Returns:
* (`GLib.Source`) — The newly-created timeout source

Creates a new timeout source.

The scheduling granularity/accuracy of this source will be in milliseconds. If
not given, `priority` defaults to `GLib.PRIORITY_DEFAULT`.

[gtimeoutsourcenew]: https://gjs-docs.gnome.org/glib20/glib.timeout_source_new

### Mainloop.timeout_seconds_source(timeout, handler, priority)

> See also: [`GLib.timeout_source_new_seconds()`][gtimeoutsourcenewseconds]

Type:
* Static

Parameters:
* timeout (`Number`) — The timeout interval in seconds
* handler (`Function`) — The function to call
* priority (`Number`) — Optional priority

Returns:
* (`GLib.Source`) — The newly-created timeout source

Creates a new timeout source.

The scheduling granularity/accuracy of this source will be in seconds. If not
given, `priority` defaults to `GLib.PRIORITY_DEFAULT`.

[gtimeoutsourcenewseconds]: https://gjs-docs.gnome.org/glib20/glib.timeout_source_new_seconds

