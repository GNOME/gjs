#!/usr/bin/env python3

import os
import subprocess

prefix = os.environ.get('MESON_INSTALL_PREFIX')
schemadir = os.path.join(prefix, 'share', 'glib-2.0', 'schemas')

if os.environ.get('DESTDIR') is None:
    print('Compiling GSettings schemas...')
    subprocess.call(['glib-compile-schemas', schemadir])
