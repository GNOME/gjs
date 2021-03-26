// -*- mode: js; indent-tabs-mode: nil -*-
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2017 Canonical, Ltd.

imports.gi.versions.Gtk = '3.0';

const {GLib, Gio, GjsTestTools, GObject, Gtk} = imports.gi;
const {system: System} = imports;

describe('Access to destroyed GObject', function () {
    let destroyedWindow;

    beforeAll(function () {
        Gtk.init(null);
    });

    beforeEach(function () {
        destroyedWindow = new Gtk.Window({type: Gtk.WindowType.TOPLEVEL});
        destroyedWindow.destroy();
    });

    it('Get property', function () {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_CRITICAL,
            'Object Gtk.Window (0x*');

        expect(destroyedWindow.title).toBeUndefined();

        GLib.test_assert_expected_messages_internal('Gjs', 'testGObjectDestructionAccess.js', 0,
            'testExceptionInDestroyedObjectPropertyGet');
    });

    it('Set property', function () {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_CRITICAL,
            'Object Gtk.Window (0x*');

        destroyedWindow.title = 'I am dead';

        GLib.test_assert_expected_messages_internal('Gjs', 'testGObjectDestructionAccess.js', 0,
            'testExceptionInDestroyedObjectPropertySet');
    });

    it('Add expando property', function () {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_CRITICAL,
            'Object Gtk.Window (0x*');

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
            'Object Gtk.Window (0x*');
        GLib.test_expect_message('Gtk', GLib.LogLevelFlags.LEVEL_CRITICAL,
            '*GTK_IS_WINDOW*');

        expect(destroyedWindow.get_title()).toBeNull();

        GLib.test_assert_expected_messages_internal('Gjs', 'testGObjectDestructionAccess.js', 0,
            'testExceptionInDestroyedObjectMethodGet');
    });

    it('Access to setter method', function () {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_CRITICAL,
            'Object Gtk.Window (0x*');
        GLib.test_expect_message('Gtk', GLib.LogLevelFlags.LEVEL_CRITICAL,
            '*GTK_IS_WINDOW*');

        destroyedWindow.set_title('I am dead');

        GLib.test_assert_expected_messages_internal('Gjs', 'testGObjectDestructionAccess.js', 0,
            'testExceptionInDestroyedObjectMethodSet');
    });

    it('Proto function connect', function () {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_CRITICAL,
            'Object Gtk.Window (0x*');

        expect(destroyedWindow.connect('foo-signal', () => {})).toBe(0);

        GLib.test_assert_expected_messages_internal('Gjs', 'testGObjectDestructionAccess.js', 0,
            'testExceptionInDestroyedObjectConnect');
    });

    it('Proto function connect_after', function () {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_CRITICAL,
            'Object Gtk.Window (0x*');

        expect(destroyedWindow.connect_after('foo-signal', () => {})).toBe(0);

        GLib.test_assert_expected_messages_internal('Gjs', 'testGObjectDestructionAccess.js', 0,
            'testExceptionInDestroyedObjectConnectAfter');
    });

    it('Proto function emit', function () {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_CRITICAL,
            'Object Gtk.Window (0x*');

        expect(destroyedWindow.emit('foo-signal')).toBeUndefined();

        GLib.test_assert_expected_messages_internal('Gjs', 'testGObjectDestructionAccess.js', 0,
            'testExceptionInDestroyedObjectEmit');
    });

    it('Proto function signals_disconnect', function () {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_CRITICAL,
            'Object Gtk.Window (0x*');

        expect(GObject.signal_handlers_disconnect_by_func(destroyedWindow, () => {})).toBe(0);

        GLib.test_assert_expected_messages_internal('Gjs', 'testGObjectDestructionAccess.js', 0,
            'testExceptionInDestroyedObjectSignalsDisconnect');
    });

    it('Proto function signals_block', function () {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_CRITICAL,
            'Object Gtk.Window (0x*');

        expect(GObject.signal_handlers_block_by_func(destroyedWindow, () => {})).toBe(0);

        GLib.test_assert_expected_messages_internal('Gjs', 'testGObjectDestructionAccess.js', 0,
            'testExceptionInDestroyedObjectSignalsBlock');
    });

    it('Proto function signals_unblock', function () {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_CRITICAL,
            'Object Gtk.Window (0x*');

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

describe('Disposed or finalized GObject', function () {
    beforeAll(function () {
        GjsTestTools.init();
    });

    afterEach(function () {
        GjsTestTools.reset();
    });

    it('is marked as disposed when it is a manually disposed property', function () {
        const emblem = new Gio.EmblemedIcon({
            gicon: new Gio.ThemedIcon({ name: 'alarm' }),
        });

        let { gicon } = emblem;
        gicon.run_dispose();
        gicon = null;
        System.gc();

        expect(emblem.gicon.toString()).toMatch(
            /\[object \(DISPOSED\) instance wrapper .* jsobj@0x[a-f0-9]+ native@0x[a-f0-9]+\]/);
    });

    it('generates a warn on object garbage collection', function () {
        Gio.File.new_for_path('/').unref();

        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_CRITICAL,
            '*Object 0x* has been finalized *');
        System.gc();
        GLib.test_assert_expected_messages_internal('Gjs', 'testGObjectDestructionAccess.js', 0,
            'generates a warn on object garbage collection');
    });

    it('generates a warn on object garbage collection if has expando property', function () {
        let file = Gio.File.new_for_path('/');
        file.toggleReferenced = true;
        file.unref();
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

    it('created from other function is marked as disposed', function () {
        let file = Gio.File.new_for_path('/');
        GjsTestTools.save_object(file);
        file.run_dispose();
        file = null;
        System.gc();

        expect(GjsTestTools.get_saved()).toMatch(
            /\[object \(DISPOSED\) instance wrapper GType:GLocalFile jsobj@0x[a-f0-9]+ native@0x[a-f0-9]+\]/);
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
});
