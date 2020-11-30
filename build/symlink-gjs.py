#!/usr/bin/env python3
# SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
# SPDX-FileCopyrightText: 2019 Chun-wei Fan <fanchunwei@src.gnome.org>

import os
import shutil
import sys
import tempfile

assert(len(sys.argv) == 2)

destdir = os.environ.get('DESTDIR')
install_prefix = os.environ.get('MESON_INSTALL_PREFIX')
bindir = sys.argv[1]
if destdir is not None:
    # os.path.join() doesn't concat paths if one of them is absolute
    if install_prefix[0] == '/' and os.name != 'nt':
        installed_bin_dir = os.path.join(destdir, install_prefix[1:], bindir)
    else:
        installed_bin_dir = os.path.join(destdir, install_prefix, bindir)
else:
    installed_bin_dir = os.path.join(install_prefix, bindir)

if os.name == 'nt':
    # Using symlinks on Windows often require administrative privileges,
    # which is not what we want.  Instead, copy gjs-console.exe.
    shutil.copyfile('gjs-console.exe', os.path.join(installed_bin_dir, 'gjs.exe'))
else:
    try:
        temp_link = tempfile.mktemp(dir=installed_bin_dir)
        os.symlink('gjs-console', temp_link)
        os.replace(temp_link, os.path.join(installed_bin_dir, 'gjs'))
    finally:
        if os.path.islink(temp_link):
            os.remove(temp_link)
