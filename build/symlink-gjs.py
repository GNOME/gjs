#!/usr/bin/env python3

import os
import shutil
import subprocess
import sys

assert(len(sys.argv) == 2)

destdir = os.environ.get('DESTDIR')
install_prefix = os.environ.get('MESON_INSTALL_PREFIX')
bindir = sys.argv[1]
if destdir is not None:
    installed_bin_dir = os.path.join(destdir, install_prefix, bindir)
else:
    installed_bin_dir = os.path.join(install_prefix, bindir)

if os.name == 'nt':
    # Using symlinks on Windows often require administrative privileges,
    # which is not what we want.  Instead, copy gjs-console.exe.
    shutil.copyfile('gjs-console.exe', os.path.join(installed_bin_dir, 'gjs.exe'))
else:
    os.symlink('gjs-console', os.path.join(installed_bin_dir, 'gjs'))
