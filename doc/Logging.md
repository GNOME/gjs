GJS includes a number of built-in functions for logging and aiding debugging, in
addition to those available as a part of the GNOME APIs.

# Built-in Functions

GJS includes four built-in logging functions: `log()`, `logError()`, `print()`
and `printerr()`. These functions are available globally (ie. without import)
and the source for these is found in [global.cpp][global-cpp].

### log()

`log()` is the most basic function available, taking a single argument as a
`String` or other object which can be coerced to a `String`. The string, or
object coerced to a string, is logged with `g_message()` from GLib.

```js
// expected output: JS LOG: Some message
log('Some message');

// expected output: JS LOG: [object Object]
log(new Object());
```

### logError()

`logError()` is a more useful function for debugging that logs the stack trace of
a JavaScript `Error()` object, with an optional prefix.

It is commonly used in conjunction with `try...catch` blocks to log errors while
still trapping the exception. An example in `gjs-console` with a backtrace:

```js
$ gjs
gjs> try {
....     throw new Error('Some error occured');
.... } catch (e) {
....     logError(e, 'FooError');
.... }

(gjs:28115): Gjs-WARNING **: 19:28:13.334: JS ERROR: FooError: Error: Some error occurred
@typein:2:16
@<stdin>:1:34
```


### print() & printerr()

`print()` takes any number of string (or coercible) arguments, joins them with a
space and appends a newline (`\n`). The resulting message will be printed
directly to `stdout` of the current process using `g_print()`.

`printerr()` is exactly like `print()`, except the resulting message is printed
to `stderr` with `g_printerr()`.

These functions are generally less useful for debugging code in programs that
embed GJS like GNOME Shell, where it is less convenient to access the `stdout`
and `stderr` pipes.

```js
$ gjs
gjs> print('some', 'string', 42);
some string 42$
gjs> printerr('some text 42');
some text
```


# GLib Functions

Aside from the built-in functions in GJS, many functions from GLib can be used
to log messages at different severity levels and assist in debugging.

Below is a common pattern for defining a series of logging functions as used in
[Polari][polari] and some other GJS applications:

```js
const GLib = imports.gi.GLib;

var LOG_DOMAIN = 'Polari';

function _makeLogFunction(level) {
    return message => {
        let stack = (new Error()).stack;
        let caller = stack.split('\n')[1];

        // Map from resource- to source location
        caller = caller.replace('resource:///org/gnome/Polari/js', 'src');

        let [code, line] = caller.split(':');
        let [func, file] = code.split(/\W*@/);
        GLib.log_structured(LOG_DOMAIN, level, {
            'MESSAGE': `${message}`,
            'SYSLOG_IDENTIFIER': 'org.gnome.Polari',
            'CODE_FILE': file,
            'CODE_FUNC': func,
            'CODE_LINE': line
        });
    };
}

globalThis.log      = _makeLogFunction(GLib.LogLevelFlags.LEVEL_MESSAGE);
globalThis.debug    = _makeLogFunction(GLib.LogLevelFlags.LEVEL_DEBUG);
globalThis.info     = _makeLogFunction(GLib.LogLevelFlags.LEVEL_INFO);
globalThis.warning  = _makeLogFunction(GLib.LogLevelFlags.LEVEL_WARNING);
globalThis.critical = _makeLogFunction(GLib.LogLevelFlags.LEVEL_CRITICAL);
globalThis.error    = _makeLogFunction(GLib.LogLevelFlags.LEVEL_ERROR);

// Log all messages when connected to the journal
if (GLib.log_writer_is_journald(2))
    GLib.setenv('G_MESSAGES_DEBUG', LOG_DOMAIN, false);
```

[global-cpp]: https://gitlab.gnome.org/GNOME/gjs/blob/HEAD/gjs/global.cpp
[polari]: https://gitlab.gnome.org/GNOME/polari/blob/HEAD/src/main.js

