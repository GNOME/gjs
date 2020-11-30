#!/usr/bin/env python3
# SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
# SPDX-FileCopyrightText: 2019 Philip Chimento <philip.chimento@gmail.com>

import os
import subprocess
import sys

if len(sys.argv) < 2:
    sys.exit("usage: compile-gschemas.py <schemadir>")

schemadir = sys.argv[1]

if os.environ.get('DESTDIR') is None:
    print('Compiling GSettings schemas...')
    subprocess.call(['glib-compile-schemas', schemadir])
