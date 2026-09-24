// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Philip Chimento <philip.chimento@gmail.com>

import Gio from 'gi://Gio';
import GLib from 'gi://GLib';

describe('Inspector', function () {
    let miniinspectorPath, decoder;  // per-suite data
    let nextRequestID, miniinspector, stdin, stdout, cancel;  // per-test data

    function readMessage() {
        const [contentLength] = stdout.read_line_utf8(cancel);
        expect(contentLength).toMatch(/^Content-Length: \d+$/);
        const nbytes = Number(contentLength.slice(16));
        expect(nbytes).not.toBeNaN();
        expect(nbytes).toBeGreaterThan(0);
        const [blank] = stdout.read_line_utf8(cancel);
        expect(blank).toBe('');
        const bytes = stdout.read_bytes(nbytes, cancel);
        const body = decoder.decode(bytes);
        return JSON.parse(body);
    }

    function sendRequest(command, argsObject = {}) {
        const requestID = nextRequestID++;
        const request = {
            seq: requestID,
            type: 'request',
            command,
            arguments: argsObject,
        };
        const bodyString = `${JSON.stringify(request)}\r\n`;
        stdin.put_string(`Content-Length: ${bodyString.length}\r\n\r\n${bodyString}`, cancel);

        const message = readMessage();
        expect(message.type).toBe('response');
        expect(message.request_seq).toBe(requestID);
        expect(message.success).toBe(true);
        expect(message.command).toBe(command);
        expect(message.message).not.toBeDefined();
        return message.body;
    }

    function expectEvent(event) {
        const message = readMessage();
        expect(message.type).toBe('event');
        expect(message.event).toBe(event);
        return message.body;
    }

    function launch(filename, launchOptions = {}) {
        sendRequest('launch', {
            cwd: 'resource:///org/gjs/jsunit/inspector',
            program: filename,
            ...launchOptions,
        });
        sendRequest('configurationDone');
    }

    beforeAll(function () {
        let file;
        if (GLib.getenv('GJS_USE_UNINSTALLED_FILES') === '1')
            file = Gio.File.new_for_path(GLib.getenv('TOP_BUILDDIR')).resolve_relative_path('./installed-tests/js/miniinspector');
        else
            file = Gio.File.new_for_uri(import.meta.url).resolve_relative_path('../miniinspector');
        miniinspectorPath = file.get_path();

        decoder = new TextDecoder();
    });

    beforeEach(function () {
        nextRequestID = 1;
        cancel = new Gio.Cancellable();

        miniinspector = new Gio.Subprocess({
            argv: [miniinspectorPath],
            flags: Gio.SubprocessFlags.STDIN_PIPE | Gio.SubprocessFlags.STDOUT_PIPE,
        });
        miniinspector.init(cancel);
        stdin = new Gio.DataOutputStream({
            baseStream: miniinspector.get_stdin_pipe(),
            closeBaseStream: false,
        });
        stdout = new Gio.DataInputStream({
            baseStream: miniinspector.get_stdout_pipe(),
            closeBaseStream: false,
            newlineType: Gio.DataStreamNewlineType.CR_LF,
        });
    });

    it('handles EOF gracefully when reading the request header', function () {
        miniinspector.get_stdin_pipe().close(cancel);
        miniinspector.wait(cancel);
        expect(miniinspector.get_successful()).toBeTrue();
    });

    it('handles EOF gracefully when reading the request body', function () {
        stdin.put_string('Content-Length: 100\r\n\r\n{', cancel);
        miniinspector.get_stdin_pipe().close(cancel);
        miniinspector.wait(cancel);
        expect(miniinspector.get_successful()).toBeTrue();
    });

    it('accepts and responds to Initialize, ConfigurationDone, and Disconnect requests', function () {
        const response = sendRequest('initialize', {
            adapterID: 'miniinspector',
            clientID: 'jasmine',
            clientName: 'GJS Unit Tests',
            locale: 'en-CA',
            pathFormat: 'uri',
        });
        expect(response.supportsConfigurationDoneRequest).toBe(true);
        expectEvent('initialized');
        sendRequest('configurationDone');

        sendRequest('disconnect');
        miniinspector.wait(cancel);
        expect(miniinspector.get_successful()).toBeTrue();
    });

    it('reports the exit code of the debuggee', function () {
        sendRequest('initialize', {
            adapterID: 'miniinspector',
            clientID: 'jasmine',
            clientName: 'GJS Unit Tests',
            locale: 'en-CA',
            pathFormat: 'uri',
        });
        expectEvent('initialized');

        launch('exitcode.js');
        const exited = expectEvent('exited');
        expect(exited.exitCode).toBe(42);
        expectEvent('terminated');

        // FIXME: We should be able to put this test in the 'once initialized'
        // block below, but currently we have to quit the server when the
        // debuggee quits; see TODO note in configurationDone() in inspector.js.
        miniinspector.wait(cancel);
        expect(miniinspector.get_successful()).toBeTrue();
    });

    describe('once initialized', function () {
        beforeEach(function () {
            sendRequest('initialize', {
                adapterID: 'miniinspector',
                clientID: 'jasmine',
                clientName: 'GJS Unit Tests',
                locale: 'en-CA',
                pathFormat: 'uri',
            });
            expectEvent('initialized');
        });

        it('can launch a file', function () {
            launch('sample.js');
            const stopped = expectEvent('stopped');
            expect(stopped.reason).toBe('instruction breakpoint');
        });

        it('can resolve a file with special characters in its name', function () {
            sendRequest('launch', {
                cwd: 'resource:///org/gjs/jsunit/inspector/nested dir#',
                program: '% hello.js',
            });
            sendRequest('configurationDone');

            const stopped = expectEvent('stopped');
            expect(stopped.reason).toBe('instruction breakpoint');

            const response = sendRequest('stackTrace');
            expect(response.stackFrames.length).toBeGreaterThan(0);
            expect(response.stackFrames[0].source.name).toBe(
                'resource:///org/gjs/jsunit/inspector/nested dir#/% hello.js'
            );
        });

        it('can launch a file without cwd', function () {
            sendRequest('launch', {
                program: 'resource:///org/gjs/jsunit/inspector/sample.js',
            });
            sendRequest('configurationDone');
            const stopped = expectEvent('stopped');
            expect(stopped.reason).toBe('instruction breakpoint');
        });

        it('can launch a file and break on entry', function () {
            launch('sample.js', {stopOnEntry: true});
            const stopped = expectEvent('stopped');
            expect(stopped.reason).toBe('entry');
        });

        afterEach(function () {
            sendRequest('disconnect');
            miniinspector.wait(cancel);
            expect(miniinspector.get_successful()).toBeTrue();
        });
    });

    afterEach(function () {
        cancel.cancel();
    });
});
