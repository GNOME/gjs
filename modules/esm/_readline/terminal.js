
import GLib from 'gi://GLib';
import gi from 'gi';

import { emitKeys, CSI } from './utils.js';
import { cursorTo } from './callbacks.js';

const NativeConsole = import.meta.importSync('_consoleNative');

/** @type {import('gi://Gio')} */
let Gio;

function requireGio() {
    if (!Gio) Gio = gi.require('Gio');
}

const cursorHide = CSI`?25l`;
const cursorShow = CSI`?25h`;

/**
 * @typedef {object} TerminalOptions
* @property {Gio.InputStream | null} [inputStream] the input stream
* @property {Gio.OutputStream | null} [outputStream] the output stream
* @property {Gio.OutputStream | null} [errorOutputStream] the error output stream
* @property {boolean} [enableColor] whether to print ANSI color codes
 */

export class Terminal {
    /**
     * Pending writes to the stream
     */
    #buffer = [];
    /**
     * @type {Gio.Cancellable | null}
     */
    #cancellable = null;

    #parser;

    /**
     * @param {TerminalOptions} options _
     */
    constructor({
        onKeyPress,
        inputStream = Terminal.stdin,
        outputStream = Terminal.stdout,
        errorOutputStream = Terminal.stderr,
        enableColor,
    }) {
        this.inputStream = inputStream;
        this.outputStream = outputStream;
        this.errorOutputStream = errorOutputStream;
        this.enableColor = enableColor ?? this.supportsColor();
        this.cancelled = false;

        this.#parser = emitKeys(onKeyPress);
        this.#parser.next();
    }

    get dimensions() {
        const values = NativeConsole.getDimensions();
        return { height: values.height, width: values.width };
    }

    commit() {
        const bytes = new TextEncoder().encode(this.#buffer.join(''));
        this.#buffer = [];

        if (!this.outputStream) return;

        this.outputStream.write_bytes(bytes, null);
        this.outputStream.flush(null);
    }

    print(...strings) {
        this.#buffer.push(...strings);
    }

    clearScreenDown() {
        this.#buffer.push(CSI.kClearScreenDown);
    }

    newLine() {
        this.#buffer.push('\n');
    }
    hideCursor() {
        this.#buffer.push(cursorHide);
    }

    showCursor() {
        this.#buffer.push(cursorShow);
    }

    cursorTo(x, y) {
        this.#buffer.push(cursorTo(x, y));
    }
    /**
     * @param {Uint8Array} bytes an array of inputted bytes to process
     * @returns {void}
     */
    handleInput(bytes) {
        if (bytes.length === 0) return;

        const input = String.fromCharCode(...bytes.values());

        for (const byte of input) {
            this.#parser.next(byte);

            if (this.cancelled) break;
        }
    }

    #asyncReadHandler(stream, result) {
        requireGio();

        if (result) {
            try {
                const gbytes = stream.read_bytes_finish(result);

                this.handleInput(gbytes.toArray());
            } catch (error) {
                if (
                    !error.matches(Gio.IOErrorEnum, Gio.IOErrorEnum.CANCELLED)
                ) {
                    console.error(error);
                    imports.system.exit(1);

                    return;
                }
            }
        }

        if (this.cancelled) return;

        this.#cancellable = new Gio.Cancellable();
        stream.read_bytes_async(
            8,
            0,
            this.#cancellable,
            this.#asyncReadHandler.bind(this)
        );
    }

    stopInput() {
        this.cancelled = true;

        this.#cancellable?.cancel();
        this.#cancellable = null;

        this.#buffer = [];
    }

    startInput() {
        if (!this.inputStream) throw new Error('Terminal has no input stream')

        this.#asyncReadHandler(this.inputStream);
    }

    supportsColor() {
        return (
            this.outputStream && 
            GLib.log_writer_supports_color(this.outputStream.fd) &&
            GLib.getenv('NO_COLOR') === null
        );
    }

    static hasUnixStreams() {
        requireGio();

        return 'UnixInputStream' in Gio && 'UnixOutputStream' in Gio;
    }

    static #stdin = null;
    static #stdout = null;
    static #stderr = null;

    static get stdout() {
        if (!Terminal.hasUnixStreams()) {
            throw new Error('Missing Gio.UnixOutputStream');
        }

        requireGio();

        return (this.#stdout ??= new Gio.BufferedOutputStream({
            baseStream: Gio.UnixOutputStream.new(1, false),
            closeBaseStream: false,
            autoGrow: true,
        }));
    }

    static get stdin() {
        if (!Terminal.hasUnixStreams()) {
            throw new Error('Missing Gio.UnixInputStream');
        }

        requireGio();

        return (this.#stdin ??= Gio.UnixInputStream.new(0, false));
    }

    static get stderr() {
        if (!Terminal.hasUnixStreams()) {
            throw new Error('Missing Gio.UnixOutputStream');
        }

        requireGio();

        return (this.#stderr ??= Gio.UnixOutputStream.new(2, false));
    }
}

export function setRawMode(enabled) {
    if (enabled) {
        const success = NativeConsole.enableRawMode();

        if (!success) throw new Error('Could not set raw mode on stdin');
    } else {
        NativeConsole.disableRawMode();
    }
}
