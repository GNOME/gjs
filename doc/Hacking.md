# Hacking on GJS #

## Setting up ##

We use the
[Google style guide](https://google.github.io/styleguide/cppguide.html)
for C++ code, with a few exceptions, 4-space indents being the main one.
There is a handy git commit hook that will autoformat your code when you
commit it.
In your GJS checkout directory, run
`tools/git-pre-commit-format install`.
For more information, see
<https://github.com/barisione/clang-format-hooks/>.

For the time being, we recommend using JHBuild to develop GJS.
Follow the [instructions from GNOME](https://wiki.gnome.org/HowDoI/Jhbuild) for [JHBuild](https://git.gnome.org/browse/jhbuild/).

Even if your system includes a development package for mozjs, we
recommend building it on JHBuild so that you can enable the debugging
features. Add this to your JHBuild configuration file:
```python
module_autogenargs['mozjs52'] = '--enable-debug'
```

Make sure it is built first with `jhbuild build mozjs52`, otherwise
`jhbuild build gjs` will skip it if you have the system package
installed.

Debugging features in mozjs reduce performance by quite a lot, in
exchange for performing many runtime checks that can alert you when
you're not using the JS API correctly.

## Making Sure Your Stuff Doesn't Break Anything Else ##

Each changeset should ensure that the test suite still passes.
In fact, each commit should ensure that the test suite still passes,
though there are some exceptions to this rule.
You can run the test suite with `jhbuild make check`.

## Debugging ##

Mozilla has some pretty-printers that make debugging JSAPI code easier.
Unfortunately they're not included in most packaged distributions of
mozjs, but you can grab them from your JHBuild copy of mozjs.

After reaching a breakpoint in your program, type this to activate the
pretty-printers:
```
source ~/.cache/jhbuild/build/mozjs-52.Y.Z/js/src/shell/js-gdb.py
```

(replace `Y.Z` with mozjs's minor and micro version numbers)

## Checking Things More Thoroughly Before A Release ##

### Distcheck ###

Run `jhbuild make distcheck` once before every release to make sure the
package builds and the tests run from a clean release tarball.
If there are any errors, they must be fixed before making the release.

### GC Zeal ###

Run the test suite with "GC zeal" to make non-deterministic GC errors
more likely to show up.

To see which GC zeal options are available:
```sh
JS_GC_ZEAL=-1 jhbuild run gjs
```

To run the test suite under it:
```sh
JS_GC_ZEAL=... jhbuild make check
```

Good parameters for `...` are `1`, `2,100`, `2,1` (really slow and can
cause the tests to time out), `4`, `6`, `7`, `11`.

Failures in mode 4 (pre barrier verification) usually point to a GC
thing not being traced when it should have been. Failures in mode 11
(post barrier verification) usually point to a weak pointer's location
not being updated after GC moved it.

### Valgrind ###

Valgrind catches memory leak errors in the C++ code.
It's a good idea to run the test suite under Valgrind before each
release.

To run the test suite under a succession of Valgrind tools:
```sh
jhbuild make check-valgrind
```

The logs from each run will be in `~/.cache/jhbuild/build/gjs/test-suite-<toolname>.log`, where `<toolname>` is `drd`, `helgrind`, `memcheck`, and `sgcheck`.

Note that LeakSanitizer, part of ASan (see below) can catch many, but
not all, errors that Valgrind can catch.
LSan executes faster than Valgrind, however.

### Static Code Analysis ###

To execute cppcheck, a static code analysis tool for the C and C++, run:
```sh
jhbuild make cppcheck
```
It is a versatile tool that can check non-standard code, including: variable 
checking, bounds checking, leaks, etc. It can detect the types of bugs that
the compilers normally fail to detect.

### Sanitizers ###

To add instrumentation code to gjs, put this (both, or any one of them) in
your JHBuild configuration file:
```python
module_autogenargs['gjs'] = '--enable-asan --enable-ubsan'
```

Sanitizers are based on compile-time instrumentation. They are available
in gcc and clang for a range of supported operating systems and
platforms.

Please, keep in mind that instrumentation is limited by execution coverage. So,
if your "testing" session never reaches a particular point of execution, then
instrumentation at that point collects no data.

### Test Coverage ###

To generate a test coverage report, put this in your JHBuild
configuration file:
```python
module_autogenargs['gjs'] = '--enable-code-coverage'
```

Then run:
```sh
jhbuild cleanone gjs
jhbuild buildone gjs
jhbuild make check-code-coverage
xdg-open ~/.cache/jhbuild/build/gjs/gjs-X.Y.Z-coverage/index.html
```

(replace `X.Y.Z` with the version number, e.g. `1.48.0`)

[JHBuild](https://wiki.gnome.org/HowDoI/Jhbuild)