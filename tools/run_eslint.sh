#!/bin/sh
# SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
# SPDX-FileCopyrightText: 2020 Philip Chimento <philip.chimento@gmail.com>

export NODE_OPTIONS=--dns-result-order=ipv4first

cd $(dirname -- "$0")
npm ci
npm run lint -- "$@"
