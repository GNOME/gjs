// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2021 Evan Welsh <contact@evanwelsh.com>

import gi from 'gi';

import { setRawMode, Readline } from './readline.js';
import { prettyPrint } from './_prettyPrint.js';

const NativeConsole = import.meta.importSync('_consoleNative');
const system = import.meta.importSync('system');

function runMainloop() {
    imports.mainloop.run('repl');
}

function quitMainloop() {
    imports.mainloop.quit('repl');
}

export class Repl {
    #lineNumber = 0;

    /** @type {string} */
    #version;

    constructor() {
        this.#version = system.versionString;
    }

    [Symbol.toStringTag]() {
        return 'Repl';
    }

    get lineNumber() {
        return this.#lineNumber;
    }

    #print(string) {
        this.readline.print(`${string}`);
    }

    #evaluateInternal(lines) {
        try {
            const result = NativeConsole.eval(lines, this.#lineNumber);

            this.#print(`${prettyPrint(result)}`);

            return null;
        } catch (error) {
            return error;
        }
    }

    #printError(error) {
        if (error.message)
            this.#print(`Uncaught ${error.name}: ${error.message}`);
        else this.#print(`${prettyPrint(error)}`);
    }

    #isValidInput(input) {
        return NativeConsole.isValid(input);
    }

    evaluateInput(lines) {
        this.#lineNumber++;

        // Rough object/code block detection similar to Node
        let trimmedLines = lines.trim();
        if (trimmedLines.startsWith('{') && !trimmedLines.endsWith(';')) {
            let wrappedLines = `(${trimmedLines})\n`;

            // Attempt to evaluate any object literals in () first
            let error = this.#evaluateInternal(wrappedLines);
            if (error) this.#printError(error);

            return;
        }

        let error = this.#evaluateInternal(lines);
        if (!error) return;

        this.#printError(error);
    }

    #startInput() {
        this.readline.connect('validate', (_, line, input) => {
            const isValid = this.#isValidInput(input);

            if (isValid && this.readline.hasMultilineInput()) {
                this.readline.completeMultilineInput();
                return;
            }

            if (!isValid) {
                this.readline.addLineToMultilineInput(line);
            }
        });

        this.readline.connect('line', (_, line) => {
            this.evaluateInput(line);
        });

        this.readline.connect('multiline', (_, multiline) => {
            this.evaluateInput(multiline);
        });

        this.readline.connect('exit', () => {
            this.exit();
        });

        this.readline.print(`GJS v${this.#version}`);
        this.readline.prompt();
    }

    start() {
        const prompt = '> ';
        const pendingPrompt = '... ';

        this.readline = Readline.create({ prompt, pendingPrompt });

        const {isRaw, isAsync} = this.readline;
          

        try {
            if (isRaw) setRawMode(true);

            this.#startInput();

            if (isAsync) runMainloop();
        } finally {
            if (isRaw) setRawMode(false);
        }
    }

    exit() {
        try {
            this.readline.cancel();

            if (this.readline.isAsync) {
                quitMainloop();
            }
        } catch {
            // Force an exit if an error occurs
            imports.system.exit(1);
        }
    }
}

// Install the Repl class in imports.console for backwards compatibility
imports.console.Repl = Repl;
