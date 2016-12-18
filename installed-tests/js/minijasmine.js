#!/usr/bin/env gjs

const GLib = imports.gi.GLib;
const Lang = imports.lang;

function _removeNewlines(str) {
    let allNewlines = /\n/g;
    return str.replace(allNewlines, '\\n');
}

function _filterStack(stack) {
    return stack.split('\n')
        .filter(stackLine => stackLine.indexOf('resource:///org/gjs/jsunit') === -1)
        .filter(stackLine => stackLine.indexOf('<jasmine-start>') === -1)
        .join('\n');
}

function _setTimeoutInternal(continueTimeout, func, time) {
    return GLib.timeout_add(GLib.PRIORITY_DEFAULT, time, function () {
        func();
        return continueTimeout;
    });
}

function _clearTimeoutInternal(id) {
    if (id > 0)
        GLib.source_remove(id);
}

// Install the browser setTimeout/setInterval API on the global object
window.setTimeout = _setTimeoutInternal.bind(undefined, GLib.SOURCE_REMOVE);
window.setInterval = _setTimeoutInternal.bind(undefined, GLib.SOURCE_CONTINUE);
window.clearTimeout = window.clearInterval = _clearTimeoutInternal;

let jasmineRequire = imports.jasmine.getJasmineRequireObj();
let jasmineCore = jasmineRequire.core(jasmineRequire);
window._jasmineEnv = jasmineCore.getEnv();

window._jasmineMain = GLib.MainLoop.new(null, false);
window._jasmineRetval = 0;

// Install Jasmine API on the global object
let jasmineInterface = jasmineRequire.interface(jasmineCore, window._jasmineEnv);
Lang.copyProperties(jasmineInterface, window);

// Reporter that outputs according to the Test Anything Protocol
// See http://testanything.org/tap-specification.html
const TapReporter = new Lang.Class({
    Name: 'TapReporter',

    _init: function () {
        this._failedSuites = [];
        this._specCount = 0;
    },

    jasmineStarted: function (info) {
        print('1..' + info.totalSpecsDefined);
    },

    jasmineDone: function () {
        this._failedSuites.forEach(failure => {
            failure.failedExpectations.forEach(result => {
                print('not ok - An error was thrown outside a test');
                print('# ' + result.message);
            });
        });

        window._jasmineMain.quit();
    },

    suiteDone: function (result) {
        if (result.failedExpectations && result.failedExpectations.length > 0) {
            window._jasmineRetval = 1;
            this._failedSuites.push(result);
        }

        if (result.status === 'disabled') {
            print('# Suite was disabled:', result.fullName);
        }
    },

    specStarted: function () {
        this._specCount++;
    },

    specDone: function (result) {
        let tap_report;
        if (result.status === 'failed') {
            window._jasmineRetval = 1;
            tap_report = 'not ok';
        } else {
            tap_report = 'ok';
        }
        tap_report += ' ' + this._specCount + ' ' + result.fullName;
        if (result.status === 'pending' || result.status === 'disabled') {
            let reason = result.pendingReason || result.status;
            tap_report += ' # SKIP ' + reason;
        }
        print(tap_report);

        // Print additional diagnostic info on failure
        if (result.status === 'failed' && result.failedExpectations) {
            result.failedExpectations.forEach((failedExpectation) => {
                print('# Message:', _removeNewlines(failedExpectation.message));
                print('# Stack:');
                let stackTrace = _filterStack(failedExpectation.stack).trim();
                print(stackTrace.split('\n').map((str) => '#   ' + str).join('\n'));
            });
        }
    },
});

window._jasmineEnv.addReporter(new TapReporter());
