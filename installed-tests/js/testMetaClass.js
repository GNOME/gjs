// -*- mode: js; indent-tabs-mode: nil -*-

const Lang = imports.lang;

const NormalClass = new Lang.Class({
    Name: 'NormalClass',

    _init: function() {
        this.one = 1;
    }
});

let Subclassed = [];
const MetaClass = new Lang.Class({
    Name: 'MetaClass',
    Extends: Lang.Class,

    _init: function(params) {
        Subclassed.push(params.Name);
        this.parent(params);

        if (params.Extended) {
            this.prototype.dynamic_method = this.wrapFunction('dynamic_method', function() {
                return 73;
            });

            this.DYNAMIC_CONSTANT = 2;
        }
    }
});

const CustomMetaOne = new MetaClass({
    Name: 'CustomMetaOne',
    Extends: NormalClass,
    Extended: false,

    _init: function() {
        this.parent();

        this.two = 2;
    }
});

const CustomMetaTwo = new MetaClass({
    Name: 'CustomMetaTwo',
    Extends: NormalClass,
    Extended: true,

    _init: function() {
        this.parent();

        this.two = 2;
    }
});

// This should inherit CustomMeta, even though
// we use Lang.Class
const CustomMetaSubclass = new Lang.Class({
    Name: 'CustomMetaSubclass',
    Extends: CustomMetaOne,
    Extended: true,

    _init: function() {
        this.parent();

        this.three = 3;
    }
});

describe('A metaclass', function () {
    it('has its constructor called each time a class is created with it', function () {
        expect(Subclassed).toEqual(['CustomMetaOne', 'CustomMetaTwo',
            'CustomMetaSubclass']);
    });

    it('is an instance of Lang.Class', function () {
        expect(NormalClass instanceof Lang.Class).toBeTruthy();
        expect(MetaClass instanceof Lang.Class).toBeTruthy();
    });

    it('produces instances that are instances of itself and Lang.Class', function () {
        expect(CustomMetaOne instanceof Lang.Class).toBeTruthy();
        expect(CustomMetaOne instanceof MetaClass).toBeTruthy();
    });

    it('can dynamically define properties in its constructor', function () {
        expect(CustomMetaTwo.DYNAMIC_CONSTANT).toEqual(2);
        expect(CustomMetaOne.DYNAMIC_CONSTANT).not.toBeDefined();
    });

    describe('instance', function () {
        let instanceOne, instanceTwo;
        beforeEach(function () {
            instanceOne = new CustomMetaOne();
            instanceTwo = new CustomMetaTwo();
        });

        it('gets all the properties from its class and metaclass', function () {
            expect(instanceOne).toEqual(jasmine.objectContaining({ one: 1, two: 2 }));
            expect(instanceTwo).toEqual(jasmine.objectContaining({ one: 1, two: 2 }));
        });

        it('gets dynamically defined properties from metaclass', function () {
            expect(() => instanceOne.dynamic_method()).toThrow();
            expect(instanceTwo.dynamic_method()).toEqual(73);
        });
    });

    it('can be instantiated with Lang.Class but still get the appropriate metaclass', function () {
        expect(CustomMetaSubclass instanceof MetaClass).toBeTruthy();
        expect(CustomMetaSubclass.DYNAMIC_CONSTANT).toEqual(2);

        let instance = new CustomMetaSubclass();
        expect(instance).toEqual(jasmine.objectContaining({ one: 1, two: 2, three: 3 }));
        expect(instance.dynamic_method()).toEqual(73);
    });
});
