const JSUnit = imports.jsUnit;

function testReflect() {
    JSUnit.assertTrue(Reflect.parse instanceof Function);

    var expr = Reflect.parse("var a = 10;");

    JSUnit.assertEquals("Program", expr.type);
    JSUnit.assertEquals("VariableDeclaration", expr.body[0].type);
    JSUnit.assertEquals("a",expr.body[0].declarations[0].id.name);
    JSUnit.assertEquals(10,expr.body[0].declarations[0].init.value);
}

JSUnit.gjstestRun(this, JSUnit.setUp, JSUnit.tearDown);
