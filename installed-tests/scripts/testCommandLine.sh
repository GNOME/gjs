#!/bin/sh

if test "$GJS_USE_UNINSTALLED_FILES" = "1"; then
    gjs="$LOG_COMPILER $LOG_FLAGS $TOP_BUILDDIR/gjs-console"
else
    gjs="$LOG_COMPILER $LOG_FLAGS gjs-console"
fi

# Avoid interference in the profiler tests from stray environment variable
unset GJS_ENABLE_PROFILER

# Avoid interference in the warning tests from G_DEBUG=fatal-warnings/criticals
OLD_G_DEBUG="$G_DEBUG"

# This JS script should exit immediately with code 42. If that is not working,
# then it will exit after 3 seconds as a fallback, with code 0.
cat <<EOF >exit.js
const GLib = imports.gi.GLib;
let loop = GLib.MainLoop.new(null, false);
GLib.idle_add(GLib.PRIORITY_LOW, () => imports.system.exit(42));
GLib.timeout_add_seconds(GLib.PRIORITY_HIGH, 3, () => loop.quit());
loop.run();
EOF

# this JS script fails if either 1) --help is not passed to it, or 2) the string
# "sentinel" is not in its search path
cat <<EOF >help.js
const System = imports.system;
if (imports.searchPath.indexOf('sentinel') == -1)
    System.exit(1);
if (ARGV.indexOf('--help') == -1)
    System.exit(1);
System.exit(0);
EOF

# this JS script should print one string (jobs are run before the interpreter
# finishes) and should not print the other (jobs should not be run after the
# interpreter is instructed to quit)
cat <<EOF >promise.js
const System = imports.system;
Promise.resolve().then(() => {
    print('Should be printed');
    System.exit(42);
});
Promise.resolve().then(() => print('Should not be printed'));
EOF

# this JS script should not cause an unhandled promise rejection
cat <<EOF >awaitcatch.js
async function foo() { throw new Error('foo'); }
async function bar() {
    try {
        await foo();
    } catch (e) {}
}
bar();
EOF

total=0

report () {
    exit_code=$?
    total=$((total + 1))
    if test $exit_code -eq 0; then
        echo "ok $total - $1"
    else
        echo "not ok $total - $1"
    fi
}

report_xfail () {
    exit_code=$?
    total=$((total + 1))
    if test $exit_code -ne 0; then
        echo "ok $total - $1"
    else
        echo "not ok $total - $1"
    fi
}

skip () {
    total=$((total + 1))
    echo "ok $total - $1 # SKIP $2"
}

# Test that System.exit() works in gjs-console
$gjs -c 'imports.system.exit(0)'
report "System.exit(0) should exit successfully"
$gjs -c 'imports.system.exit(42)'
test $? -eq 42
report "System.exit(42) should exit with the correct exit code"

# FIXME: should check -eq 42 specifically, but in debug mode we will be
# hitting an assertion. For this reason, skip when running under valgrind
# since nothing will be freed.
echo "# VALGRIND = $VALGRIND"
if test -z $VALGRIND; then
    $gjs exit.js
    test $? -ne 0
    report "System.exit() should still exit across an FFI boundary"
else
    skip "System.exit() should still exit across an FFI boundary" "running under valgrind"
fi

# ensure the encoding of argv is being properly handled
$gjs -c 'imports.system.exit((ARGV[0] !== "Valentín") ? 1 : 0)' "Valentín"
report "Basic unicode encoding (accents, etc) should be functioning properly for ARGV and imports."
$gjs -c 'imports.system.exit((ARGV[0] !== "☭") ? 1 : 0)' "☭"
report "Unicode encoding for symbols should be functioning properly for ARGV and imports."

# gjs --help prints GJS help
$gjs --help >/dev/null
report "--help should succeed"
test -n "$($gjs --help)"
report "--help should print something"

# print GJS help even if it's not the first argument
$gjs -I . --help >/dev/null
report "should succeed when --help is not first arg"
test -n "$($gjs -I . --help)"
report "should print something when --help is not first arg"

# --help before a script file name prints GJS help
$gjs --help help.js >/dev/null
report "--help should succeed before a script file"
test -n "$($gjs --help help.js)"
report "--help should print something before a script file"

# --help before a -c argument prints GJS help
script='imports.system.exit(1)'
$gjs --help -c "$script" >/dev/null
report "--help should succeed before -c"
test -n "$($gjs --help -c "$script")"
report "--help should print something before -c"

# --help after a script file name is passed to the script
$gjs -I sentinel help.js --help
report "--help after script file should be passed to script"
test -z "$($gjs -I sentinel help.js --help)"
report "--help after script file should not print anything"

# --help after a -c argument is passed to the script
script='if(ARGV[0] !== "--help") imports.system.exit(1)'
$gjs -c "$script" --help
report "--help after -c should be passed to script"
test -z "$($gjs -c "$script" --help)"
report "--help after -c should not print anything"

# -I after a program is not consumed by GJS
# Temporary behaviour: still consume the argument, but give a warning
# "$gjs" help.js --help -I sentinel
# report_xfail "-I after script file should not be added to search path"
# fi
G_DEBUG=$(echo "$G_DEBUG" | sed -e 's/fatal-warnings,\{0,1\}//')
$gjs help.js --help -I sentinel 2>&1 | grep -q 'Gjs-WARNING.*--include-path'
report "-I after script should succeed but give a warning"
$gjs -c 'imports.system.exit(0)' --coverage-prefix=foo --coverage-output=foo 2>&1 | grep -q 'Gjs-WARNING.*--coverage-prefix'
report "--coverage-prefix after script should succeed but give a warning"
$gjs -c 'imports.system.exit(0)' --coverage-prefix=foo --coverage-output=foo 2>&1 | grep -q 'Gjs-WARNING.*--coverage-output'
report "--coverage-output after script should succeed but give a warning"
rm -f foo/coverage.lcov
G_DEBUG="$OLD_G_DEBUG"

for version_arg in --version --jsversion; do
    # --version and --jsversion work
    $gjs $version_arg >/dev/null
    report "$version_arg should work"
    test -n "$($gjs $version_arg)"
    report "$version_arg should print something"

    # --version and --jsversion after a script go to the script
    script="if(ARGV[0] !== '$version_arg') imports.system.exit(1)"
    $gjs -c "$script" $version_arg
    report "$version_arg after -c should be passed to script"
    test -z "$($gjs -c "$script" $version_arg)"
    report "$version_arg after -c should not print anything"
done

# --profile
rm -f gjs-*.syscap foo.syscap
$gjs -c 'imports.system.exit(0)' && ! stat gjs-*.syscap &> /dev/null
report "no profiling data should be dumped without --profile"

# Skip some tests if built without profiler support
if $gjs --profile -c 1 2>&1 | grep -q 'Gjs-Message.*Profiler is disabled'; then
    reason="profiler is disabled"
    skip "--profile should dump profiling data to the default file name" "$reason"
    skip "--profile with argument should dump profiling data to the named file" "$reason"
    skip "GJS_ENABLE_PROFILER=1 should enable the profiler" "$reason"
else
    rm -f gjs-*.syscap
    $gjs --profile -c 'imports.system.exit(0)' && stat gjs-*.syscap &> /dev/null
    report "--profile should dump profiling data to the default file name"
    rm -f gjs-*.syscap
    $gjs --profile=foo.syscap -c 'imports.system.exit(0)' && test -f foo.syscap
    report "--profile with argument should dump profiling data to the named file"
    rm -f foo.syscap && rm -f gjs-*.syscap
    GJS_ENABLE_PROFILER=1 $gjs -c 'imports.system.exit(0)' && stat gjs-*.syscap &> /dev/null
    report "GJS_ENABLE_PROFILER=1 should enable the profiler"
    rm -f gjs-*.syscap
fi

# interpreter handles queued promise jobs correctly
output=$($gjs promise.js)
test $? -eq 42
report "interpreter should exit with the correct exit code from a queued promise job"
test -n "$output" -a -z "${output##*Should be printed*}"
report "interpreter should run queued promise jobs before finishing"
test -n "${output##*Should not be printed*}"
report "interpreter should stop running jobs when one calls System.exit()"

$gjs -c "Promise.resolve().then(() => { throw new Error(); });" 2>&1 | grep -q 'Gjs-WARNING.*Unhandled promise rejection.*[sS]tack trace'
report "unhandled promise rejection should be reported"
test -z "$($gjs awaitcatch.js)"
report "catching an await expression should not cause unhandled rejection"
# https://gitlab.gnome.org/GNOME/gjs/issues/18
G_DEBUG=$(echo "$G_DEBUG" | sed -e 's/fatal-warnings,\{0,1\}//')
$gjs -c "(async () => await true)(); void foobar;" 2>&1 | grep -q 'Script .* threw an exception'
report "main program exceptions are not swallowed by queued promise jobs"
G_DEBUG="$OLD_G_DEBUG"

# https://gitlab.gnome.org/GNOME/gjs/issues/26
$gjs -c 'new imports.gi.Gio.Subprocess({argv: ["true"]}).init(null);'
report "object unref from other thread after shutdown should not race"

# https://gitlab.gnome.org/GNOME/gjs/issues/212
if test -n "$ENABLE_GTK"; then
    G_DEBUG=$(echo "$G_DEBUG" | sed -e 's/fatal-warnings,\{0,1\}//' -e 's/fatal-criticals,\{0,1\}//')
    $gjs -c 'imports.gi.versions.Gtk = "3.0";
             const Gtk = imports.gi.Gtk;
             const GObject = imports.gi.GObject;
             Gtk.init(null);
             let BadWidget = GObject.registerClass(class BadWidget extends Gtk.Widget {
                vfunc_destroy() {};
             });
             let w = new BadWidget ();'
    report "avoid crashing when GTK vfuncs are called on context destroy"
    G_DEBUG="$OLD_G_DEBUG"
else
    skip "avoid crashing when GTK vfuncs are called on context destroy" "GTK disabled"
fi

rm -f exit.js help.js promise.js awaitcatch.js

echo "1..$total"
