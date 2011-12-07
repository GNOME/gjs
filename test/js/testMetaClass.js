// application/javascript;version=1.8 -*- mode: js; indent-tabs-mode: nil -*-

if (!('assertEquals' in this)) { /* allow running this test standalone */
    imports.lang.copyPublicProperties(imports.jsUnit, this);
    gjstestRun = function() { return imports.jsUnit.gjstestRun(window); };
}

const Lang = imports.lang;

function assertArrayEquals(expected, got) {
    assertEquals(expected.length, got.length);
    for (let i = 0; i < expected.length; i ++) {
        assertEquals(expected[i], got[i]);
    }
}

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

function testMetaClass() {
    assertArrayEquals(['CustomMetaOne',
                       'CustomMetaTwo',
                       'CustomMetaSubclass'], Subclassed);

    assertTrue(NormalClass instanceof Lang.Class);
    assertTrue(MetaClass instanceof Lang.Class);

    assertTrue(CustomMetaOne instanceof Lang.Class);
    assertTrue(CustomMetaOne instanceof MetaClass);

    assertEquals(2, CustomMetaTwo.DYNAMIC_CONSTANT);
    assertUndefined(CustomMetaOne.DYNAMIC_CONSTANT);
}

function testMetaInstance() {
    let instanceOne = new CustomMetaOne();

    assertEquals(1, instanceOne.one);
    assertEquals(2, instanceOne.two);

    assertRaises(function() {
        instanceOne.dynamic_method();
    });

    let instanceTwo = new CustomMetaTwo();
    assertEquals(1, instanceTwo.one);
    assertEquals(2, instanceTwo.two);
    assertEquals(73, instanceTwo.dynamic_method());
}

function testMetaSubclass() {
    assertTrue(CustomMetaSubclass instanceof MetaClass);

    let instance = new CustomMetaSubclass();

    assertEquals(1, instance.one);
    assertEquals(2, instance.two);
    assertEquals(3, instance.three);

    assertEquals(73, instance.dynamic_method());
    assertEquals(2, CustomMetaSubclass.DYNAMIC_CONSTANT);
}

gjstestRun();
