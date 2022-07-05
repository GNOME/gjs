#!/usr/bin/env gjs
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2016 Philip Chimento <philip.chimento@gmail.com>

const GLib = imports.gi.GLib;

function _filterStack(stack) {
    if (!stack)
        return 'No stack';

    return stack.split('\n')
        .filter(stackLine => stackLine.indexOf('resource:///org/gjs/jsunit') === -1)
        .filter(stackLine => stackLine.indexOf('<jasmine-start>') === -1)
        .join('\n');
}

let jasmineRequire = imports.jasmine.getJasmineRequireObj();
let jasmineCore = jasmineRequire.core(jasmineRequire);
globalThis._jasmineEnv = jasmineCore.getEnv();
globalThis._jasmineEnv.configure({
    random: false,
});
globalThis._jasmineMain = GLib.MainLoop.new(null, false);
globalThis._jasmineRetval = 0;
globalThis._jasmineErrorsOutput = [];

// Install Jasmine API on the global object
let jasmineInterface = jasmineRequire.interface(jasmineCore, globalThis._jasmineEnv);
Object.assign(globalThis, jasmineInterface);

// Reporter that outputs according to the Test Anything Protocol
// See http://testanything.org/tap-specification.html
class TapReporter {
    constructor() {
        this._failedSuites = [];
        this._specCount = 0;
    }

    jasmineStarted(info) {
        print(`1..${info.totalSpecsDefined}`);
    }

    jasmineDone() {
        this._failedSuites.forEach(failure => {
            failure.failedExpectations.forEach(result => {
                print('not ok - An error was thrown outside a test');
                print(`# ${result.message}`);
            });
        });

        globalThis._jasmineMain.quit();
    }

    suiteDone(result) {
        if (result.failedExpectations && result.failedExpectations.length > 0) {
            globalThis._jasmineRetval = 1;
            this._failedSuites.push(result);
        }

        if (result.status === 'disabled')
            print('# Suite was disabled:', result.fullName);
    }

    specStarted() {
        this._specCount++;
    }

    specDone(result) {
        let tapReport;
        if (result.status === 'failed') {
            globalThis._jasmineRetval = 1;
            tapReport = 'not ok';
        } else {
            tapReport = 'ok';
        }
        tapReport += ` ${this._specCount} ${result.fullName}`;
        if (result.status === 'pending' || result.status === 'disabled' ||
            result.status === 'excluded') {
            let reason = result.pendingReason || result.status;
            tapReport += ` # SKIP ${reason}`;
        }
        print(tapReport);

        // Print additional diagnostic info on failure
        if (result.status === 'failed' && result.failedExpectations) {
            result.failedExpectations.forEach(failedExpectation => {
                const output = [];
                const messageLines = failedExpectation.message.split('\n');
                output.push(`Message: ${messageLines.shift()}`);
                output.push(...messageLines.map(str => `  ${str}`));
                output.push('Stack:');
                let stackTrace = _filterStack(failedExpectation.stack).trim();
                output.push(...stackTrace.split('\n').map(str => `  ${str}`));

                if (globalThis._jasmineErrorsOutput.length) {
                    globalThis._jasmineErrorsOutput.push(
                        Array(GLib.getenv('COLUMNS') || 80).fill('―').join(''));
                }

                globalThis._jasmineErrorsOutput.push(`Test: ${result.fullName}`);
                globalThis._jasmineErrorsOutput.push(...output);
                print(output.map(l => `# ${l}`).join('\n'));
            });
        }
    }
}

globalThis._jasmineEnv.addReporter(new TapReporter());

// If we're running the tests in certain JS_GC_ZEAL modes or Valgrind, then some
// will time out if the CI machine is under a certain load. In that case
// increase the default timeout.
const gcZeal = GLib.getenv('JS_GC_ZEAL');
const valgrind = GLib.getenv('VALGRIND');
if (valgrind || (gcZeal && (gcZeal === '2' || gcZeal.startsWith('2,') || gcZeal === '4')))
    jasmine.DEFAULT_TIMEOUT_INTERVAL *= 5;
