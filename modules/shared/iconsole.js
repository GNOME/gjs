// This is a stub which describes the exports for the native console module.

/** @type {Object.<string, any>} */
var module = {};

// Tell the wrapper that the native module is 'console', otherwise it would attempt to find an
// 'iconsole' native module.
module.nativeName = 'console';
// Tell the wrapper to back this stub with the native module.
module.isNative = true;
// Define no named exports. (the wrapper will automatically include a default export though)
module.exports = {};