#!/bin/sh
# SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
# SPDX-FileCopyrightText: 2026 Philip Chimento <philip.chimento@gmail.com>

DIR="$( cd "$( dirname "${0}" )" && pwd )"
# shellcheck source=installed-tests/scripts/common.sh
. "${DIR}"/common.sh

scratch="$(mktemp -d)" || exit 1
trap 'rm -rf "$scratch"' EXIT
# convert signals into exits for shells that don't run the EXIT trap when killed
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

$gjs -p >/dev/null 2>&1
report_xfail "option parsing fails with incomplete import map arg"

output=$($gjs -p fake-import-map.json -c 1 2>&1)
test $? -eq 1
report "nonexistent import map arg exits early"
printf '%s' "$output" | grep -q 'fake-import-map.json'
report "nonexistent import map should print a relevant message"

G_RESOURCE_OVERLAYS="/org/gjs/importmaptest=$fixtures" \
    $gjs -p resource:///org/gjs/importmaptest/importMapGLib.json \
    -m "$fixtures/importMapImporter.js" glib >/dev/null 2>&1
report "import map can be resource URI"

# All tests below are expected to log warnings
G_DEBUG=$(echo "$G_DEBUG" | sed -e 's/fatal-warnings,\{0,1\}//')

output=$($gjs -p "$fixtures/importMapBuiltinInvalid.json" -m "$fixtures/importMapImporter.js" system 2>&1)
report_xfail "built-in module mapped to an invalid address should block the import"
printf '%s' "$output" | grep -q 'Gjs-WARNING.*Import map'
report "built-in module mapped to an invalid address should warn about the invalid entry"

output=$($gjs -p "$fixtures/importMapBuiltinNonString.json" -m "$fixtures/importMapImporter.js" gettext 2>&1)
report_xfail "built-in module mapped to a non-string should block the import"
printf '%s' "$output" | grep -q 'Gjs-WARNING.*Import map'
report "built-in module mapped to a non-string should warn about the invalid entry"

output=$($gjs -p "$fixtures/importMapNonString.json" -m "$fixtures/importMapImporter.js" gi://GLib 2>&1)
report_xfail "URL mapped to a non-string should block the import"
printf '%s' "$output" | grep -q 'Gjs-WARNING.*Import map'
report "URL mapped to a non-string should warn about the invalid entry"

# The importer is served from a resource so that the scope can name its URL.
# The invalid entry in the scope must not fall back to the top-level imports.
output=$(G_RESOURCE_OVERLAYS="/org/gjs/importmaptest=$fixtures" \
    $gjs -p "$fixtures/importMapScopeInvalid.json" \
    -m resource:///org/gjs/importmaptest/importMapImporter.js glib 2>&1)
report_xfail "invalid entry in a scope should block the import"
printf '%s' "$output" | grep -q 'Gjs-WARNING.*Import map'
report "invalid entry in a scope should warn about the invalid entry"

# A prefix mapping needs a directory to map to, so create one
mkdir "$scratch/sub"
echo 'export default 42;' > "$scratch/sub/x.js"
cat > "$scratch/importMapSlashMismatch.json" <<EOF
{"imports": {"lib/": "file://$scratch/sub/x.js"}}
EOF

output=$($gjs -p "$scratch/importMapSlashMismatch.json" -m "$fixtures/importMapImporter.js" lib/x.js 2>&1)
report_xfail "prefix mapped to an address without trailing slash should block the import"
printf '%s' "$output" | grep -q 'Gjs-WARNING.*Import map'
report "prefix mapped to an address without trailing slash should warn about the invalid entry"

# There is no base URL, so relative URLs in an import map are invalid

output=$($gjs -p "$fixtures/importMapRelativeKey.json" -m "$fixtures/importMapImporter.js" gi://GLib 2>&1)
printf '%s' "$output" | grep -q 'Gjs-WARNING.*specifier key cannot be relative'
report "relative specifier key should warn"

output=$($gjs -p "$fixtures/importMapRelativeAddress.json" -m "$fixtures/importMapImporter.js" gi://GLib 2>&1)
printf '%s' "$output" | grep -q 'Gjs-WARNING.*invalid address: ./glib.js'
report "relative address should warn"

output=$($gjs -p "$fixtures/importMapAbsolutePathAddress.json" -m "$fixtures/importMapImporter.js" gi://GLib 2>&1)
printf '%s' "$output" | grep -q 'Gjs-WARNING.*invalid address: /glib.js'
report "absolute path address should warn"

output=$($gjs -p "$fixtures/importMapRelativeScope.json" -m "$fixtures/importMapImporter.js" gi://GLib 2>&1)
printf '%s' "$output" | grep -q 'Gjs-WARNING.*scope prefix is not parseable: ./'
report "relative scope prefix should warn"

echo "1..$total"
