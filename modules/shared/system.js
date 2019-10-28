// This is a stub which describes the exports for the native system module.

/** @type {Object.<string, any>} */
var module = {};

// Tell the module wrapper that the C++ native module is 'system'
module.nativeName = 'system';
// Tell the module wrapper to derive the exports' values from the native module.
module.isNative = true;
// Define the exports. 'true' is an arbitrary value.
module.exports = {
    exit: true, gc: true, dumpHeap: true, breakpoint: true, refcount: true, addressOf: true, addressOfGObject: true
};