# Hacking on GJS

## Quick start

If you are looking to get started quickly, then you can clone GJS using
GNOME Builder and choose the `org.gnome.GjsConsole` build configuration.

For the most part, you will be able to build GJS with the Build button
and run the interpreter with the Run button.
If you need to issue any of the Meson commands manually, make sure to do
so in a runtime terminal (Ctrl+Alt+T) rather than a build terminal or a
regular terminal.

## Setting up

First of all, download the GJS source code using Git.
Go to [GJS on GitLab](https://gitlab.gnome.org/GNOME/gjs), and click
"Fork" near the top right of the page.
Then, click the "Clone" button that's located a bit under the "Fork"
button, and click the little clipboard icon next to "Clone with SSH" or
"Clone with HTTPS", to copy the address to your clipboard.
Go to your terminal, and type `git clone` and then paste the address
into your terminal with Shift+Ctrl+V.
(Don't forget Shift! It's important when pasting into a terminal.)
This will download the GJS source code into a `gjs` directory.

If you are contributing C++ code, install the handy git
commit hook that will autoformat your code when you commit it.
In your `gjs` directory, run
`tools/git-pre-commit-format install`.
For more information, see
<https://github.com/barisione/clang-format-hooks/>.
(You can skip this step if it doesn't work for you, but in that case
you'll need to manually format your code before it gets merged.
You can also skip this step if you are not writing any C++ code.)

## Dependencies

GJS requires five other libraries to be installed: GLib, libffi,
gobject-introspection, SpiderMonkey (also called "mozjs140" on some
systems.) and the build tool Meson.
The readline library is not required, but strongly recommended.
We recommend installing your system's development packages for libffi,
gobject-introspection, Meson and readline.

<details>
    <summary>Ubuntu</summary>
    <code>sudo apt-get install libffi-dev libreadline-dev libgirepository1.0-dev meson</code>
</details>

<details>
    <summary>Fedora</summary>
    <code>sudo dnf install libffi readline-devel gobject-introspection-devel meson</code>
</details>

But, if your system's versions of these packages aren't new enough, then
the build process will download and build sufficient versions.
(Temporarily, until GNOME 49 is released, GJS requires a development
version of GLib, so the build process will always download GLib.)

SpiderMonkey cannot be auto-installed, so you will need to install it
either through your system's package manager, or building it yourself.
Even if your system includes a development package for SpiderMonkey, we
still recommend building it if you are going to do any development on
GJS's C++ code so that you can enable the debugging features.
These debugging features reduce performance by quite a lot, but they
will help catch mistakes in the API that could otherwise go unnoticed
and cause crashes in gnome-shell later on.

If you aren't writing any C++ code, and your system provides it (for
example, Fedora 41 or Ubuntu 24.10 and later versions), then you don't
need to build it yourself.
Install SpiderMonkey using your system's package manager instead:

<details>
    <summary>Ubuntu</summary>
    <code>sudo apt-get install libmozjs-140-dev</code>
</details>

<details>
    <summary>Fedora</summary>
    <code>sudo dnf install mozjs140-devel</code>
</details>

If you _are_ writing C++ code, then please build SpiderMonkey yourself
with the debugging features enabled.
This can save you time later when you submit your merge request, because
the code will be checked using the debugging features.

To build SpiderMonkey, follow the instructions on [this page](https://github.com/mozilla-spidermonkey/spidermonkey-embedding-examples/blob/esr140/docs/Building%20SpiderMonkey.md) to download the source code and build the library.
If you are using `-Dprefix` to build GJS into a different path, then
make sure to use the same build prefix for SpiderMonkey with `--prefix`.

## First build

Temporarily, until GNOME 49 is released, you will need to first run:
```sh
export GI_TYPELIB_PATH=$(pkg-config --variable=typelibdir girepository-2.0)
```
You need to do this once per terminal session, and you can put it in
your shell profile file if you want to do it automatically.

To build GJS, change to your `gjs` directory, and run:
```sh
meson setup _build
ninja -C _build
```

Add any options with `-D` arguments to the `meson setup _build` command.
For a list of available options, run `meson configure`.

That's it! You can now run your build of gjs for testing and hacking with

```sh
meson devenv -C _build gjs-console ../script.js
```
(the path `../script.js` is relative to `_build`, not the root folder)

To install GJS into the path you chose with `-Dprefix`, (or into
`/usr/local` if you didn't choose a path), run
`ninja -C _build install`, adding `sudo` if necessary.

## Making Sure Your Stuff Doesn't Break Anything Else

Make your changes in your `gjs` directory, then run
`ninja -C _build` to build a modified copy of GJS.

Each changeset should ensure that the test suite still passes.
In fact, each commit should ensure that the test suite still passes,
though there are some exceptions to this rule.
You can run the test suite with `meson test -C _build`.

For some contributions, it's a good idea to test your modified version
of GJS with GNOME Shell.
For this, you might want to use JHBuild to build GJS instead, and run
it with `jhbuild run gnome-shell --replace`.
You need to be logged into an Xorg session, not Wayland, for this to
work.

## Debugging

Mozilla has some pretty-printers that make debugging JSAPI code easier.
Unfortunately they're not included in most packaged distributions of
mozjs, but you can grab them from your built copy of mozjs.

After reaching a breakpoint in your program, type this to activate the
pretty-printers:
```sh
source /path/to/spidermonkey/js/src/_build/js/src/shell/js-gdb.py
```

(replace `/path/to/spidermonkey` with the path to your SpiderMonkey
sources)

## Getting a stack trace

Run your program with `gdb --args gjs myfile.js`.
This will drop you into the GDB debugger interface.

Enter `r` to start the program.

When it segfaults, enter `bt full` to get the C++ stack trace, and enter
`call gjs_dumpstack()` to get the JS stack trace.
(It may need to be `call (void) gjs_dumpstack()` if you don't have debugging
symbols installed.)

Enter `q` to quit.

## Checking Things More Thoroughly Before A Release

### GC Zeal

Run the test suite with "GC zeal" to make non-deterministic GC errors
more likely to show up.

To see which GC zeal options are available:
```sh
JS_GC_ZEAL=-1 js140
```

We include three test setups, `extra_gc`, `pre_verify`, and
`post_verify`, for the most useful modes: `2,1`, `4`, and `11`
respectively.
Run them like this (replace `extra_gc` with any of the other names):
```sh
meson test -C _build --setup=extra_gc
```

Failures in mode `pre_verify` usually point to a GC thing not being
traced when it should have been.
Failures in mode `post_verify` usually point to a weak pointer's
location not being updated after GC moved it.

### Valgrind

Valgrind catches memory leak errors in the C++ code.
It's a good idea to run the test suite under Valgrind before each
release.

To run the test suite under Valgrind's memcheck tool:
```sh
meson test -C _build --setup=valgrind
```

The logs from each run will be in
`_build/meson-logs/testlog-valgrind.txt`.

Note that LeakSanitizer, part of ASan (see below) can catch many, but
not all, errors that Valgrind can catch.
LSan executes faster than Valgrind, however.

### Static Code Analysis

To execute cppcheck, a static code analysis tool for the C and C++, run:
```sh
tools/run_cppcheck.sh
```
It is a versatile tool that can check non-standard code, including: variable
checking, bounds checking, leaks, etc. It can detect the types of bugs that
the compilers normally fail to detect.

### Sanitizers

To build GJS with support for the ASan and UBSan sanitizers, configure
meson like this:
```sh
meson setup _build -Db_sanitize=address,undefined
```
and then run the tests as normal.

### Test Coverage

To generate a test coverage report, run this script:
```sh
tools/run_coverage.sh
gio open _coverage/html/index.html
```
This will build GJS into a separate build directory with code coverage
instrumentation enabled, run the test suite to collect the coverage
data, and open the generated HTML report.

[embedder](https://github.com/spidermonkey-embedders/spidermonkey-embedding-examples/blob/esr140/docs/Building%20SpiderMonkey.md)

## Troubleshooting

### I sent a merge request from my fork but CI does not pass.

Check the job log, most likely you missed the following

> The container registry is not enabled in $USERNAME/gjs, enable it in the project general settings panel

* Go to your fork general setting, for example https://gitlab.gnome.org/$USERNAME/gjs/edit
* Expand "Visibility, project features, permissions"
* Enable "Container registry"
* Hit "Save changes"
