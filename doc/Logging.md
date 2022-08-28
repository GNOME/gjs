# Logging

GJS includes a number of built-in functions for logging and aiding debugging, in
addition to those available as a part of the GNOME APIs.

In most cases, the [`console`][console] suite of functions should be preferred
for logging in GJS.

#### Import

The functions in this module are available globally, without import.

[console]: https://gjs-docs.gnome.org/gjs/console.md

### log(message)

> See also: [`console.log()`][console-log]

Type:
* Static

Parameters:
* message (`Any`) — A string or any coercible value

Logs a message with severity equal to
[`GLib.LogLevelFlags.LEVEL_MESSAGE`][gloglevelflagsmessage].

```js
// expected output: JS LOG: Some message
log('Some message');

// expected output: JS LOG: [object Object]
log({key: 'value'});
```

[console-log]: https://gjs-docs.gnome.org/gjs/console.md#console-log
[gloglevelflagsmessage]: https://gjs-docs.gnome.org/glib20/glib.loglevelflags#default-level_message

### logError(error, prefix)

> See also: [`console.trace()`][console-trace]

Type:
* Static

Parameters:
* error (`Error`) — An `Error` or [`GLib.Error`][gerror] object
* prefix (`String`) — Optional prefix for the message

Logs a stack trace for `error`, with an optional prefix, with severity equal to
[`GLib.LogLevelFlags.LEVEL_WARNING`][gloglevelflagswarning].

This function is commonly used in conjunction with `try...catch` blocks to log
errors while still trapping the exception:

```js
try {
    throw new Error('Some error occured');
} catch (e) {
    logError(e, 'FooError');
}
```

It can also be passed directly to the `catch()` clause of a `Promise` chain:

```js
Promise.reject().catch(logError);
```

[console-trace]: https://gjs-docs.gnome.org/gjs/console.md#console-trace
[gerror]: https://gjs-docs.gnome.org/glib20/glib.error
[gloglevelflagswarning]: https://gjs-docs.gnome.org/glib20/glib.loglevelflags#default-level_warning

### print(...messages)

> Note: this function is not useful for GNOME Shell extensions

Type:
* Static

Parameters:
* messages (`Any`) — Any number of strings or coercible values

Takes any number of strings (or values that can be coerced to strings), joins
them with a space and appends a newline character (`\n`).

The resulting string is printed directly to `stdout` of the current process with
[`g_print()`][gprint].

```js
$ gjs -c "print('foobar', 42, {});"
foobar 42 [object Object]
$ 
```

[gprint]: https://docs.gtk.org/glib/func.print.html

### printerr(...messages)

> Note: this function is not useful for GNOME Shell extensions

Type:
* Static

Parameters:
* messages (`Any`) — Any number of strings or coercible values

Takes any number of strings (or values that can be coerced to strings), joins
them with a space and appends a newline character (`\n`).

The resulting string is printed directly to `stderr` of the current process with
[`g_printerr()`][gprinterr].

```js
$ gjs -c "printerr('foobar', 42, {});"
foobar 42 [object Object]
$ 
```

[gprinterr]: https://docs.gtk.org/glib/func.printerr.html

