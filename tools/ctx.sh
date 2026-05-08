#!/bin/sh
# SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
# SPDX-FileCopyrightText: 2026 Philip Chimento <philip.chimento@gmail.com>

# Clang-Tidy with eXperimental checks

# Infuriatingly, you cannot get either run-clang-tidy nor clang-tidy-diff to
# pass this extra argument on its clang-tidy command line.

$(which clang-tidy) --experimental-custom-checks "$@"
