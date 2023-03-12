import GLib from 'gi://GLib';
import gi from 'gi';

import { charLengthAt, charLengthLeft } from './_readline/utils.js';
import {Terminal} from './_readline/terminal.js';

export {Terminal, setRawMode} from './_readline/terminal.js';


const NativeConsole = import.meta.importSync('_consoleNative');

/**
 * @param {string} string the string to splice
 * @param {number} index the index to start removing characters at
 * @param {number} removeCount how many characters to remove
 * @param {string} replacement a string to replace the removed characters with
 * @returns {string}
 */
function StringSplice(string, index, removeCount = 0, replacement = '') {
    return (
        string.slice(0, index) + replacement + string.slice(index + removeCount)
    );
}

/**
 * @typedef {object} ReadlineOptions
 * @property {string} prompt the prompt to print prior to the line
 * @property {string} [pendingPrompt] the prompt shown when multiline input is pending
 * @property {boolean} [enableColor] whether to print ANSI color codes
 */

/** @typedef {ReadlineOptions & import('./_readline/terminal.js').TerminalOptions} AsyncReadlineOptions */

/**
 * A basic abstraction around line-by-line input
 */
export class Readline {
    #prompt;
    #pendingPrompt;
    /**
     * The current input
     */
    #input = '';
    /**
     * Whether to cancel the prompt
     */
    #cancelling = false;

    /**
     * Store pending lines of multiline input
     *
     * @example
     * gjs > 'a pending line...
     * ..... '
     *
     * @type {string[]}
     */
    #pendingInputLines = [];

    get isRaw() {
        throw new Error('Unimplemented');
    }

    get isAsync() {
        throw new Error('Unimplemented');
    }

    /**
     * @param {ReadlineOptions} options _
     */
    constructor({ prompt, pendingPrompt = ' '.padStart(4, '.') }) {
        this.#prompt = prompt;
        this.#pendingPrompt = pendingPrompt;
    }

    [Symbol.toStringTag]() {
        return 'Readline';
    }

    get cancelled() {
        return this.#cancelling;
    }

    get line() {
        return this.#input;
    }

    set line(value) {
        this.#input = value;
    }

    hasMultilineInput() {
        return this.#pendingInputLines.length > 0;
    }

    addLineToMultilineInput(line) {
        // Buffer the pending input...
        this.#pendingInputLines.push(line);
    }

    completeMultilineInput() {
        // Reset lines before input is triggered
        this.#pendingInputLines = [];
    }

    processLine() {
        const { line } = this;

        // Rebuild the input...
        const multilineInput = [...this.#pendingInputLines, line].join('\n');

        this.emit('validate', line, multilineInput);

        // Reset state...
        this.#input = '';
        if (this.#pendingInputLines.length > 0) {
            return;
        }

        if (multilineInput.includes('\n')) {
            this.emit('multiline', multilineInput);
        } else {
            this.emit('line', multilineInput);
        }
    }

    get inputPrompt() {
        if (this.#pendingInputLines.length > 0) {
            return this.#pendingPrompt;
        }

        return this.#prompt;
    }

    print(_output) {}

    render() {}

    prompt() {
        this.#cancelling = false;
    }

    exit() {}

    cancel() {
        this.#cancelling = true;
    }

    /**
     * @param {ReadlineOptions} options _
     */
     static create(options) {
        let isAsync = false;

        try {
            // Only enable async input if GJS_READLINE_USE_FALLBACK is not 'true'
            isAsync = GLib.getenv('GJS_READLINE_USE_FALLBACK') !== 'true';
            // Only enable async input if the terminal supports Unix streams
            isAsync &&= Terminal.hasUnixStreams();
        } catch {
            // Otherwise, disable async
            isAsync = false;
        }

        if (isAsync) {
            return new AsyncReadline(options);
        }

        return new SyncReadline(options);
    }
}
imports.signals.addSignalMethods(Readline.prototype);

/**
 * Synchronously reads lines and emits events to handle them
 */
export class SyncReadline extends Readline {
    /**
     * @param {ReadlineOptions} options _
     */
    constructor({ prompt, pendingPrompt }) {
        super({ prompt, pendingPrompt, enableColor: false });
    }

    prompt() {
        while (!this.cancelled) {
            const { inputPrompt } = this;

            try {
                this.line = NativeConsole.interact(inputPrompt).split('');
            } catch {
                this.line = '';
            }

            this.processLine();
        }
    }

    print(output) {
        print(output);
    }

    get isAsync() {
        return false;
    }

    get isRaw() {
        return false;
    }
}

/**
 * Asynchronously reads lines and prints output, allowing a mainloop
 * to run in parallel.
 */
export class AsyncReadline extends Readline {
    #exitWarning = false;

    /**
     * Store previously inputted lines
     *
     * @type {string[]}
     */
    #history = [];
    #historyIndex = -1;

    /**
     * The cursor's current column position.
     */
    #cursorColumn = 0;

    /**
     * @type {Terminal}
     */
    #terminal;

    /**
     * @param {AsyncReadlineOptions} options _
     */
    constructor({
        inputStream,
        outputStream,
        errorOutputStream,
        prompt,
        pendingPrompt
    }) {
        super({ prompt, pendingPrompt });

        this.#terminal = new Terminal({
            inputStream,
            outputStream,
            errorOutputStream,
            onKeyPress: (_, key) => {
                this.#processKey(key);

                if (!this.cancelled) this.render();
            },
        });
    }

    get isAsync() {
        return true;
    }

    get isRaw() {
        return true;
    }

    /**
     * Gets the current line of input or a line from history if the user has scrolled up
     */
    get line() {
        if (this.#historyIndex > -1) return this.#history[this.#historyIndex];

        return super.line;
    }

    /**
     * Modifies the current line of input or a line from history if the user has scrolled up
     */
    set line(value) {
        if (this.#historyIndex > -1) {
            this.#history[this.#historyIndex] = value;
            return;
        }

        super.line = value;
    }

    exit() {
        if (this.#exitWarning) {
            this.#exitWarning = false;
            this.emit('exit');
        } else {
            this.#exitWarning = true;
            this.print('\n(To exit, press Ctrl+C again or Ctrl+D)\n');
        }
    }

    historyUp() {
        if (this.#historyIndex < this.#history.length - 1) {
            this.#historyIndex++;
            this.moveCursorToEnd();
        }
    }

    historyDown() {
        if (this.#historyIndex >= 0) {
            this.#historyIndex--;
            this.moveCursorToEnd();
        }
    }

    moveCursorToBeginning() {
        this.cursor = 0;
    }

    moveCursorToEnd() {
        this.cursor = this.line.length;
    }

    moveCursorLeft() {
        this.cursor -= charLengthLeft(this.line, this.cursor);
    }

    moveCursorRight() {
        this.cursor += charLengthAt(this.line, this.cursor);
    }

    addChar(char) {
        this.line = StringSplice(this.line, this.cursor, 0, char);
        this.moveCursorRight();
    }

    deleteChar() {
        const { line, cursor } = this;

        if (line.length > 0 && cursor > 0) {
            const charLength = charLengthLeft(this.line, this.cursor);
            const modified = StringSplice(
                line,
                cursor - charLength,
                charLength
            );

            this.line = modified;
            this.moveCursorLeft();
        }
    }

    deleteCharRightOrClose() {
        const { line, cursor } = this;

        if (cursor < line.length - 1) {
            const charLength = charLengthAt(this.line, this.cursor);
            this.line = StringSplice(line, cursor, charLength);
            return;
        }

        this.exit();
    }

    deleteToBeginning() {
        this.line = StringSplice(this.line, 0, this.cursor);
    }

    deleteToEnd() {
        this.line = StringSplice(this.line, this.cursor);
    }

    /**
     * Adapted from lib/internal/readline/interface.js in Node.js
     */
    deleteWordLeft() {
        const { line, cursor } = this;

        if (cursor > 0) {
            // Reverse the string and match a word near beginning
            // to avoid quadratic time complexity
            let leading = line.slice(0, cursor);
            const reversed = [...leading].reverse().join('');
            const match = reversed.match(/^\s*(?:[^\w\s]+|\w+)?/);
            leading = leading.slice(0, leading.length - match[0].length);
            this.line = leading.concat(line.slice(cursor));
            this.cursor = leading.length;
        }
    }

    /**
     * Adapted from lib/internal/readline/interface.js in Node.js
     */
    deleteWordRight() {
        const { line, cursor } = this;

        if (line.length > 0 && cursor < line.length) {
            const trailing = line.slice(cursor);
            const match = trailing.match(/^(?:\s+|\W+|\w+)\s*/);
            this.line = line
                .slice(0, cursor)
                .concat(trailing.slice(match[0].length));
        }
    }

    /**
     * Adapted from lib/internal/readline/interface.js in Node.js
     */
    wordLeft() {
        const { line, cursor } = this;

        if (cursor > 0) {
            // Reverse the string and match a word near beginning
            // to avoid quadratic time complexity
            const leading = line.slice(0, cursor);
            const reversed = [...leading].reverse().join('');
            const match = reversed.match(/^\s*(?:[^\w\s]+|\w+)?/);

            this.cursor -= match[0].length;
        }
    }

    /**
     * Adapted from lib/internal/readline/interface.js in Node.js
     */
    wordRight() {
        const { line } = this;

        if (this.cursor < line.length) {
            const trailing = line.slice(this.cursor);
            const match = trailing.match(/^(?:\s+|[^\w\s]+|\w+)\s*/);

            this.cursor += match[0].length;
        }
    }

    /**
     * @param {number} column the column to move the cursor to
     */
    set cursor(column) {
        if (column < 0) {
            this.#cursorColumn = 0;
            return;
        }

        // Ensure the input index isn't longer than the content...
        this.#cursorColumn = Math.min(this.line.length, column);
    }

    /**
     * The current column the cursor is at
     */
    get cursor() {
        return this.#cursorColumn;
    }

    processLine() {
        const { line } = this;

        // Add the line to history
        this.#history.unshift(line);

        // Reset the CTRL-C exit warning
        this.#exitWarning = false;

        // Move the cursor to the beginning of the new line
        this.moveCursorToBeginning();

        // Print a newline
        this.#terminal.newLine();
        // Commit changes
        this.#terminal.commit();

        // Call Readline.processLine to handle input validation
        // and act on the input
        super.processLine();

        // Reset the history scroll so the user sees the current
        // input
        this.#historyIndex = -1;
    }

    #processKey(key) {
        if (!key.sequence) return;

        if (key.ctrl && !key.meta && !key.shift) {
            switch (key.name) {
                case 'c':
                    this.exit();
                    return;
                case 'h':
                    this.deleteChar();
                    return;
                case 'd':
                    this.deleteCharRightOrClose();
                    return;
                case 'u':
                    this.deleteToBeginning();
                    return;
                case 'k':
                    this.deleteToEnd();
                    return;
                case 'a':
                    this.moveCursorToBeginning();
                    return;
                case 'e':
                    this.moveCursorToEnd();
                    return;
                case 'b':
                    this.moveCursorLeft();
                    return;
                case 'f':
                    this.moveCursorRight();
                    return;
                case 'l':
                    NativeConsole.clearTerminal();
                    return;
                case 'n':
                    this.historyDown();
                    return;
                case 'p':
                    this.historyUp();
                    return;
                case 'z':
                    // Pausing is unsupported.
                    return;
                case 'w':
                case 'backspace':
                    this.deleteWordLeft();
                    return;
                case 'delete':
                    this.deleteWordRight();
                    return;
                case 'left':
                    this.wordLeft();
                    return;
                case 'right':
                    this.wordRight();
                    return;
            }
        } else if (key.meta && !key.shift) {
            switch (key.name) {
                case 'd':
                    this.deleteWordRight();
                    return;
                case 'backspace':
                    this.deleteWordLeft();
                    return;
                case 'b':
                    this.wordLeft();
                    return;
                case 'f':
                    this.wordRight();
                    return;
            }
        }

        switch (key.name) {
            case 'up':
                this.historyUp();
                return;
            case 'down':
                this.historyDown();
                return;
            case 'left':
                this.moveCursorLeft();
                return;
            case 'right':
                this.moveCursorRight();
                return;
            case 'backspace':
                this.deleteChar();
                return;
            case 'return':
                this.processLine();
                return;
        }

        this.addChar(key.sequence);
    }

    render() {
        // Prevent the cursor from flashing while we render...
        this.#terminal.hideCursor();
        this.#terminal.cursorTo(0);
        this.#terminal.clearScreenDown();

        const { inputPrompt, line } = this;
        this.#terminal.print(inputPrompt, line);
        this.#terminal.cursorTo(inputPrompt.length + this.cursor);
        this.#terminal.showCursor();

        this.#terminal.commit();

        this.emit('render');
    }

    /**
     * @param {string[]} strings strings to write to stdout
     */
    print(...strings) {
        this.#terminal.print(...strings);
        this.#terminal.newLine();
        this.#terminal.commit();
    }

    cancel() {
        super.cancel();

        this.#terminal.stopInput();
        this.#terminal.newLine();
        this.#terminal.commit();
    }

    prompt() {
        super.prompt();

        this.render();

        // Start the async read loop...
        this.#terminal.startInput();
    }
}

