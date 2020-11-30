/* exported ImporterClass, testImporterFunction */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2009 litl, LLC

function testImporterFunction() {
    return '__init__ function tested';
}

function ImporterClass() {
    this._init();
}

ImporterClass.prototype = {
    _init() {
        this._a = '__init__ class tested';
    },

    testMethod() {
        return this._a;
    },
};
