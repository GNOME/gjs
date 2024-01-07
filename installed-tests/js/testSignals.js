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

    it('emit works with no connections', function () {
        expect(() => foo.emit('random-event')).not.toThrow();
    });

    ['connect', 'connectAfter'].forEach(connectMethod => {
        describe(`using ${connectMethod}`, function () {
            it('calls a signal handler when a signal is emitted', function () {
                foo[connectMethod]('bar', bar);
                foo.emit('bar', 'This is a', 'This is b');
                expect(bar).toHaveBeenCalledWith(foo, 'This is a', 'This is b');
            });

            it('calls remaining handlers after one is disconnected', function () {
                const id1 = foo[connectMethod]('bar', bar);

                const bar2 = jasmine.createSpy('bar2');
                const id2 = foo[connectMethod]('bar', bar2);

                foo.emit('bar');
                expect(bar).toHaveBeenCalledTimes(1);
                expect(bar2).toHaveBeenCalledTimes(1);

                foo.disconnect(id1);

                foo.emit('bar');

                expect(bar).toHaveBeenCalledTimes(1);
                expect(bar2).toHaveBeenCalledTimes(2);

                foo.disconnect(id2);
            });

            it('does not call a signal handler after the signal is disconnected', function () {
                let id = foo[connectMethod]('bar', bar);
                foo.emit('bar', 'This is a', 'This is b');
                bar.calls.reset();
                foo.disconnect(id);
                // this emission should do nothing
                foo.emit('bar', 'Another a', 'Another b');
                expect(bar).not.toHaveBeenCalled();
            });

            it('can disconnect a signal handler during signal emission', function () {
                var toRemove = [];
                let firstId = foo[connectMethod]('bar', function (theFoo) {
                    theFoo.disconnect(toRemove[0]);
                    theFoo.disconnect(toRemove[1]);
                });
                toRemove.push(foo[connectMethod]('bar', bar));
                toRemove.push(foo[connectMethod]('bar', bar));

                // emit signal; what should happen is that the second two handlers are
                // disconnected before they get invoked
                foo.emit('bar');
                expect(bar).not.toHaveBeenCalled();

                // clean up the last handler
                foo.disconnect(firstId);

                expect(() => foo.disconnect(firstId)).toThrowError(
                    `No signal connection ${firstId} found`);

                // poke in private implementation to verify no handlers left
                expect(Object.keys(foo._signalConnections).length).toEqual(0);
            });

            it('distinguishes multiple signals', function () {
                let bonk = jasmine.createSpy('bonk');
                foo[connectMethod]('bar', bar);
                foo[connectMethod]('bonk', bonk);
                foo[connectMethod]('bar', bar);

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
                let id = foo[connectMethod]('bar', bar);
                expect(foo.signalHandlerIsConnected(id)).toEqual(true);
                foo.disconnect(id);
                expect(foo.signalHandlerIsConnected(id)).toEqual(false);
            });

            it('does not call a subsequent connected callbacks if stopped by earlier', function () {
                const afterBar = jasmine.createSpy('bar');
                const afterAfterBar = jasmine.createSpy('barBar');
                foo[connectMethod]('bar', bar.and.returnValue(true));
                foo[connectMethod]('bar', afterBar);
                foo[connectMethod]('bar', afterAfterBar);
                foo.emit('bar', 'This is a', 123);
                expect(bar).toHaveBeenCalledWith(foo, 'This is a', 123);
                expect(afterBar).not.toHaveBeenCalled();
                expect(afterAfterBar).not.toHaveBeenCalled();
            });

            describe('with exception in signal handler', function () {
                let bar2;
                beforeEach(function () {
                    bar.and.throwError('Exception we are throwing on purpose');
                    bar2 = jasmine.createSpy('bar');
                    foo[connectMethod]('bar', bar);
                    foo[connectMethod]('bar', bar2);
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
        });
    });

    it('using connectAfter calls a signal handler later than when using connect when a signal is emitted', function () {
        const afterBar = jasmine.createSpy('bar');
        foo.connectAfter('bar', (...args) => {
            expect(bar).toHaveBeenCalledWith(foo, 'This is a', 'This is b');
            afterBar(...args);
        });
        foo.connect('bar', bar);
        foo.emit('bar', 'This is a', 'This is b');
        expect(afterBar).toHaveBeenCalledWith(foo, 'This is a', 'This is b');
    });

    it('does not call a connected after handler when stopped by connect', function () {
        const afterBar = jasmine.createSpy('bar');
        foo.connectAfter('bar', afterBar);
        foo.connect('bar', bar.and.returnValue(true));
        foo.emit('bar', 'This is a', 'This is b');
        expect(bar).toHaveBeenCalledWith(foo, 'This is a', 'This is b');
        expect(afterBar).not.toHaveBeenCalled();
    });
}
