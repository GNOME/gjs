// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2017 Philip Chimento <philip.chimento@gmail.com>

// this JS script should print one string (jobs are run before the interpreter
// finishes) and should not print the other (jobs should not be run after the
// interpreter is instructed to quit)
const System = imports.system;
Promise.resolve().then(() => {
    print('Should be printed');
    System.exit(42);
});
Promise.resolve().then(() => print('Should not be printed'));
