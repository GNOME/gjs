const Gio = imports.gi.Gio;
const GObject = imports.gi.GObject;

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