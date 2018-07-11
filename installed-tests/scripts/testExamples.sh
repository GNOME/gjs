#!/bin/sh
DIR="$( cd "$( dirname "${0}" )" && pwd )"
source "${DIR}"/common.sh

# Run the examples
$gjs examples/gio-cat.js Makefile
report "run the gio-cat.js example"

if [[ -n "${ENABLE_GTK}" ]]; then
    timeout 5s $gjs examples/calc.js
    report_timeout "run the calc.js example"

    timeout 5s $gjs examples/gtk.js
    report_timeout "run the gtk.js example"

    timeout 5s $gjs examples/gtk-application.js
    report_timeout "run the gtk-application.js example"

    timeout 5s $gjs examples/gettext.js
    report_timeout "run the gettext.js example"
else
    skip "run the calc.js example" "running without GTK"
    skip "run the gtk.js example" "running without GTK"
    skip "run the gtk-application.js example" "running without GTK"
    skip "run the gettext.js example" "running without GTK"
fi
echo "1..$total"
