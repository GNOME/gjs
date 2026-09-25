// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2016 Philip Chimento <philip@endlessm.com>

// this JS script fails if either 1) --help is not passed to it, or 2) the
// string "sentinel" is not in its search path
const System = imports.system;
if (imports.searchPath.indexOf('sentinel') === -1)
    System.exit(1);
if (ARGV.indexOf('--help') === -1)
    System.exit(1);
System.exit(0);
