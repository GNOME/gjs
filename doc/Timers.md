# Timers

GJS implements the [WHATWG Timers][whatwg-timers] specification, with some
changes to accommodate the GLib event loop.

In particular, the returned value of `setInterval()` and `setTimeout()` is not a
`Number`, but a [`GLib.Source`][gsource].

#### Import

The functions in this module are available globally, without import.

[whatwg-timers]: https://html.spec.whatwg.org/multipage/timers-and-user-prompts.html#timers
[gsource]: https://gjs-docs.gnome.org/glib20/glib.source

### setInterval(handler, timeout, ...arguments)

Type:
* Static

Parameters:
* handler (`Function`) — The callback to invoke
* timeout (`Number`) — Optional interval in milliseconds
* arguments (`Array(Any)`) — Optional arguments to pass to `handler`

Returns:
* (`GLib.Source`) — The identifier of the repeated action

> New in GJS 1.72 (GNOME 42)

Schedules a timeout to run `handler` every `timeout` milliseconds. Any
`arguments` are passed straight through to the `handler`.

### clearInterval(id)

Type:
* Static

Parameters:
* id (`GLib.Source`) — The identifier of the interval you want to cancel.

> New in GJS 1.72 (GNOME 42)

Cancels the timeout set with `setInterval()` or `setTimeout()` identified by
`id`.

### setTimeout(handler, timeout, ...arguments)

Type:
* Static

Parameters:
* handler (`Function`) — The callback to invoke
* timeout (`Number`) — Optional timeout in milliseconds
* arguments (`Array(Any)`) — Optional arguments to pass to `handler`

Returns:
* (`GLib.Source`) — The identifier of the repeated action

> New in GJS 1.72 (GNOME 42)

Schedules a timeout to run `handler` after `timeout` milliseconds. Any
`arguments` are passed straight through to the `handler`.

### clearTimeout(id)

Type:
* Static

Parameters:
* id (`GLib.Source`) — The identifier of the timeout you want to cancel.

> New in GJS 1.72 (GNOME 42)

Cancels the timeout set with `setTimeout()` or `setInterval()` identified by
`id`.

