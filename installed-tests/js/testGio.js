// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2017 Patrick Griffis <tingping@tingping.se>
// SPDX-FileCopyrightText: 2019 Philip Chimento <philip.chimento@gmail.com>

const {GLib, Gio, GObject} = imports.gi;

const Foo = GObject.registerClass({
    Properties: {
        boolval: GObject.ParamSpec.boolean('boolval', '', '',
            GObject.ParamFlags.READWRITE, false),
    },
}, class Foo extends GObject.Object {
    _init(value) {
        super._init();
        this.value = value;
    }
});

describe('ListStore iterator', function () {
    let list;

    beforeEach(function () {
        list = new Gio.ListStore({item_type: Foo});
        for (let i = 0; i < 100; i++)
            list.append(new Foo(i));
    });

    it('ListStore iterates', function () {
        let i = 0;
        for (let f of list)
            expect(f.value).toBe(i++);
    });
});

function compareFunc(a, b) {
    return a.value - b.value;
}

describe('Sorting in ListStore', function () {
    let list;

    beforeEach(function () {
        list = new Gio.ListStore({
            item_type: Foo,
        });
    });

    it('test insert_sorted', function () {
        for (let i = 10; i > 0; i--)
            list.insert_sorted(new Foo(i), compareFunc);
        let i = 1;
        for (let f of list)
            expect(f.value).toBe(i++);
    });

    it('test sort', function () {
        for (let i = 10; i > 0; i--)
            list.append(new Foo(i));
        list.sort(compareFunc);
        let i = 1;
        for (let f of list)
            expect(f.value).toBe(i++);
    });
});

describe('Promisify function', function () {
    it("doesn't crash when async function is not defined", function () {
        expect(() => Gio._promisify(Gio.Subprocess.prototype, 'commuicate_utf8_async', 'communicate_utf8_finish')).toThrowError(/commuicate_utf8_async/);
    });

    it("doesn't crash when finish function is not defined", function () {
        expect(() => Gio._promisify(Gio.Subprocess.prototype, 'communicate_utf8_async', 'commuicate_utf8_finish')).toThrowError(/commuicate_utf8_finish/);
    });

    it('promisifies functions', async function () {
        Gio._promisify(Gio.File.prototype, 'query_info_async');
        const file = Gio.File.new_for_path('.');

        const fileInfo = await file.query_info_async(Gio.FILE_ATTRIBUTE_STANDARD_TYPE,
            Gio.FileQueryInfoFlags.NONE, GLib.PRIORITY_DEFAULT, null);
        expect(fileInfo.get_file_type()).not.toBe(Gio.FileType.UNKNOWN);
    });

    it('preserves old behavior', function (done) {
        Gio._promisify(Gio.File.prototype, 'query_info_async');
        const file = Gio.File.new_for_path('.');

        file.query_info_async(Gio.FILE_ATTRIBUTE_STANDARD_TYPE,
            Gio.FileQueryInfoFlags.NONE, GLib.PRIORITY_DEFAULT, null, (_, res) => {
                const fileInfo = file.query_info_finish(res);
                expect(fileInfo.get_file_type()).not.toBe(Gio.FileType.UNKNOWN);
                done();
            });
    });

    it('can guess the finish function', function () {
        expect(() => Gio._promisify(Gio._LocalFilePrototype, 'read_async')).not.toThrow();
        expect(() => Gio._promisify(Gio.DBus, 'get')).not.toThrow();
    });
});

describe('Gio.Settings overrides', function () {
    it("doesn't crash when forgetting to specify a schema ID", function () {
        expect(() => new Gio.Settings()).toThrowError(/schema/);
    });

    it("doesn't crash when specifying a schema ID that isn't installed", function () {
        expect(() => new Gio.Settings({schema: 'com.example.ThisDoesntExist'}))
            .toThrowError(/schema/);
    });

    it("doesn't crash when forgetting to specify a schema path", function () {
        expect(() => new Gio.Settings({schema: 'org.gnome.GjsTest.Sub'}))
            .toThrowError(/schema/);
    });

    it("doesn't crash when specifying conflicting schema paths", function () {
        expect(() => new Gio.Settings({
            schema: 'org.gnome.GjsTest',
            path: '/conflicting/path/',
        })).toThrowError(/schema/);
    });

    describe('with existing schema', function () {
        const KINDS = ['boolean', 'double', 'enum', 'flags', 'int', 'int64',
            'string', 'strv', 'uint', 'uint64', 'value'];
        let settings;

        beforeEach(function () {
            settings = new Gio.Settings({schema: 'org.gnome.GjsTest'});
        });

        it("doesn't crash when resetting a nonexistent key", function () {
            expect(() => settings.reset('foobar')).toThrowError(/key/);
        });

        it("doesn't crash when checking a nonexistent key", function () {
            KINDS.forEach(kind => {
                expect(() => settings[`get_${kind}`]('foobar')).toThrowError(/key/);
            });
        });

        it("doesn't crash when setting a nonexistent key", function () {
            KINDS.forEach(kind => {
                expect(() => settings[`set_${kind}`]('foobar', null)).toThrowError(/key/);
            });
        });

        it("doesn't crash when checking writable for a nonexistent key", function () {
            expect(() => settings.is_writable('foobar')).toThrowError(/key/);
        });

        it("doesn't crash when getting the user value for a nonexistent key", function () {
            expect(() => settings.get_user_value('foobar')).toThrowError(/key/);
        });

        it("doesn't crash when getting the default value for a nonexistent key", function () {
            expect(() => settings.get_default_value('foobar')).toThrowError(/key/);
        });

        it("doesn't crash when binding a nonexistent key", function () {
            const foo = new Foo();
            expect(() => settings.bind('foobar', foo, 'boolval', Gio.SettingsBindFlags.GET))
                .toThrowError(/key/);
            expect(() => settings.bind_writable('foobar', foo, 'boolval', false))
                .toThrowError(/key/);
        });

        it("doesn't crash when creating actions for a nonexistent key", function () {
            expect(() => settings.create_action('foobar')).toThrowError(/key/);
        });

        it("doesn't crash when checking info about a nonexistent key", function () {
            expect(() => settings.settings_schema.get_key('foobar')).toThrowError(/key/);
        });

        it("doesn't crash when getting a nonexistent sub-schema", function () {
            expect(() => settings.get_child('foobar')).toThrowError(/foobar/);
        });

        it('still works with correct keys', function () {
            const KEYS = ['window-size', 'maximized', 'fullscreen'];

            KEYS.forEach(key => expect(settings.is_writable(key)).toBeTruthy());

            expect(() => {
                settings.set_value('window-size', new GLib.Variant('(ii)', [100, 100]));
                settings.set_boolean('maximized', true);
                settings.set_boolean('fullscreen', true);
            }).not.toThrow();

            expect(settings.get_value('window-size').deepUnpack()).toEqual([100, 100]);
            expect(settings.get_boolean('maximized')).toEqual(true);
            expect(settings.get_boolean('fullscreen')).toEqual(true);

            expect(() => {
                KEYS.forEach(key => settings.reset(key));
            }).not.toThrow();

            KEYS.forEach(key => expect(settings.get_user_value(key)).toBeNull());
            expect(settings.get_default_value('window-size').deepUnpack()).toEqual([-1, -1]);
            expect(settings.get_default_value('maximized').deepUnpack()).toEqual(false);
            expect(settings.get_default_value('fullscreen').deepUnpack()).toEqual(false);

            const foo = new Foo({boolval: true});
            settings.bind('maximized', foo, 'boolval', Gio.SettingsBindFlags.GET);
            expect(foo.boolval).toBeFalsy();
            Gio.Settings.unbind(foo, 'boolval');
            settings.bind_writable('maximized', foo, 'boolval', false);
            expect(foo.boolval).toBeTruthy();

            expect(settings.create_action('maximized')).not.toBeNull();

            expect(settings.settings_schema.get_key('fullscreen')).not.toBeNull();

            const sub = settings.get_child('sub');
            expect(sub.get_uint('marine')).toEqual(10);
        });
    });
});

describe('Gio.add_action_entries override', function () {
    it('registers each entry as an action', function ()  {
        const app = new Gio.Application();

        const entries = [
            {
                name: 'foo',
                parameter_type: 's',
            },
            {
                name: 'bar',
                state: 'false',
            },
        ];

        app.add_action_entries(entries);

        expect(app.lookup_action('foo').name).toEqual(entries[0].name);
        expect(app.lookup_action('foo').parameter_type.dup_string()).toEqual(entries[0].parameter_type);

        expect(app.lookup_action('bar').name).toEqual(entries[1].name);
        expect(app.lookup_action('bar').state.print(true)).toEqual(entries[1].state);
    });

    it('connects and binds the activate handler', function (done) {
        const app = new Gio.Application();
        let action;

        const entries = [
            {
                name: 'foo',
                parameter_type: 's',
                activate() {
                    expect(this).toBe(action);
                    done();
                },
            },
        ];

        app.add_action_entries(entries);
        action = app.lookup_action('foo');

        action.activate(new GLib.Variant('s', 'hello'));
    });

    it('connects and binds the change_state handler', function (done) {
        const app = new Gio.Application();
        let action;

        const entries = [
            {
                name: 'bar',
                state: 'false',
                change_state() {
                    expect(this).toBe(action);
                    done();
                },
            },
        ];

        app.add_action_entries(entries);
        action = app.lookup_action('bar');

        action.change_state(new GLib.Variant('b', 'true'));
    });

    it('throw an error if the parameter_type is invalid', function () {
        const app = new Gio.Application();

        const entries = [
            {
                name: 'foo',
                parameter_type: '(((',
            },
        ];

        expect(() => app.add_action_entries(entries)).toThrow();
    });

    it('throw an error if the state is invalid', function () {
        const app = new Gio.Application();

        const entries = [
            {
                name: 'bar',
                state: 'foo',
            },
        ];

        expect(() => app.add_action_entries(entries)).toThrow();
    });
});

describe('Gio.FileEnumerator overrides', function () {
    it('iterates synchronously', function () {
        const dir = Gio.File.new_for_path('.');
        let count = 0;
        for (const value of dir.enumerate_children(
            'standard::name',
            Gio.FileQueryInfoFlags.NOFOLLOW_SYMLINKS,
            null
        )) {
            expect(value).toBeInstanceOf(Gio.FileInfo);
            count++;
        }
        expect(count).toBeGreaterThan(0);
    });

    it('iterates asynchronously', async function () {
        const dir = Gio.File.new_for_path('.');
        let count = 0;
        for await (const value of dir.enumerate_children(
            'standard::name',
            Gio.FileQueryInfoFlags.NOFOLLOW_SYMLINKS,
            null
        )) {
            expect(value).toBeInstanceOf(Gio.FileInfo);
            count++;
        }
        expect(count).toBeGreaterThan(0);
    });
});

describe('Non-introspectable file attribute overrides', function () {
    let numExpectedWarnings, file, info;
    const flags = [Gio.FileQueryInfoFlags.NONE, null];

    function expectWarnings(count) {
        numExpectedWarnings = count;
        for (let c = 0; c < count; c++) {
            GLib.test_expect_message('Gjs', GLib.LogLevelFlags.LEVEL_WARNING,
                '*not introspectable*');
        }
    }

    function assertWarnings(testName) {
        for (let c = 0; c < numExpectedWarnings; c++) {
            GLib.test_assert_expected_messages_internal('Gjs', 'testGio.js', 0,
                `test Gio.${testName}`);
        }
        numExpectedWarnings = 0;
    }

    beforeEach(function () {
        numExpectedWarnings = 0;
        [file] = Gio.File.new_tmp('XXXXXX');
        info = file.query_info('standard::*', ...flags);
    });

    it('invalid means unsetting the attribute', function () {
        expectWarnings(2);
        expect(() =>
            file.set_attribute('custom::remove', Gio.FileAttributeType.INVALID, null, ...flags))
            .toThrowError(/not introspectable/);
        expect(() => info.set_attribute('custom::remove', Gio.FileAttributeType.INVALID)).not.toThrow();
        assertWarnings();
    });

    it('works for boolean', function () {
        expectWarnings(2);
        expect(() =>
            file.set_attribute(Gio.FILE_ATTRIBUTE_STANDARD_IS_HIDDEN, Gio.FileAttributeType.BOOLEAN, false, ...flags))
            .toThrowError(/not introspectable/);
        expect(() => info.set_attribute(Gio.FILE_ATTRIBUTE_STANDARD_IS_HIDDEN, Gio.FileAttributeType.BOOLEAN, false))
            .not.toThrow();
        assertWarnings();
    });

    it('works for uint32', function () {
        expectWarnings(2);
        expect(() => file.set_attribute(Gio.FILE_ATTRIBUTE_TIME_MODIFIED_USEC, Gio.FileAttributeType.UINT32, 123456, ...flags))
            .not.toThrow();
        expect(() => info.set_attribute(Gio.FILE_ATTRIBUTE_TIME_MODIFIED_USEC, Gio.FileAttributeType.UINT32, 654321))
            .not.toThrow();
        assertWarnings();
    });

    it('works for uint64', function () {
        expectWarnings(2);
        expect(() => file.set_attribute(Gio.FILE_ATTRIBUTE_TIME_MODIFIED, Gio.FileAttributeType.UINT64, Date.now() / 1000, ...flags))
            .not.toThrow();
        expect(() => info.set_attribute(Gio.FILE_ATTRIBUTE_TIME_MODIFIED, Gio.FileAttributeType.UINT64, Date.now() / 1000))
            .not.toThrow();
        assertWarnings();
    });

    it('works for object', function () {
        expectWarnings(2);
        const icon = Gio.ThemedIcon.new_from_names(['list-add-symbolic']);
        expect(() =>
            file.set_attribute(Gio.FILE_ATTRIBUTE_STANDARD_ICON, Gio.FileAttributeType.OBJECT, icon, ...flags))
            .toThrowError(/not introspectable/);
        expect(() => info.set_attribute(Gio.FILE_ATTRIBUTE_STANDARD_ICON, Gio.FileAttributeType.OBJECT, icon))
            .not.toThrow();
        assertWarnings();
    });

    afterEach(function () {
        file.delete_async(GLib.PRIORITY_DEFAULT, null, (obj, res) => obj.delete_finish(res));
    });
});
