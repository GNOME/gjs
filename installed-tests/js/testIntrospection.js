// Various tests having to do with how introspection is implemented in GJS

imports.gi.versions.Gdk = '3.0';
imports.gi.versions.Gtk = '3.0';
const {Gdk, Gio, GLib, GObject, Gtk} = imports.gi;
const System = imports.system;

describe('GLib.DestroyNotify parameter', function () {
    it('throws when encountering a GDestroyNotify not associated with a callback', function () {
        // the 'destroy' argument applies to the data, which is not supported in
        // gobject-introspection
        expect(() => Gio.MemoryInputStream.new_from_data('foobar'))
            .toThrowError(/destroy/);
    });
});

describe('Unsafe integer marshalling', function () {
    it('warns when conversion is lossy', function () {
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_WARNING,
            '*cannot be safely stored*');
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_WARNING,
            '*cannot be safely stored*');
        void GLib.MAXINT64;
        void GLib.MAXUINT64;
        GLib.test_assert_expected_messages_internal('Gjs',
            'testEverythingBasic.js', 0,
            'Limits warns when conversion is lossy');
    });
});

describe('Marshalling empty flat arrays of structs', function () {
    let widget;
    beforeAll(function () {
        if (GLib.getenv('ENABLE_GTK') !== 'yes') {
            pending('GTK disabled');
            return;
        }
        Gtk.init(null);
    });

    beforeEach(function () {
        widget = new Gtk.Label();
    });

    it('accepts null', function () {
        widget.drag_dest_set(0, null, Gdk.DragAction.COPY);
    });

    it('accepts an empty array', function () {
        widget.drag_dest_set(0, [], Gdk.DragAction.COPY);
    });
});

describe('Constructor', function () {
    it('throws when constructor called without new', function () {
        expect(() => Gio.AppLaunchContext())
            .toThrowError(/Constructor called as normal method/);
    });
});

describe('Enum classes', function () {
    it('enum has a $gtype property', function () {
        expect(Gio.BusType.$gtype).toBeDefined();
    });

    it('enum $gtype property is enumerable', function () {
        expect('$gtype' in Gio.BusType).toBeTruthy();
    });
});

describe('GError domains', function () {
    it('Number converts error to quark', function () {
        expect(Gio.ResolverError.quark()).toEqual(Number(Gio.ResolverError));
    });
});

describe('Object properties on GtkBuilder-constructed objects', function () {
    let o1;
    beforeAll(function () {
        if (GLib.getenv('ENABLE_GTK') !== 'yes') {
            pending('GTK disabled');
            return;
        }
        Gtk.init(null);
    });

    beforeEach(function () {
        const ui = `
            <interface>
              <object class="GtkButton" id="button">
                <property name="label">Click me</property>
              </object>
            </interface>`;

        let builder = Gtk.Builder.new_from_string(ui, -1);
        o1 = builder.get_object('button');
    });

    it('are found on the GObject itself', function () {
        expect(o1.label).toBe('Click me');
    });

    it('are found on the GObject\'s parents', function () {
        expect(o1.visible).toBeFalsy();
    });

    it('are found on the GObject\'s interfaces', function () {
        expect(o1.action_name).toBeNull();
    });
});

describe('Garbage collection of introspected objects', function () {
    // This tests a regression that would very rarely crash, but
    // when run under valgrind this code would show use-after-free.
    it('collects objects properly with signals connected', function (done) {
        function orphanObject() {
            let obj = new GObject.Object();
            obj.connect('notify', () => {});
        }

        orphanObject();
        System.gc();
        GLib.idle_add(GLib.PRIORITY_LOW, () => done());
    });
});

describe('Gdk.Atom', function () {
    it('is presented as string', function () {
        expect(Gdk.Atom.intern('CLIPBOARD', false)).toBe('CLIPBOARD');
        expect(Gdk.Atom.intern('NONE', false)).toBe(null);
    });
});
