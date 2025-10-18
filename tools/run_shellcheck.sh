#!/bin/sh
# SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
# SPDX-FileCopyrightText: 2025 Marco Trevisan

shellcheck -x $(find installed-tests -type f -name "*.sh" -print0 | sort | xargs -0)
