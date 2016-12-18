// -*- mode: js; indent-tabs-mode: nil -*-

const Lang = imports.lang;

const MagicBase = new Lang.Class({
    Name: 'MagicBase',

    _init: function(a, buffer) {
        if (buffer) buffer.push(a);
        this.a = a;
    },

    foo: function(a, buffer) {
        buffer.push(a);
        return a * 3;
    },

    bar: function(a) {
        return a * 5;
    }
});

const Magic = new Lang.Class({
    Name: 'Magic',

    Extends: MagicBase,

    _init: function(a, b, buffer) {
        this.parent(a, buffer);
        if (buffer) buffer.push(b);
        this.b = b;
    },

    foo: function(a, b, buffer) {
        let val = this.parent(a, buffer);
        buffer.push(b);
        return val * 2;
    },

    bar: function(a, buffer) {
        this.foo(a, 2*a, buffer);
        return this.parent(a);
    }
});

const Accessor = new Lang.Class({
    Name: 'AccessorMagic',

    _init: function(val) {
        this._val = val;
    },

    get value() {
        return this._val;
    },

    set value(val) {
        if (val != 42)
            throw TypeError('Value is not a magic number');
        this._val = val;
    }
});

const AbstractBase = new Lang.Class({
    Name: 'AbstractBase',
    Abstract: true,

    _init: function() {
        this.foo = 42;
    }
});

describe('Class framework', function () {
    it('calls _init constructors', function () {
        let newMagic = new MagicBase('A');
        expect(newMagic.a).toEqual('A');
    });

    it('calls parent constructors', function () {
        let buffer = [];

        let newMagic = new Magic('a', 'b', buffer);
        expect(buffer).toEqual(['a', 'b']);

        buffer = [];
        let val = newMagic.foo(10, 20, buffer);
        expect(buffer).toEqual([10, 20]);
        expect(val).toEqual(10 * 6);
    });

    it('sets the right constructor properties', function () {
        expect(Magic.prototype.constructor).toBe(Magic);

        let newMagic = new Magic();
        expect(newMagic.constructor).toBe(Magic);
    });

    it('sets up instanceof correctly', function () {
        let newMagic = new Magic();

        expect(newMagic instanceof Magic).toBeTruthy();
        expect(newMagic instanceof MagicBase).toBeTruthy();
    });

    it('reports a sensible value for toString()', function () {
        let newMagic = new MagicBase();
        expect(newMagic.toString()).toEqual('[object MagicBase]');
    });

    it('allows overriding toString()', function () {
        const ToStringOverride = new Lang.Class({
            Name: 'ToStringOverride',

            toString: function() {
                let oldToString = this.parent();
                return oldToString + '; hello';
            }
        });

        let override = new ToStringOverride();
        expect(override.toString()).toEqual('[object ToStringOverride]; hello');
    });

    it('is not configurable', function () {
        let newMagic = new MagicBase();

        delete newMagic.foo;
        expect(newMagic.foo).toBeDefined();
    });

    it('allows accessors for properties', function () {
        let newAccessor = new Accessor(11);

        expect(newAccessor.value).toEqual(11);
        expect(() => newAccessor.value = 12).toThrow();

        newAccessor.value = 42;
        expect(newAccessor.value).toEqual(42);
    });

    it('raises an exception when creating an abstract class', function () {
        expect(() => new AbstractBase()).toThrow();
    });

    it('inherits properties from abstract base classes', function () {
        const AbstractImpl = new Lang.Class({
            Name: 'AbstractImpl',
            Extends: AbstractBase,

            _init: function() {
                this.parent();
                this.bar = 42;
            }
        });

        let newAbstract = new AbstractImpl();
        expect(newAbstract.foo).toEqual(42);
        expect(newAbstract.bar).toEqual(42);
    });

    it('inherits constructors from abstract base classes', function () {
        const AbstractImpl = new Lang.Class({
            Name: 'AbstractImpl',
            Extends: AbstractBase,
        });

        let newAbstract = new AbstractImpl();
        expect(newAbstract.foo).toEqual(42);
    });

    it('lets methods call other methods without clobbering __caller__', function () {
        let newMagic = new Magic();
        let buffer = [];

        let res = newMagic.bar(10, buffer);
        expect(buffer).toEqual([10, 20]);
        expect(res).toEqual(50);
    });

    it('allows custom return values from constructors', function () {
        const CustomConstruct = new Lang.Class({
            Name: 'CustomConstruct',

            _construct: function(one, two) {
                return [one, two];
            }
        });

        let instance = new CustomConstruct(1, 2);

        expect(instance instanceof Array).toBeTruthy();
        expect(instance instanceof CustomConstruct).toBeFalsy();
        expect(instance).toEqual([1, 2]);
    });
});
