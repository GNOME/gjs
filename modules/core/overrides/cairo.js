// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2018 Philip Chimento <philip.chimento@gmail.com>

// This override adds the builtin Cairo bindings to imports.gi.cairo.
// (It's confusing to have two incompatible ways to import Cairo.)

const {cairo} = imports.gi;
Object.assign(cairo, imports.cairo);

