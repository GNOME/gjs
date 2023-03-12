// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2022 Evan Welsh <contact@evanwelsh.com>

import Gio from 'gi://Gio';

import { AsyncReadline } from 'readline';

function createReadline() {
    return new AsyncReadline({
        inputStream: null,
        outputStream: null,
        errorOutputStream: null,
        prompt: '> ',
        enableColor: false,
    });
}

function createReadlineWithStreams() {
    const inputStream = new Gio.MemoryInputStream();
    const outputStream = Gio.MemoryOutputStream.new_resizable();
    const errorOutputStream = Gio.MemoryOutputStream.new_resizable();

    const readline = new AsyncReadline({
        inputStream,
        outputStream,
        errorOutputStream,
        prompt: '> ',
        enableColor: false,
    });

    return {
        readline,
        inputStream,
        teardown() {
            readline.cancel();

            try {
                readline.stdout.close(null);
            } catch {}

            try {
                readline.stdin.close(null);
            } catch {}

            try {
                readline.stderr.close(null);
            } catch {}
        },
    };
}

function expectReadlineOutput({
    readline,
    inputStream,
    input,
    output,
    keystrokes = 1,
}) {
    return new Promise((resolve) => {
        let renderCount = 0;

        readline.connect('render', () => {
            if (++renderCount === keystrokes) {
                readline.disconnectAll();

                expect(readline.line).toBe(output);
                resolve();
            }
        });

        inputStream.add_bytes(new TextEncoder().encode(input));
    });
}



describe('Readline', () => {
    describe('AsyncReadline', () => {
        it('handles key events on stdin', async function () {
            const { readline, inputStream, teardown } = createReadlineWithStreams();
    
            readline.prompt();
    
            await expectReadlineOutput({
                readline,
                inputStream,
                input: 'a',
                output: 'a',
            });
    
            await expectReadlineOutput({
                readline,
                inputStream,
                input: 'b',
                output: 'ab',
            });
    
            await expectReadlineOutput({
                readline,
                inputStream,
                input: '\x1b[D\x1b[Dcr',
                output: 'crab',
                keystrokes: 4,
            });
    
            teardown();
        });
    });
    
    it('can move word left', function () {
        const readline = createReadline();

        readline.line = 'lorem ipsum';
        readline.cursor = readline.line.length;

        readline.wordLeft();

        expect(readline.line).toBe('lorem ipsum');
        expect(readline.cursor).toBe('lorem '.length);
    });

    it('can move word right', function () {
        const readline = createReadline();

        readline.line = 'lorem ipsum';
        readline.cursor = 0;

        readline.wordRight();

        expect(readline.line).toBe('lorem ipsum');
        expect(readline.cursor).toBe('lorem '.length);
    });

    it('can delete word left', function () {
        const readline = createReadline();

        readline.line = 'lorem ipsum';
        readline.cursor = readline.line.length;

        readline.deleteWordLeft();

        const output = 'lorem ';

        expect(readline.line).toBe(output);
        expect(readline.cursor).toBe(output.length);
    });

    it('can delete word right', function () {
        const readline = createReadline();

        readline.line = 'lorem ipsum';
        readline.cursor = 0;

        readline.deleteWordRight();

        const output = 'ipsum';

        expect(readline.line).toBe(output);
        expect(readline.cursor).toBe(0);
    });
});
