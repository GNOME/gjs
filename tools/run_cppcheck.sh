#!/bin/sh
# SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
# SPDX-FileCopyrightText: 2017 Claudio André <claudioandre.br@gmail.com>

ninja -C _build
cppcheck --project=_build/compile_commands.json --inline-suppr \
    --enable=warning,performance,portability,missingInclude \
    -UHAVE_PRINTF_ALTERNATIVE_INT \
    --library=gtk,tools/cppcheck.cfg --force --quiet $@
