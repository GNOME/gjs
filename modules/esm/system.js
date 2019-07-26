/** 
 * @function
 * @type {(obj: object) => string}
 * @param {object} obj
 * @returns {string}
 */
export function addressOf(obj) {
    return imports.system.addressOf(obj);
}

/**  
 * @function
 * @type {(gobj: import('gi://GObject').Object) => string}
 * @param {object} gobj
 * @returns {string}
 */
export function addressOfGObject(gobj) {
    return imports.system.addressOfGObject(gobj);
}

/** 
 * @function
 * @type {(obj: object) => number}
 * @param {object} obj
 * @returns {number}
 */
export function refcount(obj) {
    return imports.system.refcount(obj);
}

/** 
 * @function
 * @type {() => void}
 */
export function breakpoint() {
    return imports.system.breakpoint();
}

/** 
 * @function
 * @type {(filename: string) => void}
 */
export function dumpHeap() {
    return imports.system.dumpHeap();
}

/** 
 * @function
 * @type {() => void}
 */
export function gc() {
    return imports.system.gc();
}

/** 
 * @function
 * @type {(code?: number) => void}
 * @param {number} [code] The exit code.
 */
export function exit(code) {
    return imports.system.exit(code);
}

/**
 * @function
 * @type {() => void}
 */
export function clearDateCaches() {
    return imports.system.clearDateCaches();
}

/**
 * 
 */
export default (imports.system);
