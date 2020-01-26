# Hacking on GJS #

## Setting up ##

First of all, if you are contributing C++ code, install the handy git
commit hook that will autoformat your code when you commit it.
In your GJS checkout directory, run
`tools/git-pre-commit-format install`.
For more information, see
<https://github.com/barisione/clang-format-hooks/>.
(You can skip this step if it doesn't work for you, but in that case
you'll need to manually format your code before it gets merged.
You can also skip this step if you are not writing any C++ code.)

GJS requires four other libraries to be installed: GLib, libffi,
gobject-introspection, and SpiderMonkey (also called "mozjs68" on some
systems.)
The readline library is not required, but strongly recommended.
We recommend installing your system's development packages for GLib,
libffi, gobject-introspection, and readline.
(For example, on Ubuntu you would run
`sudo apt-get install libglib2.0-dev libffi-dev libreadline-dev libgirepository1.0-dev libreadline-dev`.)
But, if your system's versions of these packages aren't new enough, then
the build process will download and build sufficient versions.

SpiderMonkey cannot be auto-installed, so you will need to install it
either through your system's package manager, or building it yourself.
Even if your system includes a development package for SpiderMonkey, we
still recommend building it if you are going to do any development on
GJS so that you can enable the debugging features.
These debugging features reduce performance by quite a lot, but they
will help catch mistakes in the API that could otherwise go unnoticed
and cause crashes in gnome-shell later on.

To build SpiderMonkey, follow the instructions on [this page][embedder]
to download the source code and build the library.
If you are using `-Dprefix` to build GJS into a different path, then
make sure to use the same build prefix for SpiderMonkey with `--prefix`.

## First build ##

To build GJS, change to your checkout directory, and run:
```sh
meson _build
ninja -C _build
```

Add any options with `-D` arguments to the `meson _build` command.
For a list of available options, run `meson configure`.

To install GJS into the path you chose with `-Dprefix`, (or into
`/usr/local` if you didn't choose a path), run
`ninja -C _build install`, adding `sudo` if necessary.

## Making Sure Your Stuff Doesn't Break Anything Else ##

Make your changes in your GJS checkout directory, then run
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

## Debugging ##

Mozilla has some pretty-printers that make debugging JSAPI code easier.
Unfortunately they're not included in most packaged distributions of
mozjs, but you can grab them from your built copy of mozjs.

After reaching a breakpoint in your program, type this to activate the
pretty-printers:
```sh
source /path/to/spidermonkey/js/src/_build/shell/js-gdb.py
```

(replace `/path/to/spidermonkey` with the path to your SpiderMonkey
sources)

## Checking Things More Thoroughly Before A Release ##

### GC Zeal ###

Run the test suite with "GC zeal" to make non-deterministic GC errors
more likely to show up.

To see which GC zeal options are available:
```sh
JS_GC_ZEAL=-1 js68
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

### Valgrind ###

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

### Static Code Analysis ###

To execute cppcheck, a static code analysis tool for the C and C++, run:
```sh
tools/run_cppcheck.sh
```
It is a versatile tool that can check non-standard code, including: variable 
checking, bounds checking, leaks, etc. It can detect the types of bugs that
the compilers normally fail to detect.

### Sanitizers ###

To build GJS with support for the ASan and UBSan sanitizers, configure
meson like this:
```sh
meson _build -Db_sanitize=address,undefined
```
and then run the tests as normal.

### Test Coverage ###

To generate a test coverage report, run this script:
```sh
tools/run_coverage.sh
gio open _coverage/html/index.html
```
This will build GJS into a separate build directory with code coverage
instrumentation enabled, run the test suite to collect the coverage
data, and open the generated HTML report.

[embedder](https://github.com/spidermonkey-embedders/spidermonkey-embedding-examples/blob/esr68/docs/Building%20SpiderMonkey.md)