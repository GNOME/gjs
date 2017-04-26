#!/bin/sh

if test "$GJS_USE_UNINSTALLED_FILES" = "1"; then
    gjs="$TOP_BUILDDIR/gjs-console"
else
    gjs="gjs-console"
fi

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

# Test that System.exit() works in gjs-console
"$gjs" -c 'imports.system.exit(0)'
report "System.exit(0) should exit successfully"
"$gjs" -c 'imports.system.exit(42)'
test $? -eq 42
report "System.exit(42) should exit with the correct exit code"

# FIXME: should check -eq 42 specifically, but in debug mode we will be
# hitting an assertion
"$gjs" exit.js
test $? -ne 0
report "System.exit() should still exit across an FFI boundary"

# gjs --help prints GJS help
"$gjs" --help >/dev/null
report "--help should succeed"
test -n "$("$gjs" --help)"
report "--help should print something"

# print GJS help even if it's not the first argument
"$gjs" -I . --help >/dev/null
report "should succeed when --help is not first arg"
test -n "$("$gjs" -I . --help)"
report "should print something when --help is not first arg"

# --help before a script file name prints GJS help
"$gjs" --help help.js >/dev/null
report "--help should succeed before a script file"
test -n "$("$gjs" --help help.js)"
report "--help should print something before a script file"

# --help before a -c argument prints GJS help
script='imports.system.exit(1)'
"$gjs" --help -c "$script" >/dev/null
report "--help should succeed before -c"
test -n "$("$gjs" --help -c "$script")"
report "--help should print something before -c"

# --help after a script file name is passed to the script
"$gjs" -I sentinel help.js --help
report "--help after script file should be passed to script"
test -z "$("$gjs" -I sentinel help.js --help)"
report "--help after script file should not print anything"

# --help after a -c argument is passed to the script
script='if(ARGV[0] !== "--help") imports.system.exit(1)'
"$gjs" -c "$script" --help
report "--help after -c should be passed to script"
test -z "$("$gjs" -c "$script" --help)"
report "--help after -c should not print anything"

# -I after a program is not consumed by GJS
# Temporary behaviour: still consume the argument, but give a warning
# "$gjs" help.js --help -I sentinel
# report_xfail "-I after script file should not be added to search path"
# fi
"$gjs" help.js --help -I sentinel 2>&1 | grep -q 'Gjs-WARNING.*--include-path'
report "-I after script should succeed but give a warning"
"$gjs" -c 'imports.system.exit(0)' --coverage-prefix=foo --coverage-output=foo 2>&1 | grep -q 'Gjs-WARNING.*--coverage-prefix'
report "--coverage-prefix after script should succeed but give a warning"
"$gjs" -c 'imports.system.exit(0)' --coverage-prefix=foo --coverage-output=foo 2>&1 | grep -q 'Gjs-WARNING.*--coverage-output'
report "--coverage-output after script should succeed but give a warning"
rm -f foo/coverage.lcov

# --version works
"$gjs" --version >/dev/null
report "--version should work"
test -n "$("$gjs" --version)"
report "--version should print something"

# --version after a script goes to the script
script='if(ARGV[0] !== "--version") imports.system.exit(1)'
"$gjs" -c "$script" --version
report "--version after -c should be passed to script"
test -z "$("$gjs" -c "$script" --version)"
report "--version after -c should not print anything"

rm -f exit.js help.js

echo "1..$total"
