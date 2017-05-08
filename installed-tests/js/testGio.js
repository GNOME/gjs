const Gio = imports.gi.Gio;
const GObject = imports.gi.GObject;
const Lang = imports.lang;

const Foo = new Lang.Class({
    Name: 'Foo',
    Extends: GObject.Object,
    _init: function (value) {
        this.parent();
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