const {GLib, Gio, GObject} = imports.gi;

const Foo = GObject.registerClass(class Foo extends GObject.Object {
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
    it("throws but doesn't crash when forgetting to specify a schema ID", function () {
        expect(() => new Gio.Settings()).toThrowError(/schema/);
    });

    it("throws but doesn't crash when specifying a schema ID that isn't installed", function () {
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

        it("throws but doesn't crash when resetting a key that doesn't belong to the schema", function () {
            expect(() => settings.reset('foobar')).toThrowError(/key/);
        });

        it("throws but doesn't crash when checking a key that doesn't belong to the schema", function () {
            KINDS.forEach(kind => {
                expect(() => settings[`get_${kind}`]('foobar')).toThrowError(/key/);
            });
        });

        it("throws but doesn't crash when setting a key that doesn't belong to the schema", function () {
            KINDS.forEach(kind => {
                expect(() => settings[`set_${kind}`]('foobar', null)).toThrowError(/key/);
            });
        });

        it('still works with correct keys', function () {
            expect(() => {
                settings.set_value('window-size', new GLib.Variant('(ii)', [100, 100]));
                settings.set_boolean('maximized', true);
                settings.set_boolean('fullscreen', true);
            }).not.toThrow();

            expect(settings.get_value('window-size').deep_unpack()).toEqual([100, 100]);
            expect(settings.get_boolean('maximized')).toEqual(true);
            expect(settings.get_boolean('fullscreen')).toEqual(true);

            expect(() => {
                settings.reset('window-size');
                settings.reset('maximized');
                settings.reset('fullscreen');
            }).not.toThrow();
        });
    });
});
