// -*- mode: js; indent-tabs-mode: nil -*-
imports.gi.versions.Gtk = '3.0';

const GLib = imports.gi.GLib;
const Gtk = imports.gi.Gtk;

describe('Access to destroyed GObject', () => {
    let destroyedWindow;

    beforeAll(() => {
        Gtk.init(null);
    });

    beforeEach(() => {
        destroyedWindow = new Gtk.Window({type: Gtk.WindowType.TOPLEVEL});
        destroyedWindow.destroy();
    });

    it('Get property', () => {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_CRITICAL,
            'Object Gtk.Window (0x*');

        let title = destroyedWindow.title;

        GLib.test_assert_expected_messages_internal('Gjs', 'testGObjectDestructionAccess.js', 0,
            'testExceptionInDestroyedObjectPropertyGet');
    });

    it('Set property', () => {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_CRITICAL,
            'Object Gtk.Window (0x*');

        destroyedWindow.title = 'I am dead';

        GLib.test_assert_expected_messages_internal('Gjs', 'testGObjectDestructionAccess.js', 0,
            'testExceptionInDestroyedObjectPropertySet');
    });

    it('Access to getter method', () => {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_CRITICAL,
            'Object Gtk.Window (0x*');

        let title = destroyedWindow.get_title();

        GLib.test_assert_expected_messages_internal('Gjs', 'testGObjectDestructionAccess.js', 0,
            'testExceptionInDestroyedObjectMethodGet');
    });

    it('Access to setter method', () => {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_CRITICAL,
            'Object Gtk.Window (0x*');

        destroyedWindow.set_title('I am dead');

        GLib.test_assert_expected_messages_internal('Gjs', 'testGObjectDestructionAccess.js', 0,
            'testExceptionInDestroyedObjectMethodSet');
    });

    it('Proto function connect', () => {
        expect(() => {
            destroyedWindow.connect('foo-signal', () => {});
        }).toThrowError(/Object Gtk.Window \(0x[a-f0-9]+\), has been already deallocated - impossible to connect to signal\./)
    });

    it('Proto function connect_after', () => {
        expect(() => {
            destroyedWindow.connect_after('foo-signal', () => {});
        }).toThrowError(/Object Gtk.Window \(0x[a-f0-9]+\), has been already deallocated - impossible to connect to signal\./)
    });

    it('Proto function emit', () => {
        expect(() => {
            destroyedWindow.emit('foo-signal');
        }).toThrowError(/Object Gtk.Window \(0x[a-f0-9]+\), has been already deallocated - impossible to emit signal\./)
    });
});
