// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2025 Gary Li <gary.li1@uwaterloo.ca>

// this script prints out all files in current directory
// useful to test encodings and filename type marshalling
const {Gio} = imports.gi;
const cd = Gio.File.new_for_path('.');
const iter = cd.enumerate_children('standard::name', null, null);
let f;
while ((f = iter.next_file(null)))
    f.get_name();
