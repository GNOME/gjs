// application/javascript;version=1.8

if (!('assertEquals' in this)) { /* allow running this test standalone */
    imports.lang.copyPublicProperties(imports.jsUnit, this);
    gjstestRun = function() { return imports.jsUnit.gjstestRun(window); };
}

const Regress = imports.gi.Regress;
const GObject = imports.gi.GObject;

let name = 'foo-property';
let nick = 'Foo property';
let blurb = 'This is the foo property';
let flags = GObject.ParamFlags.READABLE;

function testStringParamSpec() {
    let stringSpec = GObject.ParamSpec.string(name, nick, blurb, flags,
                                              'Default Value');

    assertEquals(name, stringSpec.name);
    assertEquals(nick, stringSpec._nick);
    assertEquals(blurb, stringSpec._blurb);
    assertEquals(flags, stringSpec.flags);
    assertEquals('Default Value', stringSpec.default_value);
}

function testIntParamSpec() {
    let intSpec = GObject.ParamSpec.int(name, nick, blurb, flags,
                                        -100, 100, -42);

    assertEquals(name, intSpec.name);
    assertEquals(nick, intSpec._nick);
    assertEquals(blurb, intSpec._blurb);
    assertEquals(flags, intSpec.flags);
    assertEquals(-42, intSpec.default_value);
}

function testUIntParamSpec() {
    let uintSpec = GObject.ParamSpec.uint(name, nick, blurb, flags,
                                          20, 100, 42);

    assertEquals(name, uintSpec.name);
    assertEquals(nick, uintSpec._nick);
    assertEquals(blurb, uintSpec._blurb);
    assertEquals(flags, uintSpec.flags);
    assertEquals(42, uintSpec.default_value);
}

function testInt64ParamSpec() {
    let int64Spec = GObject.ParamSpec.int64(name, nick, blurb, flags,
                                            0x4000,
                                            0xffffffff,
                                            0x2266bbff);

    assertEquals(name, int64Spec.name);
    assertEquals(nick, int64Spec._nick);
    assertEquals(blurb, int64Spec._blurb);
    assertEquals(flags, int64Spec.flags);
    assertEquals(0x2266bbff, int64Spec.default_value);
}

function testUInt64ParamSpec() {
    let uint64Spec = GObject.ParamSpec.uint64(name, nick, blurb, flags,
                                              0,
                                              0xffffffff,
                                              0x2266bbff);

    assertEquals(name, uint64Spec.name);
    assertEquals(nick, uint64Spec._nick);
    assertEquals(blurb, uint64Spec._blurb);
    assertEquals(flags, uint64Spec.flags);
    assertEquals(0x2266bbff, uint64Spec.default_value);
}

function testEnumParamSpec() {
    let enumSpec = GObject.ParamSpec.enum(name, nick, blurb, flags,
                                          Regress.TestEnum,
                                          Regress.TestEnum.VALUE2);

    assertEquals(name, enumSpec.name);
    assertEquals(nick, enumSpec._nick);
    assertEquals(blurb, enumSpec._blurb);
    assertEquals(flags, enumSpec.flags);
    assertEquals(Regress.TestEnum.VALUE2, enumSpec.default_value);
}

function testFlagsParamSpec() {
    let flagsSpec = GObject.ParamSpec.flags(name, nick, blurb, flags,
                                            Regress.TestFlags,
                                            Regress.TestFlags.FLAG2);

    assertEquals(name, flagsSpec.name);
    assertEquals(nick, flagsSpec._nick);
    assertEquals(blurb, flagsSpec._blurb);
    assertEquals(flags, flagsSpec.flags);
    assertEquals(Regress.TestFlags.FLAG2, flagsSpec.default_value);
}

gjstestRun();
