// -*- mode: js; js-indent-level: 4; indent-tabs-mode: nil -*-
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2012 Red Hat, Inc.
// SPDX-FileCopyrightText: 2012 Giovanni Campagna <scampa.giovanni@gmail.com>

/* exported format, printf, vprintf */

var {vprintf} = imports._format;

function printf(fmt, ...args) {
    print(vprintf(fmt, args));
}

/*
 * This function is intended to extend the String object and provide a
 * String.format API for string formatting.
 * It has to be set up using String.prototype.format = Format.format;
 * Usage:
 * "somestring %s %d".format('hello', 5);
 * It supports %s, %d, %x and %f.
 * For %f it also supports precisions like "%.2f".format(1.526).
 * All specifiers can be prefixed with a minimum field width, e.g.
 * "%5s".format("foo").
 * Unless the width is prefixed with '0', the formatted string will be padded
 * with spaces.
 */
function format(...args) {
    return vprintf(this, args);
}
