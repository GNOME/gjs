import { Console } from "console";

const { print } = require("print");

const console = new Console(print);

window.console = console;