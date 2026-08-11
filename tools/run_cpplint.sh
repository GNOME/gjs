#!/bin/sh
# SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
# SPDX-FileCopyrightText: 2025 Philip Chimento <philip.chimento@gmail.com>

cd "$(dirname -- "$0")/.." || exit 1

[ $# -eq 0 ] && set -- .

cpplint --quiet --recursive "$@"
