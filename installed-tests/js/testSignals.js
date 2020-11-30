/* eslint-disable no-restricted-properties */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC

const GLib = imports.gi.GLib;
const Lang = imports.lang;
const Signals = imports.signals;

const Foo = new Lang.Class({
    Name: 'Foo',
    Implements: [Signals.WithSignals],
    _init() {},
});

describe('Legacy object with signals', function () {
    testSignals(Foo);
});

class FooWithoutSignals {}
Signals.addSignalMethods(FooWithoutSignals.prototype);

describe('Object with signals added', function () {
    testSignals(FooWithoutSignals);
});

function testSignals(klass) {
    let foo, bar;
    beforeEach(function () {
        foo = new klass();
        bar = jasmine.createSpy('bar');
    });

    it('calls a signal handler when a signal is emitted', function () {
        foo.connect('bar', bar);
        foo.emit('bar', 'This is a', 'This is b');
        expect(bar).toHaveBeenCalledWith(foo, 'This is a', 'This is b');
    });

    it('does not call a signal handler after the signal is disconnected', function () {
        let id = foo.connect('bar', bar);
        foo.emit('bar', 'This is a', 'This is b');
        bar.calls.reset();
        foo.disconnect(id);
        // this emission should do nothing
        foo.emit('bar', 'Another a', 'Another b');
        expect(bar).not.toHaveBeenCalled();
    });

    it('can disconnect a signal handler during signal emission', function () {
        var toRemove = [];
        let firstId = foo.connect('bar', function (theFoo) {
            theFoo.disconnect(toRemove[0]);
            theFoo.disconnect(toRemove[1]);
        });
        toRemove.push(foo.connect('bar', bar));
        toRemove.push(foo.connect('bar', bar));

        // emit signal; what should happen is that the second two handlers are
        // disconnected before they get invoked
        foo.emit('bar');
        expect(bar).not.toHaveBeenCalled();

        // clean up the last handler
        foo.disconnect(firstId);

        // poke in private implementation to sanity-check no handlers left
        expect(foo._signalConnections.length).toEqual(0);
    });

    it('distinguishes multiple signals', function () {
        let bonk = jasmine.createSpy('bonk');
        foo.connect('bar', bar);
        foo.connect('bonk', bonk);
        foo.connect('bar', bar);

        foo.emit('bar');
        expect(bar).toHaveBeenCalledTimes(2);
        expect(bonk).not.toHaveBeenCalled();

        foo.emit('bonk');
        expect(bar).toHaveBeenCalledTimes(2);
        expect(bonk).toHaveBeenCalledTimes(1);

        foo.emit('bar');
        expect(bar).toHaveBeenCalledTimes(4);
        expect(bonk).toHaveBeenCalledTimes(1);

        foo.disconnectAll();
        bar.calls.reset();
        bonk.calls.reset();

        // these post-disconnect emissions should do nothing
        foo.emit('bar');
        foo.emit('bonk');
        expect(bar).not.toHaveBeenCalled();
        expect(bonk).not.toHaveBeenCalled();
    });

    it('determines if a signal is connected on a JS object', function () {
        let id = foo.connect('bar', bar);
        expect(foo.signalHandlerIsConnected(id)).toEqual(true);
        foo.disconnect(id);
        expect(foo.signalHandlerIsConnected(id)).toEqual(false);
    });

    describe('with exception in signal handler', function () {
        let bar2;
        beforeEach(function () {
            bar.and.throwError('Exception we are throwing on purpose');
            bar2 = jasmine.createSpy('bar');
            foo.connect('bar', bar);
            foo.connect('bar', bar2);
            GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_WARNING,
                'JS ERROR: Exception in callback for signal: *');
            foo.emit('bar');
        });

        it('does not affect other callbacks', function () {
            expect(bar).toHaveBeenCalledTimes(1);
            expect(bar2).toHaveBeenCalledTimes(1);
        });

        it('does not disconnect the callback', function () {
            GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_WARNING,
                'JS ERROR: Exception in callback for signal: *');
            foo.emit('bar');
            expect(bar).toHaveBeenCalledTimes(2);
            expect(bar2).toHaveBeenCalledTimes(2);
        });
    });
}
