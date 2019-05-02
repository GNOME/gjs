const {GLib, Gio, GObject} = imports.gi;

const Foo = GObject.registerClass({
    Properties: {
        boolval: GObject.ParamSpec.boolean('boolval', '', '',
            GObject.ParamFlags.READWRITE, false),
    }
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
        for (let i = 0; i < 100; i++) {
            list.append(new Foo(i));
        }
    });

    it('ListStore iterates', function () {
        let i = 0;
        for (let f of list) {
            expect(f.value).toBe(i++);
        }
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

            expect(settings.get_value('window-size').deep_unpack()).toEqual([100, 100]);
            expect(settings.get_boolean('maximized')).toEqual(true);
            expect(settings.get_boolean('fullscreen')).toEqual(true);

            expect(() => {
                KEYS.forEach(key => settings.reset(key));
            }).not.toThrow();

            KEYS.forEach(key => expect(settings.get_user_value(key)).toBeNull());
            expect(settings.get_default_value('window-size').deep_unpack()).toEqual([-1, -1]);
            expect(settings.get_default_value('maximized').deep_unpack()).toEqual(false);
            expect(settings.get_default_value('fullscreen').deep_unpack()).toEqual(false);

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
