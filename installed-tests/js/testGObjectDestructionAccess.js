// -*- mode: js; indent-tabs-mode: nil -*-
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2017 Canonical, Ltd.

import Gio from 'gi://Gio';
import GjsTestTools from 'gi://GjsTestTools';
import GLib from 'gi://GLib';
import GObject from 'gi://GObject';
import Gtk from 'gi://Gtk?version=3.0';
import System from 'system';

describe('Access to destroyed GObject', function () {
    let destroyedWindow;

    beforeAll(function () {
        Gtk.init(null);
    });

    beforeEach(function () {
        destroyedWindow = new Gtk.Window({type: Gtk.WindowType.TOPLEVEL});
        destroyedWindow.set_title('To be destroyed');
        destroyedWindow.destroy();
    });

    it('Get property', function () {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_CRITICAL,
            'Object Gtk.Window (0x* disposed *');

        expect(destroyedWindow.title).toBe('To be destroyed');

        GLib.test_assert_expected_messages_internal('Gjs', 'testGObjectDestructionAccess.js', 0,
            'testExceptionInDestroyedObjectPropertyGet');
    });

    it('Set property', function () {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_CRITICAL,
            'Object Gtk.Window (0x* disposed *');
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_CRITICAL,
            'Object Gtk.Window (0x* disposed *');

        destroyedWindow.title = 'I am dead';
        expect(destroyedWindow.title).toBe('I am dead');

        GLib.test_assert_expected_messages_internal('Gjs', 'testGObjectDestructionAccess.js', 0,
            'testExceptionInDestroyedObjectPropertySet');
    });

    it('Add expando property', function () {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_CRITICAL,
            'Object Gtk.Window (0x* disposed *');

        destroyedWindow.expandoProperty = 'Hello!';

        GLib.test_assert_expected_messages_internal('Gjs', 'testGObjectDestructionAccess.js', 0,
            'testExceptionInDestroyedObjectExpandoPropertySet');
    });

    it('Access to unset expando property', function () {
        expect(destroyedWindow.expandoProperty).toBeUndefined();
    });

    it('Access previously set expando property', function () {
        destroyedWindow = new Gtk.Window({type: Gtk.WindowType.TOPLEVEL});
        destroyedWindow.expandoProperty = 'Hello!';
        destroyedWindow.destroy();

        expect(destroyedWindow.expandoProperty).toBe('Hello!');
    });

    it('Access to getter method', function () {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_CRITICAL,
            'Object Gtk.Window (0x* disposed *');

        expect(destroyedWindow.get_title()).toBe('To be destroyed');

        GLib.test_assert_expected_messages_internal('Gjs', 'testGObjectDestructionAccess.js', 0,
            'testExceptionInDestroyedObjectMethodGet');
    });

    it('Access to setter method', function () {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_CRITICAL,
            'Object Gtk.Window (0x* disposed *');
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_CRITICAL,
            'Object Gtk.Window (0x* disposed *');

        destroyedWindow.set_title('I am dead');
        expect(destroyedWindow.get_title()).toBe('I am dead');

        GLib.test_assert_expected_messages_internal('Gjs', 'testGObjectDestructionAccess.js', 0,
            'testExceptionInDestroyedObjectMethodSet');
    });

    it('Proto function connect', function () {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_CRITICAL,
            'Object Gtk.Window (0x* disposed *');

        expect(destroyedWindow.connect('foo-signal', () => {})).toBe(0);

        GLib.test_assert_expected_messages_internal('Gjs', 'testGObjectDestructionAccess.js', 0,
            'testExceptionInDestroyedObjectConnect');
    });

    it('Proto function connect_after', function () {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_CRITICAL,
            'Object Gtk.Window (0x* disposed *');

        expect(destroyedWindow.connect_after('foo-signal', () => {})).toBe(0);

        GLib.test_assert_expected_messages_internal('Gjs', 'testGObjectDestructionAccess.js', 0,
            'testExceptionInDestroyedObjectConnectAfter');
    });

    it('Proto function emit', function () {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_CRITICAL,
            'Object Gtk.Window (0x* disposed *');

        expect(destroyedWindow.emit('keys-changed')).toBeUndefined();

        GLib.test_assert_expected_messages_internal('Gjs', 'testGObjectDestructionAccess.js', 0,
            'testExceptionInDestroyedObjectEmit');
    });

    it('Proto function signals_disconnect', function () {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_CRITICAL,
            'Object Gtk.Window (0x* disposed *');

        expect(GObject.signal_handlers_disconnect_by_func(destroyedWindow, () => {})).toBe(0);

        GLib.test_assert_expected_messages_internal('Gjs', 'testGObjectDestructionAccess.js', 0,
            'testExceptionInDestroyedObjectSignalsDisconnect');
    });

    it('Proto function signals_block', function () {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_CRITICAL,
            'Object Gtk.Window (0x* disposed *');

        expect(GObject.signal_handlers_block_by_func(destroyedWindow, () => {})).toBe(0);

        GLib.test_assert_expected_messages_internal('Gjs', 'testGObjectDestructionAccess.js', 0,
            'testExceptionInDestroyedObjectSignalsBlock');
    });

    it('Proto function signals_unblock', function () {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_CRITICAL,
            'Object Gtk.Window (0x* disposed *');

        expect(GObject.signal_handlers_unblock_by_func(destroyedWindow, () => {})).toBe(0);

        GLib.test_assert_expected_messages_internal('Gjs', 'testGObjectDestructionAccess.js', 0,
            'testExceptionInDestroyedObjectSignalsUnblock');
    });

    it('Proto function toString', function () {
        expect(destroyedWindow.toString()).toMatch(
            /\[object \(DISPOSED\) instance wrapper GIName:Gtk.Window jsobj@0x[a-f0-9]+ native@0x[a-f0-9]+\]/);
    });

    it('Proto function toString before/after', function () {
        var validWindow = new Gtk.Window({type: Gtk.WindowType.TOPLEVEL});

        expect(validWindow.toString()).toMatch(
            /\[object instance wrapper GIName:Gtk.Window jsobj@0x[a-f0-9]+ native@0x[a-f0-9]+\]/);

        validWindow.destroy();

        expect(validWindow.toString()).toMatch(
            /\[object \(DISPOSED\) instance wrapper GIName:Gtk.Window jsobj@0x[a-f0-9]+ native@0x[a-f0-9]+\]/);
    });
});

describe('Access to finalized GObject', function () {
    let destroyedWindow;

    beforeAll(function () {
        Gtk.init(null);
    });

    beforeEach(function () {
        destroyedWindow = new Gtk.Window({type: Gtk.WindowType.TOPLEVEL});
        destroyedWindow.set_title('To be destroyed');
        destroyedWindow.destroy();

        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_CRITICAL,
            'Object Gtk.Window (0x* disposed *');
        GjsTestTools.unref(destroyedWindow);
        GLib.test_assert_expected_messages_internal('Gjs', 'testGObjectDestructionAccess.js', 0,
            'testExceptionInDestroyedObjectPropertyGet');
    });

    afterEach(function () {
        destroyedWindow = null;
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_CRITICAL,
            '*Object 0x* has been finalized *');
        System.gc();
        GLib.test_assert_expected_messages_internal('Gjs', 'testGObjectDestructionAccess.js', 0,
            'generates a warn on object garbage collection');
    });

    it('Get property', function () {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_CRITICAL,
            'Object Gtk.Window (0x* finalized *');

        expect(destroyedWindow.title).toBeUndefined();

        GLib.test_assert_expected_messages_internal('Gjs', 'testGObjectDestructionAccess.js', 0,
            'testExceptionInDestroyedObjectPropertyGet');
    });

    it('Set property', function () {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_CRITICAL,
            'Object Gtk.Window (0x* finalized *');
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_CRITICAL,
            'Object Gtk.Window (0x* finalized *');

        destroyedWindow.title = 'I am dead';
        expect(destroyedWindow.title).toBeUndefined();

        GLib.test_assert_expected_messages_internal('Gjs', 'testGObjectDestructionAccess.js', 0,
            'testExceptionInDestroyedObjectPropertySet');
    });

    it('Add expando property', function () {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_CRITICAL,
            'Object Gtk.Window (0x* finalized *');

        destroyedWindow.expandoProperty = 'Hello!';

        GLib.test_assert_expected_messages_internal('Gjs', 'testGObjectDestructionAccess.js', 0,
            'testExceptionInDestroyedObjectExpandoPropertySet');
    });

    it('Access to unset expando property', function () {
        expect(destroyedWindow.expandoProperty).toBeUndefined();
    });

    it('Access previously set expando property', function () {
        destroyedWindow = new Gtk.Window({type: Gtk.WindowType.TOPLEVEL});
        destroyedWindow.expandoProperty = 'Hello!';
        destroyedWindow.destroy();

        expect(destroyedWindow.expandoProperty).toBe('Hello!');
    });

    it('Access to getter method', function () {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_CRITICAL,
            'Object Gtk.Window (0x* finalized *');
        GLib.test_expect_message('Gtk', GLib.LogLevelFlags.LEVEL_CRITICAL,
            '*GTK_IS_WINDOW*');

        expect(destroyedWindow.get_title()).toBeNull();

        GLib.test_assert_expected_messages_internal('Gjs', 'testGObjectDestructionAccess.js', 0,
            'testExceptionInDestroyedObjectMethodGet');
    });

    it('Access to setter method', function () {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_CRITICAL,
            'Object Gtk.Window (0x* finalized *');
        GLib.test_expect_message('Gtk', GLib.LogLevelFlags.LEVEL_CRITICAL,
            '*GTK_IS_WINDOW*');

        destroyedWindow.set_title('I am dead');

        GLib.test_assert_expected_messages_internal('Gjs', 'testGObjectDestructionAccess.js', 0,
            'testExceptionInDestroyedObjectMethodSet');
    });

    it('Proto function connect', function () {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_CRITICAL,
            'Object Gtk.Window (0x* finalized *');

        expect(destroyedWindow.connect('foo-signal', () => { })).toBe(0);

        GLib.test_assert_expected_messages_internal('Gjs', 'testGObjectDestructionAccess.js', 0,
            'testExceptionInDestroyedObjectConnect');
    });

    it('Proto function connect_after', function () {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_CRITICAL,
            'Object Gtk.Window (0x* finalized *');

        expect(destroyedWindow.connect_after('foo-signal', () => { })).toBe(0);

        GLib.test_assert_expected_messages_internal('Gjs', 'testGObjectDestructionAccess.js', 0,
            'testExceptionInDestroyedObjectConnectAfter');
    });

    it('Proto function emit', function () {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_CRITICAL,
            'Object Gtk.Window (0x* finalized *');

        expect(destroyedWindow.emit('keys-changed')).toBeUndefined();

        GLib.test_assert_expected_messages_internal('Gjs', 'testGObjectDestructionAccess.js', 0,
            'testExceptionInDestroyedObjectEmit');
    });

    it('Proto function signals_disconnect', function () {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_CRITICAL,
            'Object Gtk.Window (0x* finalized *');

        expect(GObject.signal_handlers_disconnect_by_func(destroyedWindow, () => { })).toBe(0);

        GLib.test_assert_expected_messages_internal('Gjs', 'testGObjectDestructionAccess.js', 0,
            'testExceptionInDestroyedObjectSignalsDisconnect');
    });

    it('Proto function signals_block', function () {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_CRITICAL,
            'Object Gtk.Window (0x* finalized *');

        expect(GObject.signal_handlers_block_by_func(destroyedWindow, () => { })).toBe(0);

        GLib.test_assert_expected_messages_internal('Gjs', 'testGObjectDestructionAccess.js', 0,
            'testExceptionInDestroyedObjectSignalsBlock');
    });

    it('Proto function signals_unblock', function () {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_CRITICAL,
            'Object Gtk.Window (0x* finalized *');

        expect(GObject.signal_handlers_unblock_by_func(destroyedWindow, () => { })).toBe(0);

        GLib.test_assert_expected_messages_internal('Gjs', 'testGObjectDestructionAccess.js', 0,
            'testExceptionInDestroyedObjectSignalsUnblock');
    });

    it('Proto function toString', function () {
        expect(destroyedWindow.toString()).toMatch(
            /\[object \(FINALIZED\) instance wrapper GIName:Gtk.Window jsobj@0x[a-f0-9]+ native@0x[a-f0-9]+\]/);
    });
});

describe('Disposed or finalized GObject', function () {
    beforeAll(function () {
        GjsTestTools.init();
    });

    afterEach(function () {
        GjsTestTools.reset();
    });

    [true, false].forEach(gc => {
        it(`is marked as disposed when it is a manually disposed property ${gc ? '' : 'not '}garbage collected`, function () {
            const emblem = new Gio.EmblemedIcon({
                gicon: new Gio.ThemedIcon({name: 'alarm'}),
            });

            let {gicon} = emblem;
            gicon.run_dispose();
            gicon = null;
            System.gc();

            Array(10).fill().forEach(() => {
                // We need to repeat the test to ensure that we disassociate
                // wrappers from disposed objects on destruction.
                gicon = emblem.gicon;
                expect(gicon.toString()).toMatch(
                    /\[object \(DISPOSED\) instance wrapper .* jsobj@0x[a-f0-9]+ native@0x[a-f0-9]+\]/);

                gicon = null;
                if (gc)
                    System.gc();
            });
        });
    });

    it('calls dispose vfunc on explicit disposal only', function () {
        const callSpy = jasmine.createSpy('vfunc_dispose');
        const DisposeFile = GObject.registerClass(class DisposeFile extends Gio.ThemedIcon {
            vfunc_dispose(...args) {
                expect(this.names).toEqual(['dummy', 'dummy-symbolic']);
                callSpy(...args);
            }
        });

        let file = new DisposeFile({name: 'dummy'});
        file.run_dispose();
        expect(callSpy).toHaveBeenCalledOnceWith();

        file.run_dispose();
        expect(callSpy).toHaveBeenCalledTimes(2);
        file = null;

        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_CRITICAL,
            '*during garbage collection*offending callback was dispose()*');
        System.gc();
        GLib.test_assert_expected_messages_internal('Gjs', 'testGObjectDestructionAccess.js', 0,
            'calls dispose vfunc on explicit disposal only');

        expect(callSpy).toHaveBeenCalledTimes(2);
    });

    it('generates a warn on object garbage collection', function () {
        GjsTestTools.unref(Gio.File.new_for_path('/'));

        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_CRITICAL,
            '*Object 0x* has been finalized *');
        System.gc();
        GLib.test_assert_expected_messages_internal('Gjs', 'testGObjectDestructionAccess.js', 0,
            'generates a warn on object garbage collection');
    });

    it('generates a warn on object garbage collection if has expando property', function () {
        let file = Gio.File.new_for_path('/');
        file.toggleReferenced = true;
        GjsTestTools.unref(file);
        expect(file.toString()).toMatch(
            /\[object \(FINALIZED\) instance wrapper GType:GLocalFile jsobj@0x[a-f0-9]+ native@0x[a-f0-9]+\]/);
        file = null;
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_CRITICAL,
            '*Object 0x* has been finalized *');
        System.gc();
        GLib.test_assert_expected_messages_internal('Gjs', 'testGObjectDestructionAccess.js', 0,
            'generates a warn on object garbage collection if has expando property');
    });

    it('generates a warn if already disposed at garbage collection', function () {
        const loop = new GLib.MainLoop(null, false);

        let file = Gio.File.new_for_path('/');
        GjsTestTools.delayed_unref(file, 1);  // Will happen after dispose
        file.run_dispose();

        let done = false;
        GLib.timeout_add(GLib.PRIORITY_DEFAULT, 50, () => (done = true));
        while (!done)
            loop.get_context().iteration(true);

        file = null;
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_CRITICAL,
            '*Object 0x* has been finalized *');
        System.gc();
        GLib.test_assert_expected_messages_internal('Gjs', 'testGObjectDestructionAccess.js', 0,
            'generates a warn if already disposed at garbage collection');
    });

    [true, false].forEach(gc => {
        it(`created from other function is marked as disposed and ${gc ? '' : 'not '}garbage collected`, function () {
            let file = Gio.File.new_for_path('/');
            GjsTestTools.save_object(file);
            file.run_dispose();
            file = null;
            System.gc();

            Array(10).fill().forEach(() => {
                // We need to repeat the test to ensure that we disassociate
                // wrappers from disposed objects on destruction.
                expect(GjsTestTools.peek_saved()).toMatch(
                    /\[object \(DISPOSED\) instance wrapper GType:GLocalFile jsobj@0x[a-f0-9]+ native@0x[a-f0-9]+\]/);
                if (gc)
                    System.gc();
            });
        });
    });

    it('returned from function is marked as disposed', function () {
        expect(GjsTestTools.get_disposed(Gio.File.new_for_path('/'))).toMatch(
            /\[object \(DISPOSED\) instance wrapper GType:GLocalFile jsobj@0x[a-f0-9]+ native@0x[a-f0-9]+\]/);
    });

    it('returned from function is marked as disposed and then as finalized', function () {
        let file = Gio.File.new_for_path('/');
        GjsTestTools.save_object(file);
        GjsTestTools.delayed_unref(file, 30);
        file.run_dispose();

        let disposedFile = GjsTestTools.get_saved();
        expect(disposedFile).toEqual(file);
        expect(disposedFile).toMatch(
            /\[object \(DISPOSED\) instance wrapper GType:GLocalFile jsobj@0x[a-f0-9]+ native@0x[a-f0-9]+\]/);

        file = null;
        System.gc();

        const loop = new GLib.MainLoop(null, false);
        GLib.timeout_add(GLib.PRIORITY_DEFAULT, 50, () => loop.quit());
        loop.run();

        expect(disposedFile).toMatch(
            /\[object \(FINALIZED\) instance wrapper GType:GLocalFile jsobj@0x[a-f0-9]+ native@0x[a-f0-9]+\]/);

        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_CRITICAL,
            '*Object 0x* has been finalized *');
        disposedFile = null;
        System.gc();
        GLib.test_assert_expected_messages_internal('Gjs', 'testGObjectDestructionAccess.js', 0,
            'returned from function is marked as disposed and then as finalized');
    });

    it('ignores toggling queued unref toggles', function () {
        let file = Gio.File.new_for_path('/');
        file.expandMeWithToggleRef = true;
        GjsTestTools.ref(file);
        GjsTestTools.unref_other_thread(file);
        file.run_dispose();
    });

    it('ignores toggling queued toggles', function () {
        let file = Gio.File.new_for_path('/');
        file.expandMeWithToggleRef = true;
        GjsTestTools.ref_other_thread(file);
        GjsTestTools.unref_other_thread(file);
        file.run_dispose();
    });

    it('can be disposed from other thread', function () {
        let file = Gio.File.new_for_path('/');
        file.expandMeWithToggleRef = true;
        GjsTestTools.ref(file);
        GjsTestTools.unref_other_thread(file);
        GjsTestTools.run_dispose_other_thread(file);
    });

    it('can be garbage collected once disposed from other thread', function () {
        let file = Gio.File.new_for_path('/');
        file.expandMeWithToggleRef = true;
        GjsTestTools.run_dispose_other_thread(file);
        file = null;
        System.gc();
    });
});

describe('GObject with toggle references', function () {
    beforeAll(function () {
        GjsTestTools.init();
    });

    afterEach(function () {
        GjsTestTools.reset();
    });

    it('can be re-reffed from other thread delayed', function () {
        let file = Gio.File.new_for_path('/');
        file.expandMeWithToggleRef = true;
        const objectAddress = System.addressOfGObject(file);
        GjsTestTools.save_object_unreffed(file);
        GjsTestTools.delayed_ref_other_thread(file, 10);
        file = null;
        System.gc();

        const loop = new GLib.MainLoop(null, false);
        GLib.timeout_add(GLib.PRIORITY_DEFAULT, 50, () => loop.quit());
        loop.run();

        // We need to cleanup the extra ref we added before now.
        // However, depending on whether the thread ref happens the object
        // may be already finalized, and in such case we need to throw
        try {
            file = GjsTestTools.steal_saved();
            if (file) {
                expect(System.addressOfGObject(file)).toBe(objectAddress);
                expect(file instanceof Gio.File).toBeTruthy();
                GjsTestTools.unref(file);
            }
        } catch (e) {
            expect(() => {
                throw e;
            }).toThrowError(/.*Unhandled GType.*/);
        }
    });

    it('can be re-reffed and unreffed again from other thread', function () {
        let file = Gio.File.new_for_path('/');
        const objectAddress = System.addressOfGObject(file);
        file.expandMeWithToggleRef = true;
        GjsTestTools.save_object(file);
        GjsTestTools.ref(file);
        GjsTestTools.delayed_unref_other_thread(file, 10);
        file = null;
        System.gc();

        const loop = new GLib.MainLoop(null, false);
        GLib.timeout_add(GLib.PRIORITY_DEFAULT, 50, () => loop.quit());
        loop.run();

        file = GjsTestTools.get_saved();
        expect(System.addressOfGObject(file)).toBe(objectAddress);
        expect(file instanceof Gio.File).toBeTruthy();
    });

    it('can be re-reffed and unreffed again from other thread with delay', function () {
        let file = Gio.File.new_for_path('/');
        file.expandMeWithToggleRef = true;
        GjsTestTools.delayed_ref_unref_other_thread(file, 10);
        file = null;
        System.gc();

        const loop = new GLib.MainLoop(null, false);
        GLib.timeout_add(GLib.PRIORITY_DEFAULT, 50, () => loop.quit());
        loop.run();
    });

    it('can be toggled up by getting a GWeakRef', function () {
        let file = Gio.File.new_for_path('/');
        file.expandMeWithToggleRef = true;
        GjsTestTools.save_weak(file);
        GjsTestTools.get_weak();
    });

    it('can be toggled up by getting a GWeakRef from another thread', function () {
        let file = Gio.File.new_for_path('/');
        file.expandMeWithToggleRef = true;
        GjsTestTools.save_weak(file);
        GjsTestTools.get_weak_other_thread();
    });

    it('can be toggled up by getting a GWeakRef from another thread and re-reffed in main thread', function () {
        let file = Gio.File.new_for_path('/');
        file.expandMeWithToggleRef = true;
        GjsTestTools.save_weak(file);
        GjsTestTools.get_weak_other_thread();

        // Ok, let's play more dirty now...
        GjsTestTools.ref(file); // toggle up
        GjsTestTools.unref(file); // toggle down

        GjsTestTools.ref(file);
        GjsTestTools.ref(file);
        GjsTestTools.unref(file);
        GjsTestTools.unref(file);
    });

    it('can be toggled up by getting a GWeakRef from another and re-reffed from various threads', function () {
        let file = Gio.File.new_for_path('/');
        file.expandMeWithToggleRef = true;
        GjsTestTools.save_weak(file);
        GjsTestTools.get_weak_other_thread();

        GjsTestTools.ref_other_thread(file);
        GjsTestTools.unref_other_thread(file);

        GjsTestTools.ref(file);
        GjsTestTools.unref(file);

        GjsTestTools.ref_other_thread(file);
        GjsTestTools.unref(file);

        GjsTestTools.ref(file);
        GjsTestTools.unref_other_thread(file);
    });

    it('can be toggled up-down from various threads when the wrapper is gone', function () {
        let file = Gio.File.new_for_path('/');
        file.expandMeWithToggleRef = true;

        // We also check that late thread events won't affect the destroyed wrapper
        const threads = [];
        threads.push(GjsTestTools.delayed_ref_unref_other_thread(file, 0));
        threads.push(GjsTestTools.delayed_ref_unref_other_thread(file, 100000));
        threads.push(GjsTestTools.delayed_ref_unref_other_thread(file, 200000));
        threads.push(GjsTestTools.delayed_ref_unref_other_thread(file, 300000));
        GjsTestTools.save_object(file);
        GjsTestTools.save_weak(file);
        file = null;
        System.gc();

        threads.forEach(th => th.join());
        GjsTestTools.clear_saved();
        System.gc();

        expect(GjsTestTools.get_weak()).toBeNull();
    });

    it('can be toggled up-down from various threads when disposed and the wrapper is gone', function () {
        let file = Gio.File.new_for_path('/');
        file.expandMeWithToggleRef = true;

        // We also check that late thread events won't affect the destroyed wrapper
        const threads = [];
        threads.push(GjsTestTools.delayed_ref_unref_other_thread(file, 0));
        threads.push(GjsTestTools.delayed_ref_unref_other_thread(file, 100000));
        threads.push(GjsTestTools.delayed_ref_unref_other_thread(file, 200000));
        threads.push(GjsTestTools.delayed_ref_unref_other_thread(file, 300000));
        GjsTestTools.save_object(file);
        GjsTestTools.save_weak(file);
        file.run_dispose();
        file = null;
        System.gc();

        threads.forEach(th => th.join());
        GjsTestTools.clear_saved();
        expect(GjsTestTools.get_weak()).toBeNull();
    });

    it('can be finalized while queued in toggle queue', function () {
        let file = Gio.File.new_for_path('/');
        file.expandMeWithToggleRef = true;
        GjsTestTools.ref(file);
        GjsTestTools.unref_other_thread(file);
        GjsTestTools.unref_other_thread(file);

        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_CRITICAL,
            '*Object 0x* has been finalized *');
        file = null;
        System.gc();
        GLib.test_assert_expected_messages_internal('Gjs', 'testGObjectDestructionAccess.js', 0,
            'can be finalized while queued in toggle queue');
    });

    xit('can be toggled up-down from various threads while getting a GWeakRef from main', function () {
        let file = Gio.File.new_for_path('/');
        file.expandMeWithToggleRef = true;
        GjsTestTools.save_weak(file);

        const ids = [];
        let threads = [];
        ids.push(GLib.idle_add(GLib.PRIORITY_DEFAULT, () => {
            threads = threads.slice(-50);
            try {
                threads.push(GjsTestTools.delayed_ref_unref_other_thread(file, 1));
            } catch {
                // If creating the thread failed we're almost going out of memory
                // so let's first wait for the ones allocated to complete.
                threads.forEach(th => th.join());
                threads = [];
            }
            return GLib.SOURCE_CONTINUE;
        }));

        const loop = new GLib.MainLoop(null, false);
        ids.push(GLib.idle_add(GLib.PRIORITY_DEFAULT, () => {
            expect(GjsTestTools.get_weak()).toEqual(file);
            return GLib.SOURCE_CONTINUE;
        }));

        // We must not timeout due to deadlock #404 and finally not crash per #297
        GLib.timeout_add(GLib.PRIORITY_DEFAULT, 3000, () => loop.quit());
        loop.run();
        ids.forEach(id => GLib.source_remove(id));

        // We also check that late thread events won't affect the destroyed wrapper
        GjsTestTools.save_object(file);
        file = null;
        System.gc();
        threads.forEach(th => th.join());
        expect(GjsTestTools.get_saved_ref_count()).toBeGreaterThan(0);

        GjsTestTools.clear_saved();
        System.gc();
        expect(GjsTestTools.get_weak()).toBeNull();
    }).pend('Flaky, see https://gitlab.gnome.org/GNOME/gjs/-/issues/568');
});
