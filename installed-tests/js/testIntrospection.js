// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008, 2018 Red Hat, Inc.
// SPDX-FileCopyrightText: 2017 Philip Chimento <philip.chimento@gmail.com>
// SPDX-FileCopyrightText: 2020 Ole Jørgen Brønner <olejorgenb@yahoo.no>

// Various tests having to do with how introspection is implemented in GJS

imports.gi.versions.Gdk = '3.0';
imports.gi.versions.Gtk = '3.0';
const {Gdk, Gio, GLib, GObject, Gtk} = imports.gi;
const System = imports.system;

describe('GLib.DestroyNotify parameter', function () {
    it('throws when encountering a GDestroyNotify not associated with a callback', function () {
        // should throw when called, not when the function object is created
        expect(() => Gio.MemoryInputStream.new_from_data).not.toThrow();
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
        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_WARNING,
            '*cannot be safely stored*');
        void GLib.MININT64;
        void GLib.MAXINT64;
        void GLib.MAXUINT64;
        GLib.test_assert_expected_messages_internal('Gjs',
            'testEverythingBasic.js', 0,
            'Limits warns when conversion is lossy');
    });
});

describe('Marshalling empty flat arrays of structs', function () {
    let widget;
    let gtkEnabled;
    beforeAll(function () {
        gtkEnabled = GLib.getenv('ENABLE_GTK') === 'yes';
        if (!gtkEnabled)
            return;
        Gtk.init(null);
    });

    beforeEach(function () {
        if (!gtkEnabled) {
            pending('GTK disabled');
            return;
        }
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
    let gtkEnabled;
    beforeAll(function () {
        gtkEnabled = GLib.getenv('ENABLE_GTK') === 'yes';
        if (!gtkEnabled)
            return;
        Gtk.init(null);
    });

    beforeEach(function () {
        if (!gtkEnabled) {
            pending('GTK disabled');
            return;
        }
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

    // This tests a race condition that would crash; it should warn instead
    it('handles setting a property from C on an object whose JS wrapper has been collected', function (done) {
        class SomeObject extends GObject.Object {
            static [GObject.properties] = {
                'screenfull': GObject.ParamSpec.boolean('screenfull', '', '',
                    GObject.ParamFlags.READWRITE,
                    false),
            };

            static {
                GObject.registerClass(this);
            }
        }

        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_WARNING,
            '*property screenfull*');

        const settings = new Gio.Settings({schemaId: 'org.gnome.GjsTest'});
        let obj = new SomeObject();
        settings.bind('fullscreen', obj, 'screenfull', Gio.SettingsBindFlags.DEFAULT);
        const handler = settings.connect('changed::fullscreen', () => {
            obj.run_dispose();
            obj = null;
            settings.disconnect(handler);
            GLib.idle_add(GLib.PRIORITY_LOW, () => {
                GLib.test_assert_expected_messages_internal('Gjs',
                    'testIntrospection.js', 0,
                    'Warn about setting property on disposed JS object');
                done();
            });
        });
        settings.set_boolean('fullscreen', !settings.get_boolean('fullscreen'));
        settings.reset('fullscreen');
    });
});

describe('Gdk.Atom', function () {
    it('is presented as string', function () {
        expect(Gdk.Atom.intern('CLIPBOARD', false)).toBe('CLIPBOARD');
        expect(Gdk.Atom.intern('NONE', false)).toBe(null);
    });
});

describe('Complete enumeration (boxed types)', function () {
    it('enumerates all properties', function () {
        // Note: this test breaks down if other code access all the methods of Rectangle
        const rect = new Gdk.Rectangle();
        const names = Object.getOwnPropertyNames(Object.getPrototypeOf(rect));
        const expectAtLeast = ['equal', 'intersect', 'union', 'x', 'y', 'width', 'height'];
        expect(names).toEqual(jasmine.arrayContaining(expectAtLeast));
    });
});

describe('Complete enumeration of GIRepositoryNamespace (new_enumerate)', function () {
    it('enumerates all properties (sampled)', function () {
        const names = Object.getOwnPropertyNames(Gdk);
        // Note: properties which has been accessed are listed without new_enumerate hook
        const expectAtLeast = ['KEY_ybelowdot', 'EventSequence', 'ByteOrder', 'Window'];
        expect(names).toEqual(jasmine.arrayContaining(expectAtLeast));
    });

    it('all enumerated properties are defined', function () {
        const names = Object.keys(Gdk);
        expect(() => {
            // Access each enumerated property to check it can be defined.
            names.forEach(name => Gdk[name]);
        }).not.toThrowError(/API of type .* not implemented, cannot define .*/);
    });
});

describe('Backwards compatibility for GLib/Gio platform specific GIRs', function () {
    // Only test this if GioUnix is available
    const skip = imports.gi.versions.GioUnix !== '2.0';

    it('GioUnix objects are looked up in GioUnix, not Gio', function () {
        if (skip) {
            pending('GioUnix required for this test');
            return;
        }

        GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_WARNING,
            '*Gio.UnixMountMonitor*');

        const monitor = Gio.UnixMountMonitor.get();
        expect(monitor.toString()).toContain('GIName:GioUnix.MountMonitor');

        GLib.test_assert_expected_messages_internal('Gjs',
            'testIntrospection.js', 0,
            'Expected deprecation message for Gio.Unix -> GioUnix');
    });

    it("doesn't print the message if the type isn't resolved directly", function () {
        if (skip) {
            pending('GioUnix required for this test');
            return;
        }

        const launcher = new Gio.SubprocessLauncher({flags: Gio.SubprocessFlags.STDOUT_PIPE});
        const proc = launcher.spawnv(['ls', '/dev/null']);

        expect(proc.get_stdout_pipe().toString()).toContain('GIName:GioUnix.InputStream');
    });

    it('has some exceptions', function () {
        expect(Gio.UnixConnection.toString()).toContain('Gio_UnixConnection');

        const credentialsMessage = new Gio.UnixCredentialsMessage();
        expect(credentialsMessage.toString()).toContain('GIName:Gio.UnixCredentials');

        const fdList = new Gio.UnixFDList();
        expect(fdList.toString()).toContain('GIName:Gio.UnixFDList');

        const socketAddress = Gio.UnixSocketAddress.new_with_type('', Gio.UnixSocketAddressType.ANONYMOUS);
        expect(socketAddress.toString()).toContain('GIName:Gio.UnixSocketAddress');
    });
});
