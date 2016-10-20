#!/bin/sh -e

gjs="$TOP_BUILDDIR"/gjs-console

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

fail () {
    >&2 echo "FAIL: $1"
    exit 1
}

# gjs --help prints GJS help
"$gjs" --help >/dev/null || \
    fail "--help should succeed"
test -n "`"$gjs" --help`" || \
    fail "--help should print something"

# print GJS help even if it's not the first argument
"$gjs" -I . --help >/dev/null || \
    fail "should succeed when --help is not first arg"
test -n "`"$gjs" -I . --help`" || \
    fail "should print something when --help is not first arg"

# --help before a script file name prints GJS help
"$gjs" --help help.js >/dev/null || \
    fail "--help should succeed before a script file"
test -n "`"$gjs" --help help.js`" || \
    fail "--help should print something before a script file"

# --help before a -c argument prints GJS help
script='imports.system.exit(1)'
"$gjs" --help -c "$script" >/dev/null || \
    fail "--help should succeed before -c"
test -n "`"$gjs" --help -c "$script"`" || \
    fail "--help should print something before -c"

# --help after a script file name is passed to the script
"$gjs" -I sentinel help.js --help || \
    fail "--help after script file should be passed to script"
test -z "`"$gjs" -I sentinel help.js --help`" || \
    fail "--help after script file should not print anything"

# --help after a -c argument is passed to the script
script='if(ARGV[0] !== "--help") imports.system.exit(1)'
"$gjs" -c "$script" --help || \
    fail "--help after -c should be passed to script"
test -z "`"$gjs" -c "$script" --help`" || \
    fail "--help after -c should not print anything"

# -I after a program is not consumed by GJS
if "$gjs" help.js --help -I sentinel; then
    fail "-I after script file should not be added to search path"
fi

rm -f help.js
