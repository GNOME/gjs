// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC

// simple test module (used by testImporter.js)

/* eslint no-redeclare: ["error", { "builtinGlobals": false }] */ // for toString
/* exported bar, foo, testToString, toString */

var foo = 'This is foo';
var bar = 'This is bar';

var toString = x => x;

function testToString(x) {
    return toString(x);
}
