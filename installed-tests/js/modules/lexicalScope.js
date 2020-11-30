/* exported a, b, c */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2017 Philip Chimento <philip.chimento@gmail.com>

// Tests bindings in the global scope (var) and lexical environment (let, const)

// This should be exported as a property when importing this module:
var a = 1;

// These should not be exported, but for compatibility we will pretend they are
// for the time being:
let b = 2;
const c = 3;

// It's not clear whether this should be exported in ES6, but for compatibility
// it should be:
this.d = 4;

// Modules should have access to standard properties on the global object.
if (typeof imports === 'undefined')
    throw new Error('fail the import');

// This should probably not be supported in the future, but I'm not sure how
// we can phase it out compatibly. The module should also have access to
// properties that the importing code defines.
if (typeof expectMe === 'undefined')
    throw new Error('fail the import');
