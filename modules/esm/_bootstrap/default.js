/// <reference path="./default.d.ts" />

import { Console } from "console";

const { log } = import.meta.require("print");
const process = import.meta.require("process");

const console = new Console(log);

Object.defineProperties(globalThis, {
    "ARGV": {
        get() {
            return process.argv;
        },
        configurable: false
    },
    console: {
        get() {
            return console;
        },
        configurable: false
    }
});

delete globalThis.internal_require;
