// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2017 Philip Chimento <philip.chimento@gmail.com>

(function (exports) {
    'use strict';

    exports.debugger = new Debugger(exports.debuggee);
    exports.debugger.collectCoverageInfo = true;
})(globalThis);
