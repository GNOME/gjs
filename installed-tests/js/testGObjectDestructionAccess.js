// -*- mode: js; indent-tabs-mode: nil -*-
imports.gi.versions.Gtk = '3.0';

const GLib = imports.gi.GLib;
const Gtk = imports.gi.Gtk;

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

        void destroyedWindow.title;

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

    it('Access to getter method', function () {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_CRITICAL,
            'Object Gtk.Window (0x*');
        GLib.test_expect_message('Gtk', GLib.LogLevelFlags.LEVEL_CRITICAL,
            '*GTK_IS_WINDOW*');

        void destroyedWindow.get_title();

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

        destroyedWindow.connect('foo-signal', () => {});

        GLib.test_assert_expected_messages_internal('Gjs', 'testGObjectDestructionAccess.js', 0,
            'testExceptionInDestroyedObjectConnect');
    });

    it('Proto function connect_after', function () {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_CRITICAL,
            'Object Gtk.Window (0x*');

        destroyedWindow.connect_after('foo-signal', () => {});

        GLib.test_assert_expected_messages_internal('Gjs', 'testGObjectDestructionAccess.js', 0,
            'testExceptionInDestroyedObjectConnectAfter');
    });

    it('Proto function emit', function () {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_CRITICAL,
            'Object Gtk.Window (0x*');

        destroyedWindow.emit('foo-signal');

        GLib.test_assert_expected_messages_internal('Gjs', 'testGObjectDestructionAccess.js', 0,
            'testExceptionInDestroyedObjectEmit');
    });

    it('Proto function toString', function () {
        expect(destroyedWindow.toString()).toMatch(
            /\[object \(FINALIZED\) instance wrapper GIName:Gtk.Window jsobj@0x[a-f0-9]+ native@0x[a-f0-9]+\]/);
    });

    it('Proto function toString before/after', function () {
        var validWindow = new Gtk.Window({type: Gtk.WindowType.TOPLEVEL});

        expect(validWindow.toString()).toMatch(
            /\[object instance wrapper GIName:Gtk.Window jsobj@0x[a-f0-9]+ native@0x[a-f0-9]+\]/);

        validWindow.destroy();

        expect(validWindow.toString()).toMatch(
            /\[object \(FINALIZED\) instance wrapper GIName:Gtk.Window jsobj@0x[a-f0-9]+ native@0x[a-f0-9]+\]/);
    });
});
