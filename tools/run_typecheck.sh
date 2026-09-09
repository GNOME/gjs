#!/bin/sh
# SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
# SPDX-FileCopyrightText: 2020 Philip Chimento <philip.chimento@gmail.com>

export NODE_OPTIONS=--dns-result-order=ipv4first

cd "$(dirname -- "$0")" || exit 1
npm ci

there_were_errors=0
for project in .. ../modules/script/_bootstrap; do
    npm run typecheck -- --project "$project" "$@" || there_were_errors=1
done
exit $there_were_errors
