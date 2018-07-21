#!/bin/bash
DIR="$( cd "$( dirname "${0}" )" && pwd )"
source "${DIR}"/common.sh

# Run the examples
$gjs examples/gio-cat.js Makefile
report "run the gio-cat.js example"

$gjs installed-tests/js/extra/test-get-json.js
report "run the test-get-json.js example"

# Expected to fail since it can't find invert-match of "hello there"
$gjs installed-tests/js/extra/test-byte-array.js > byte-array.log
grep -q --invert-match "hello there" byte-array.log
report "run the test-byte-array.js example" #GJS bug

if [[ -n "${ENABLE_GTK}" ]]; then
    timeout 5s $gjs examples/calc.js
    report_timeout "run the calc.js example"

    timeout 5s $gjs examples/gtk.js
    report_timeout "run the gtk.js example"

    timeout 5s $gjs examples/gtk-application.js
    report_timeout "run the gtk-application.js example"

    timeout 5s $gjs examples/gettext.js
    report_timeout "run the gettext.js example"

    timeout 5s $gjs installed-tests/js/extra/test-title.js
    report_timeout "run the test-title.js example"

    timeout 5s $gjs installed-tests/js/extra/metacity.js
    report_timeout "run the metacity.js example"

    timeout 5s $gjs installed-tests/js/extra/guitarTuner.js
    report_timeout "run the guitarTuner.js example"

    timeout 5s $gjs installed-tests/js/extra/weatherapp.js
    report_timeout "run the weatherapp.js example"

else
    skip "run the calc.js example" "running without GTK"
    skip "run the gtk.js example" "running without GTK"
    skip "run the gtk-application.js example" "running without GTK"
    skip "run the gettext.js example" "running without GTK"
    skip "run the test-title.js example" "running without GTK"
    skip "run the metacity.js example" "running without GTK"
    skip "run the guitarTuner.js example" "running without GTK"
    skip "run the weatherapp.js example" "running without GTK"
fi
echo "1..$total"
