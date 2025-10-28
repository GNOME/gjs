#!/bin/bash
# SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
# SPDX-FileCopyrightText: 2025 Marco Trevisan

mapfile -t files < <(find . -not -path "./subprojects/*" -type f -name "*.sh")
shellcheck -x "${files[@]}"
