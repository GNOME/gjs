// Logging

/**
 * 
 * @param {unknown[]} args 
 * @returns string
 */
function stringify(args) {
    return args.map(d => d.toString()).join(' ');
}

export class Console {
    /**
     * @private
     * @param {(str: string) => void} fn
     */
    constructor(fn) {
        this.printFn = fn;

        // From https://console.spec.whatwg.org/#console-namespace
        // TODO: Figure out if we need to do some prototype voodoo per the old spec.
    }

    /**
     * 
     * @param {*} condition 
     * @param  {...any} data 
     */
    assert(condition = false, ...data) {
        if (!condition) this.error(data);
    }

    /**
     * 
     */
    clear() {
        throw new Error("Not yet implemented.");
    }

    /**
     * 
     * @param  {...any} data 
     */
    debug(...data) {
        this.printFn(`DEBUG: ${stringify(data)}`);
    }

    /**
     * 
     * @param  {...any} data 
     */
    error(...data) {
        this.printFn(`ERROR: ${stringify(data)}`);
    }

    /**
     * 
     * @param  {...any} data 
     */
    info(...data) {
        this.printFn(`INFO: ${stringify(data)}`);
    }

    /**
     * 
     * @param  {...unknown} data 
     */
    log(...data) {
        this.printFn(`LOG: ${stringify(data)}`);
    }

    /**
     * 
     * @param {*} tabularData 
     * @param {*} properties 
     */
    table(tabularData, properties) {
        throw new Error("Not yet implemented.");
    }

    /**
     * 
     * @param  {...any} data 
     */
    trace(...data) {
        throw new Error("Not yet implemented.");
    }

    /**
     * 
     * @param  {...any} data 
     */
    warn(...data) {
        throw new Error("Not yet implemented.");
    }

    /**
     * 
     * @param {*} item 
     * @param {*} options 
     */
    dir(item, options) {
        throw new Error("Not yet implemented.");
    }

    /**
     * 
     * @param  {...any} data 
     */
    dirxml(...data) {
        throw new Error("Not yet implemented.");
    }

    // Counting
    count(label = "default") {
        throw new Error("Not yet implemented.");
    }

    countReset(label = "default") {
        throw new Error("Not yet implemented.");
    }

    // Grouping

    /**
     * 
     * @param  {...any} data 
     */
    group(...data) { throw new Error("Not yet implemented."); }

    /**
     * 
     * @param  {...any} data 
     */
    groupCollapsed(...data) { throw new Error("Not yet implemented."); }

    groupEnd() { throw new Error("Not yet implemented."); }

    // Timing
    time(label = "default") { throw new Error("Not yet implemented."); }

    /**
     * 
     * @param {*} label 
     * @param  {...any} data 
     */
    timeLog(label = "default", ...data) { throw new Error("Not yet implemented."); }

    timeEnd(label = "default") { throw new Error("Not yet implemented."); }
}