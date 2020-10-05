// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later

(function (exports) {
    'use strict';

    exports.debugger = new Debugger(exports.debuggee);
    exports.debugger.collectCoverageInfo = true;
})(globalThis);
