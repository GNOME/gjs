// -*- mode: js; indent-tabs-mode: nil -*-
imports.gi.versions.Gtk = '3.0';

const Gtk = imports.gi.Gtk;
const GLib = imports.gi.GLib;

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
            'Object Gtk.Window*');

        let title = destroyedWindow.title;

        GLib.test_assert_expected_messages_internal('Gjs', 'testGObjectDestructionAccess.js', 0,
            'testExceptionInDestroyedObjectPropertyGet');
    });

    it('Set property', () => {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_CRITICAL,
            'Object Gtk.Window*');

        destroyedWindow.title = 'I am dead';

        GLib.test_assert_expected_messages_internal('Gjs', 'testGObjectDestructionAccess.js', 0,
            'testExceptionInDestroyedObjectPropertySet');
    });

    it('Access to getter method', () => {
        expect(() => {
            let title = destroyedWindow.get_title();
        }).toThrowError(/Object Gtk.Window \(0x[a-f0-9]+\), has been already deallocated.*/)
    });

    it('Access to setter method', () => {
        expect(() => {
            destroyedWindow.set_title('I am dead');
        }).toThrowError(/Object Gtk.Window \(0x[a-f0-9]+\), has been already deallocated.*/)
    });
});
