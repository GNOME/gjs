// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2021 Evan Welsh <contact@evanwelsh.com>

// this JS script is used to test ARGV handling
const System = imports.system;

if (System.programPath.endsWith('/argv.js'))
    System.exit(0);
else
    System.exit(1);
