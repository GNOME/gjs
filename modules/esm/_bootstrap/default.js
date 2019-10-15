import { Console } from "console";

const { log } = require("print");
const process = require("process");

const console = new Console(log);

window.console = console;

Object.defineProperty(window, "ARGV", {
    get() {
        return process.argv;
    }
});