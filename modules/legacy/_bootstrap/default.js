// @ts-ignore
const printer = imports.print;

Object.defineProperties(window, {
    print: {
        configurable: false,
        value: printer.print
    },
    printerr: {
        configurable: false,
        value: printer.printerr
    },
    log: {
        configurable: false,
        value: printer.log
    },
    logError: {
        configurable: false,
        value: printer.logError
    },
    ARGV: {
        get() {
            // @ts-ignore
            return imports.process.argv;
        },
        configurable: false
    }
});