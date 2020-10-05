#!/bin/sh
# SPDX-License-Identifier: MIT OR LGPL-2.0-or-later

ninja -C _build
cppcheck --project=_build/compile_commands.json --inline-suppr \
    --enable=warning,performance,portability,missingInclude \
    --force --quiet $@
