#!/bin/sh
# SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
# SPDX-FileCopyrightText: 2016 Endless Mobile, Inc.
# SPDX-FileCopyrightText: 2016 Philip Chimento <philip.chimento@gmail.com>

DIR="$( cd "$( dirname "${0}" )" && pwd )"
# shellcheck source=installed-tests/scripts/common.sh
. "${DIR}"/common.sh

scratch="$(mktemp -d)" || exit 1
trap 'rm -rf "$scratch"' EXIT
# convert signals into exits for shells that don't run the EXIT trap when killed
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

cd "$scratch" || exit 1

# Avoid interference in the warning tests from G_DEBUG=fatal-warnings/criticals
OLD_G_DEBUG="$G_DEBUG"

$gjs --invalid-option >/dev/null 2>/dev/null
report_xfail "Invalid option should exit with failure"
$gjs --invalid-option 2>&1 | grep -q invalid-option
report "Invalid option should print a relevant message"

# Test that System.exit() works in gjs-console
$gjs -c 'imports.system.exit(0)'
report "System.exit(0) should exit successfully"
$gjs -c 'imports.system.exit(42)'
test $? -eq 42
report "System.exit(42) should exit with the correct exit code"

# Test the System.programPath works in gjs-console
$gjs "$fixtures/argv.js"
report "System.programPath should end in '/argv.js' when gjs argv.js is run"

# FIXME: should check -eq 42 specifically, but in debug mode we will be
# hitting an assertion. For this reason, skip when running under valgrind
# since nothing will be freed. Also suppress LSan for the same reason.
echo "# VALGRIND = $VALGRIND"
if test -z "$VALGRIND"; then
    ASAN_OPTIONS=detect_leaks=0 $gjs "$fixtures/exit.js"
    test $? -ne 0
    report "System.exit() should still exit across an FFI boundary"

    # https://gitlab.gnome.org/GNOME/gjs/-/issues/417
    output="$(ASAN_OPTIONS=detect_leaks=0 $gjs "$fixtures/promiseexit.js" 2>&1)"
    test $? -ne 0 && printf '%s' "$output" | grep -q "Unhandled promise rejection"
    report "Unhandled promise rejections should still be printed when exiting"
else
    skip "System.exit() should still exit across an FFI boundary" "running under valgrind"
    skip "Unhandled promise rejections should still be printed when exiting" "running under valgrind"
fi

# ensure the encoding of argv is being properly handled
$gjs -c 'imports.system.exit((ARGV[0] !== "Valentín") ? 1 : 0)' "Valentín"
report "Basic unicode encoding (accents, etc) should be functioning properly for ARGV and imports."
$gjs -c 'imports.system.exit((ARGV[0] !== "☭") ? 1 : 0)' "☭"
report "Unicode encoding for symbols should be functioning properly for ARGV and imports."

# ensure unicode paths are supported
mkdir Код
touch Код/🍁.js
$gjs -m Код/🍁.js
report "Unicode pathed encoding should work for module run."

# non UTF8 file names throws error
if bash -c "touch $'\xff'" 2>/dev/null; then
    G_FILENAME_ENCODING=utf8 $gjs -m "$fixtures/printFiles.js" 2>&1 | \
        grep -q 'Gjs-CRITICAL.*Could not convert filename string to UTF-8 for string: \\377'
    report "Throws error if filename is not UTF8"
else
    skip "Throws error if filename is not UTF8" "filesystem doesn't support non UTF8 file names."
fi

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
$gjs --help "$fixtures/help.js" >/dev/null
report "--help should succeed before a script file"
test -n "$($gjs --help "$fixtures/help.js")"
report "--help should print something before a script file"

# --help before a -c argument prints GJS help
script='imports.system.exit(1)'
$gjs --help -c "$script" >/dev/null
report "--help should succeed before -c"
test -n "$($gjs --help -c "$script")"
report "--help should print something before -c"

# --help after a script file name is passed to the script
$gjs -I sentinel "$fixtures/help.js" --help
report "--help after script file should be passed to script"
test -z "$($gjs -I sentinel "$fixtures/help.js" --help)"
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
$gjs "$fixtures/help.js" --help -I sentinel 2>&1 | grep -q 'Gjs-WARNING.*--include-path'
report "-I after script should succeed but give a warning"
$gjs -c 'imports.system.exit(0)' --coverage-prefix=foo --coverage-output=foo 2>&1 | grep -q 'Gjs-WARNING.*--coverage-prefix'
report "--coverage-prefix after script should succeed but give a warning"
$gjs -c 'imports.system.exit(0)' --coverage-prefix=foo --coverage-output=foo 2>&1 | grep -q 'Gjs-WARNING.*--coverage-output'
report "--coverage-output after script should succeed but give a warning"
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
$gjs -c 'imports.system.exit(0)' && ! stat gjs-*.syscap > /dev/null 2>&1
report "no profiling data should be dumped without --profile"

# Skip some tests if built without profiler support
if $gjs --profile -c 1 2>&1 | grep -q 'Gjs-Message.*Profiler is disabled'; then
    reason="profiler is disabled"
    skip "--profile should dump profiling data to the default file name" "$reason"
    skip "--profile with argument should dump profiling data to the named file" "$reason"
    skip "GJS_ENABLE_PROFILER=1 should enable the profiler" "$reason"
else
    rm -f gjs-*.syscap
    $gjs --profile -c 'imports.system.exit(0)' && stat gjs-*.syscap > /dev/null 2>&1
    report "--profile should dump profiling data to the default file name"
    $gjs --profile=foo.syscap -c 'imports.system.exit(0)' && test -f foo.syscap
    report "--profile with argument should dump profiling data to the named file"
    rm -f gjs-*.syscap
    GJS_ENABLE_PROFILER=1 $gjs -c 'imports.system.exit(0)' && stat gjs-*.syscap > /dev/null 2>&1
    report "GJS_ENABLE_PROFILER=1 should enable the profiler"
fi

# interpreter handles queued promise jobs correctly
output=$($gjs "$fixtures/promise.js")
test $? -eq 42
report "interpreter should exit with the correct exit code from a queued promise job"
test -n "$output" -a -z "${output##*Should be printed*}"
report "interpreter should run queued promise jobs before finishing"
test -n "${output##*Should not be printed*}"
report "interpreter should stop running jobs when one calls System.exit()"

$gjs -c "Promise.resolve().then(() => { throw new Error(); });" 2>&1 | grep -q 'Gjs-WARNING.*Unhandled promise rejection.*[sS]tack trace'
report "unhandled promise rejection should be reported"
test -z "$($gjs "$fixtures/awaitcatch.js")"
report "catching an await expression should not cause unhandled rejection"
# https://gitlab.gnome.org/GNOME/gjs/issues/18
G_DEBUG=$(echo "$G_DEBUG" | sed -e 's/fatal-warnings,\{0,1\}//')
$gjs -c "(async () => await true)(); void foobar;" 2>&1 | grep -q 'ReferenceError: foobar is not defined'
report "main program exceptions are not swallowed by queued promise jobs"
G_DEBUG="$OLD_G_DEBUG"

# https://gitlab.gnome.org/GNOME/gjs/issues/26
$gjs -c 'new imports.gi.Gio.Subprocess({argv: ["true"]}).init(null);'
report "object unref from other thread after shutdown should not race"

# https://gitlab.gnome.org/GNOME/gjs/issues/212
if test -n "$ENABLE_GTK"; then
    G_DEBUG=$(echo "$G_DEBUG" | sed -e 's/fatal-warnings,\{0,1\}//' -e 's/fatal-criticals,\{0,1\}//')
    $gjs "$fixtures/badWidget.js"
    report "avoid crashing when GTK vfuncs are called on context destroy"
    G_DEBUG="$OLD_G_DEBUG"
else
    skip "avoid crashing when GTK vfuncs are called on context destroy" "GTK disabled"
fi

# https://gitlab.gnome.org/GNOME/gjs/-/issues/322
$gjs --coverage-prefix="$PWD" --coverage-output="$PWD" "$fixtures/awaitcatch.js"
grep -q TN: coverage.lcov
report "coverage prefix is treated as an absolute path"

$gjs -m "$fixtures/doublegi.js" 2>&1 | grep -q 'already loaded'
report "avoid statically importing two versions of the same module"

# https://gitlab.gnome.org/GNOME/gjs/-/issues/19
echo "# VALGRIND = $VALGRIND"
if test -z "$VALGRIND"; then
    output=$(env LSAN_OPTIONS=detect_leaks=0 ASAN_OPTIONS=detect_leaks=0 \
        "$gjs" -m "$fixtures/signalexit.js")
    test $? -eq 15
    report "exit with correct code from a signal callback"
    test -n "$output" -a -z "${output##*click 1*}"
    report "avoid asserting when System.exit is called from a signal callback"
    test -n "${output##*click 2*}"
    report "exit after first System.exit call in a signal callback"
else
    skip "exit with correct code from a signal callback" "running under valgrind"
    skip "avoid asserting when System.exit is called from a signal callback" "running under valgrind"
    skip "exit after first System.exit call in a signal callback" "running under valgrind"
fi

# https://gitlab.gnome.org/GNOME/gjs/-/issues/671
output=$(LC_ALL=C $gjs "$fixtures/int.js")
test "$output" = "0036 0030"
report "%Id prints Latin digits in C locale $output"
output=$(LC_ALL=en_CA $gjs "$fixtures/int.js")
test "$output" = "0036 0030"
report "%Id prints Latin digits in en_CA locale $output"
output=$(LC_ALL=fa_IR $gjs "$fixtures/int.js")
test "$output" = "06f6 06f0"
report "%Id prints Persian digits in fa_IR locale $output"
output=$(LC_ALL=ar_EG $gjs "$fixtures/int.js")
test "$output" = "0666 0660"
report "%Id prints Arabic digits in ar_EG locale $output"

echo "1..$total"
