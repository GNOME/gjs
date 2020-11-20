#!/bin/sh
# SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
# SPDX-FileCopyrightText: 2020 Philip Chimento <philip.chimento@gmail.com>

cd $(dirname -- "$0")
yarn install --frozen-lockfile
yarn lint "$@"
