// -*- mode: js; indent-tabs-mode: nil -*-
imports.gi.versions.Gtk = '3.0';

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
        expect(() => {
            let title = destroyedWindow.title;
        }).toThrowError(/Object Gtk.Window \(0x[a-f0-9]+\), has been already finalized. Impossible to get any property from it./)
    });

    it('Set property', () => {
        expect(() => {
            destroyedWindow.title = 'I am dead';
        }).toThrowError(/Object Gtk.Window \(0x[a-f0-9]+\), has been already finalized. Impossible to set any property to it./)
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

    it('Proto function connect', () => {
        expect(() => {
            destroyedWindow.connect('foo-signal', () => {});
        }).toThrowError(/Object Gtk.Window \(0x[a-f0-9]+\), has been already deallocated - impossible to connect to signal\..*/)
    });

    it('Proto function connect_after', () => {
        expect(() => {
            destroyedWindow.connect_after('foo-signal', () => {});
        }).toThrowError(/Object Gtk.Window \(0x[a-f0-9]+\), has been already deallocated - impossible to connect to signal\..*/)
    });

    it('Proto function emit', () => {
        expect(() => {
            destroyedWindow.emit('foo-signal');
        }).toThrowError(/Object Gtk.Window \(0x[a-f0-9]+\), has been already deallocated - impossible to emit signal\..*/)
    });
});
