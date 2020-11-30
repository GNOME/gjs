#!/bin/bash
# SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
# SPDX-FileCopyrightText: 2018 Claudio André <claudioandre.br@gmail.com>

DIR="$( cd "$( dirname "${0}" )" && pwd )"
source "${DIR}"/common.sh

# Run the examples
$gjs examples/gio-cat.js meson.build
report "run the gio-cat.js example"

if [[ -n "${ENABLE_GTK}" ]]; then
    export graphical_gjs="xvfb-run -a dbus-run-session -- $gjs"

    eval timeout 5s $graphical_gjs examples/calc.js
    report_timeout "run the calc.js example"

    eval timeout 5s $graphical_gjs examples/gtk.js
    report_timeout "run the gtk.js example"

    eval timeout 5s $graphical_gjs examples/gtk-application.js
    report_timeout "run the gtk-application.js example"

    eval timeout 5s $graphical_gjs examples/gettext.js
    report_timeout "run the gettext.js example"
else
    skip "run the calc.js example" "running without GTK"
    skip "run the gtk.js example" "running without GTK"
    skip "run the gtk-application.js example" "running without GTK"
    skip "run the gettext.js example" "running without GTK"
fi
echo "1..$total"
