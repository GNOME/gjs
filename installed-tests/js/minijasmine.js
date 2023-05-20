#!/usr/bin/env -S gjs -m
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2016 Philip Chimento <philip.chimento@gmail.com>

import GLib from 'gi://GLib';

function _filterStack(stack) {
    if (!stack)
        return 'No stack';

    return stack.split('\n')
        .filter(stackLine => stackLine.indexOf('resource:///org/gjs/jsunit') === -1)
        .join('\n');
}

let jasmineRequire = imports.jasmine.getJasmineRequireObj();
let jasmineCore = jasmineRequire.core(jasmineRequire);

export let environment = jasmineCore.getEnv();
environment.configure({
    random: false,
});
export const mainloop = GLib.MainLoop.new(null, false);
export let retval = 0;
export let errorsOutput = [];

// Install Jasmine API on the global object
let jasmineInterface = jasmineRequire.interface(jasmineCore, environment);
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

        mainloop.quit();
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

                if (errorsOutput.length) {
                    errorsOutput.push(
                        Array(GLib.getenv('COLUMNS') || 80).fill('―').join(''));
                }

                errorsOutput.push(`Test: ${result.fullName}`);
                errorsOutput.push(...output);
                print(output.map(l => `# ${l}`).join('\n'));
            });
        }
    }
}

environment.addReporter(new TapReporter());

// If we're running the tests in certain JS_GC_ZEAL modes or Valgrind, then some
// will time out if the CI machine is under a certain load. In that case
// increase the default timeout.
const gcZeal = GLib.getenv('JS_GC_ZEAL');
const valgrind = GLib.getenv('VALGRIND');
if (valgrind || (gcZeal && (gcZeal === '2' || gcZeal.startsWith('2,') || gcZeal === '4')))
    jasmine.DEFAULT_TIMEOUT_INTERVAL *= 5;

/**
 * The Promise (or null) that minijasmine-executor locks on
 * to avoid exiting prematurely
 */
export let mainloopLock = null;

/**
 * Stops the main loop but prevents the minijasmine-executor from
 * exiting. This is used for testing the main loop itself.
 *
 * @returns a callback which returns control of the main loop to
 *   minijasmine-executor
 */
export function acquireMainloop() {
    let resolve;
    mainloopLock = new Promise(_resolve => (resolve = _resolve));

    if (!mainloop.is_running())
        throw new Error("Main loop was stopped already, can't acquire");

    mainloop.quit();

    return () => {
        mainloopLock = null;
        resolve(true);
    };
}
