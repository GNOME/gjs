# Console

GJS implements the [WHATWG Console][whatwg-console] specification, with some
changes to accommodate GLib.

In particular, log severity is mapped to [`GLib.LogLevelFlags`][gloglevelflags]
and some methods are not implemented:

* `console.profile()`
* `console.profileEnd()`
* `console.timeStamp()`

#### Import

The functions in this module are available globally, without import.

[whatwg-console]: https://console.spec.whatwg.org/
[gloglevelflags]: https://gjs-docs.gnome.org/glib20/glib.loglevelflags

### console.assert(condition, ...data)

Type:
* Static

Parameters:
* condition (`Boolean`) — A boolean condition which, if `false`, causes the log
  to print
* data (`Any`) — Formatting substitutions, if applicable

> New in GJS 1.70 (GNOME 41)

Logs a critical message if the condition is not truthy.

See [`console.error()`](#console-error) for additional information.

### console.clear()

Type:
* Static

> New in GJS 1.70 (GNOME 41)

Resets grouping and clears the terminal on systems supporting ANSI terminal
control sequences.

In file-based stdout or systems which do not support clearing, `console.clear()`
has no visual effect.

### console.count(label)

Type:
* Static

Parameters:
* label (`String`) — Optional label

> New in GJS 1.70 (GNOME 41)

Logs how many times `console.count()` has been called with the given `label`.

See [`console.countReset()`](#console-countreset) for resetting a count.

### console.countReset(label)

Type:
* Static

Parameters:
* label (`String`) — The unique label to reset the count for

> New in GJS 1.70 (GNOME 41)

Resets a counter used with `console.count()`.

### console.debug(...data)

Type:
* Static

Parameters:
* data (`Any`) — Formatting substitutions, if applicable

> New in GJS 1.70 (GNOME 41)

Logs a message with severity equal to
[`GLib.LogLevelFlags.LEVEL_DEBUG`][gloglevelflagsdebug].

[gloglevelflagsdebug]: https://gjs-docs.gnome.org/glib20/glib.loglevelflags#default-level_debug

### console.dir(item, options)

Type:
* Static

Parameters:
* item (`Object`) — The item to display
* options (`undefined`) — Additional options for the formatter. Unused in GJS.

> New in GJS 1.70 (GNOME 41)

Resurively display all properties of `item`.

### console.dirxml(...data)

Type:
* Static

Parameters:
* data (`Any`) — Formatting substitutions, if applicable

> New in GJS 1.70 (GNOME 41)

Alias for [`console.log()`](#console-log)

### console.error(...data)

Type:
* Static

Parameters:
* data (`Any`) — Formatting substitutions, if applicable

> New in GJS 1.70 (GNOME 41)

Logs a message with severity equal to
[`GLib.LogLevelFlags.LEVEL_CRITICAL`][gloglevelflagscritical].

Does not use [`GLib.LogLevelFlags.LEVEL_ERROR`][gloglevelflagserror] to avoid
asserting and forcibly shutting down the application.

[gloglevelflagscritical]: https://gjs-docs.gnome.org/glib20/glib.loglevelflags#default-level_critical
[gloglevelflagserror]: https://gjs-docs.gnome.org/glib20/glib.loglevelflags#default-level_error

### console.group(...data)

Type:
* Static

Parameters:
* data (`Any`) — Formatting substitutions, if applicable

> New in GJS 1.70 (GNOME 41)

Creates a new inline group in the console log, causing any subsequent console
messages to be indented by an additional level, until `console.groupEnd()` is
called.

### console.groupCollapsed(...data)

Type:
* Static

Parameters:
* data (`Any`) — Formatting substitutions, if applicable

> New in GJS 1.70 (GNOME 41)

Alias for [`console.group()`](#console-group)

### console.groupEnd()

Type:
* Static

> New in GJS 1.70 (GNOME 41)

Exits the current inline group in the console log.

### console.info(...data)

Type:
* Static

Parameters:
* data (`Any`) — Formatting substitutions, if applicable

> New in GJS 1.70 (GNOME 41)

Logs a message with severity equal to
[`GLib.LogLevelFlags.LEVEL_INFO`][gloglevelflagsinfo].

[gloglevelflagsinfo]: https://gjs-docs.gnome.org/glib20/glib.loglevelflags#default-level_info

### console.log(...data)

Type:
* Static

Parameters:
* data (`Any`) — Formatting substitutions, if applicable

> New in GJS 1.70 (GNOME 41)

Logs a message with severity equal to
[`GLib.LogLevelFlags.LEVEL_MESSAGE`][gloglevelflagsmessage].

[gloglevelflagsmessage]: https://gjs-docs.gnome.org/glib20/glib.loglevelflags#default-level_message

### console.table(tabularData, options)

> Note: This is an alias for [`console.log()`](#console-log) in GJS

Type:
* Static

Parameters:
* tabularData (`Any`) — Formatting substitutions, if applicable
* properties (`undefined`) — Unsupported in GJS

> New in GJS 1.70 (GNOME 41)

Logs a message with severity equal to
[`GLib.LogLevelFlags.LEVEL_MESSAGE`][gloglevelflagsmessage].

[gloglevelflagsmessage]: https://gjs-docs.gnome.org/glib20/glib.loglevelflags#default-level_message

### console.time(label)

Type:
* Static

Parameters:
* label (`String`) — unique identifier for this action, pass to
  `console.timeEnd()` to complete

> New in GJS 1.70 (GNOME 41)

Starts a timer you can use to track how long an operation takes.

### console.timeEnd(label)

Type:
* Static

Parameters:
* label (`String`) — unique identifier for this action

> New in GJS 1.70 (GNOME 41)

Logs the time since the last call to `console.time(label)` and completes the
action.

Call `console.time(label)` again to re-measure.

### console.timeLog(label, ...data)

Type:
* Static

Parameters:
* label (`String`) — unique identifier for this action, pass to
  `console.timeEnd()` to complete
* data (`Any`) — Formatting substitutions, if applicable

> New in GJS 1.70 (GNOME 41)

Logs the time since the last call to `console.time(label)` where `label` is the
same.

### console.trace(...data)

Type:
* Static

Parameters:
* data (`Any`) — Formatting substitutions, if applicable

> New in GJS 1.70 (GNOME 41)

Outputs a stack trace to the console.

### console.warn(...data)

Type:
* Static

Parameters:
* data (`Any`) — Formatting substitutions, if applicable

> New in GJS 1.70 (GNOME 41)

Logs a message with severity equal to
[`GLib.LogLevelFlags.LEVEL_WARNING`][gloglevelflagswarning].

[gloglevelflagswarning]: https://gjs-docs.gnome.org/glib20/glib.loglevelflags#default-level_warning


## Log Domain

> New in GJS 1.70 (GNOME 41)

The log domain for the default global `console` object is set to `"Gjs-Console"`
by default, but can be changed if necessary. The three symbols of interest are
`setConsoleLogDomain()`, `getConsoleLogDomain()` and `DEFAULT_LOG_DOMAIN`.

You can import these symbols and modify the log domain like so:

```js
import { setConsoleLogDomain, getConsoleLogDomain, DEFAULT_LOG_DOMAIN } from 'console';

// Setting the log domain
setConsoleLogDomain('my.app.id');

// expected output: my.app.id-Message: 12:21:17.899: cool
console.log('cool');

// Checking and resetting the log domain
if (getConsoleLogDomain() !== DEFAULT_LOG_DOMAIN)
    setConsoleLogDomain(DEFAULT_LOG_DOMAIN);

// expected output: Gjs-Console-Message: 12:21:17.899: cool
console.log('cool');
```

