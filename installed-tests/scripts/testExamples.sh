#!/bin/sh
DIR="$( cd "$( dirname "${0}" )" && pwd )"
source "${DIR}"/common.sh

# Run the examples
$gjs examples/gio-cat.js Makefile
report "run the gio-cat.js example"

$gjs examples/gtk.js _AUTO_QUIT  #remove
report "run the example gtk.js"

if [[ -n "${ENABLE_GTK}" ]]; then
    $gjs examples/calc.js _AUTO_QUIT
    report "run the calc.js example"

    $gjs examples/gtk.js _AUTO_QUIT
    report "run the gtk.js example"

    $gjs examples/gettext.js _AUTO_QUIT
    report "run the gettext.js example"
fi
echo "1..$total"
