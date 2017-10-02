(function(exports) {
    'use strict';

    exports.debugger = new Debugger(exports.debuggee);
    exports.debugger.collectCoverageInfo = true;
})(window);
