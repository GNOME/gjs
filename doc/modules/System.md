# [System](https://gitlab.gnome.org/GNOME/gjs/blob/master/modules/system.cpp)

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

[example-application]: https://gitlab.gnome.org/GNOME/gjs/blob/master/examples/gtk-application.js
