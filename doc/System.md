# System

The `System` module provides common low-level facilities such as access to
process arguments and `exit()`, as well as a number of useful functions and
properties for debugging.

Note that the majority of the functions and properties in this module should not
be used in normal operation of a GJS application.

#### Import

When using ESModules:

```js
import System from 'system';
```

When using legacy imports:

```js
const System = imports.system;
```

### System.addressOf(object)

> See also: [`System.addressOfGObject()`](#system-addressofgobject)

Type:
* Static

Parameters:
* object (`Object`) — Any `Object`

Returns:
* (`String`) — A hexadecimal string (e.g. `0xb4f170f0`)

Return the memory address of any object as a string.

This is the address of memory being managed by the JavaScript engine, which may
represent a wrapper around memory elsewhere.

> Caution, don't use this as a unique identifier!
>
> JavaScript's garbage collector can move objects around in memory, or
> deduplicate identical objects, so this value may change during the execution
> of a program.

### System.addressOfGObject(gobject)

> See also: [`System.addressOf()`](#system-addressof)

Type:
* Static

Parameters:
* gobject (`GObject.Object`) — Any [`GObject.Object`][gobject]-derived instance

Returns:
* (`String`) — A hexadecimal string (e.g. `0xb4f170f0`)

> New in GJS 1.58 (GNOME 3.34)

Return the memory address of any GObject as a string.

[gobject]: https://gjs-docs.gnome.org/gobject20/gobject.object

### System.breakpoint()

> Warning: Using this function in code run outside of GDB will abort the process

Type:
* Static

Inserts a breakpoint instruction into the code.

With `System.breakpoint()` calls in your code, a GJS program can be debugged by
running it in GDB:

```
gdb --args gjs script.js
```

Once GDB has started, you can start the program with `run`. When the debugger
hits a breakpoint it will pause execution of the process and return to the
prompt. You can then use the standard `backtrace` command to print a C++ stack
trace, or use `call gjs_dumpstack()` to print a JavaScript stack trace:

```
(gdb) run
Starting program: /usr/bin/gjs -m script.js
...
Thread 1 "gjs" received signal SIGTRAP, Trace/breakpoint trap.
(gdb) call gjs_dumpstack()
== Stack trace for context 0x5555555b7180 ==
#0   555555640548 i   file:///path/to/script.js:4 (394b8c3cc060 @ 12)
#1   5555556404c8 i   file:///path/to/script.js:7 (394b8c3cc0b0 @ 6)
#2   7fffffffd3a0 b   self-hosted:2408 (394b8c3a9650 @ 753)
#3   5555556403e8 i   self-hosted:2355 (394b8c3a9600 @ 375)
(gdb)
```

To continue executing the program, you can use the `continue` (or `cont`) to
resume the process and debug further.

Remember that if you run the program outside of GDB, it will abort at the
breakpoint, so make sure to remove any calls to `System.breakpoint()` when
you're done debugging.

### System.clearDateCaches()

Type:
* Static

Clears the timezone cache.

This is a workaround for SpiderMonkey [Bug #1004706][bug-1004706].

[bug-1004706]: https://bugzilla.mozilla.org/show_bug.cgi?id=1004706

### System.dumpHeap(path)

See also: The [`heapgraph`][heapgraph] utility in the GJS repository

Type:
* Static

Parameters:
* path (`String`) — Optional file path

Dump a representation of internal heap memory. If `path` is not given, GJS will
write the contents to `stdout`.

[heapgraph]: https://gitlab.gnome.org/GNOME/gjs/blob/HEAD/tools/heapgraph.md

### System.dumpMemoryInfo(path)

Type:
* Static

Parameters:
* path (`String`) — Optional file path

> New in GJS 1.70 (GNOME 41)

Dump internal garbage collector statistics. If `path` is not given, GJS will
write the contents to `stdout`.

Example output:

```json
{
  "gcBytes": 794624,
  "gcMaxBytes": 4294967295,
  "mallocBytes": 224459,
  "gcIsHighFrequencyMode": true,
  "gcNumber": 1,
  "majorGCCount": 1,
  "minorGCCount": 1,
  "sliceCount": 1,
  "zone": {
    "gcBytes": 323584,
    "gcTriggerBytes": 42467328,
    "gcAllocTrigger": 36097228.8,
    "mallocBytes": 120432,
    "mallocTriggerBytes": 59768832,
    "gcNumber": 1
  }
}
```

### System.exit(code)

Type:
* Static

Parameters:
* code (`Number`) — An exit code

This works the same as C's `exit()` function; exits the program, passing a
certain error code to the shell. The shell expects the error code to be zero if
there was no error, or non-zero (any value you please) to indicate an error.

This value is used by other tools such as `make`; if `make` calls a program that
returns a non-zero error code, then `make` aborts the build.

### System.gc()

Type:
* Static

Run the garbage collector.

### System.programArgs

Type:
* `Array(String)`

> New in GJS 1.68 (GNOME 40)

A list of arguments passed to the current process.

This is effectively an alias for the global `ARGV`, which is misleading in that
it is not equivalent to the platform's `argv`.

### System.programInvocationName

Type:
* `String`

> New in GJS 1.68 (GNOME 40)

This property contains the name of the script as it was invoked from the command
line.

In C and other languages, this information is contained in the first element of
the platform's equivalent of `argv`, but GJS's `ARGV` only contains the
subsequent command-line arguments. In other words, `ARGV[0]` in GJS is the same
as `argv[1]` in C.

For example, passing ARGV to a `Gio.Application`/`Gtk.Application` (See also:
[examples/gtk-application.js][example-application]):

```js
import Gtk from 'gi://Gtk?version=3.0';
import System from 'system';

const myApp = new Gtk.Application();
myApp.connect("activate", () => log("activated"));
myApp.run([System.programInvocationName, ...ARGV]);
```

[example-application]: https://gitlab.gnome.org/GNOME/gjs/blob/HEAD/examples/gtk-application.js

### System.programPath

Type:
* `String`

> New in GJS 1.68 (GNOME 40)

The full path of the executed program.

### System.refcount(gobject)

Type:
* Static

Parameters:
* gobject (`GObject.Object`) — A [`GObject.Object`][gobject]

Return the reference count of any GObject-derived type. When an object's
reference count is zero, it is cleaned up and erased from memory.

[gobject]: https://gjs-docs.gnome.org/gobject20/gobject.object

### System.version

Type:
* `String`

This property contains version information about GJS.

