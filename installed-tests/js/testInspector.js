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

    it('accepts and responds to an Initialize and ConfigurationDone request', function () {
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
            sendRequest('launch', {
                cwd: 'resource:///org/gjs/jsunit/inspector',
                program: 'sample.js',
            });
            sendRequest('configurationDone');
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
                'resource:///org/gjs/jsunit/inspector/nested dir#/% hello.js',
            );
        });
    });

    afterEach(function () {
        cancel.cancel();
        miniinspector.force_exit();
        miniinspector.wait(null);
    });
});
