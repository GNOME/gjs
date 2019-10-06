const system = require("system");

/**
 * @param {string} name
 * @returns {(...args: unknown[]) => unknown} 
 */
function wrap(name) {
    return (...args) => system[name](...args);
}

export const exit = wrap("exit");
export const gc = wrap("gc");
export const dumpHeap = wrap("dumpHeap");
export const breakpoint = wrap("breakpoint");
export const refcount = wrap("refcount");
export const addressOfGObject = wrap("addressOfGObject");
export const addressOf = wrap("addressOf");

export default system;
