const Regress = imports.gi.Regress;

describe('Fundamental type support', function () {
    it('constructs a subtype of a fundamental type', function () {
        expect(() => new Regress.TestFundamentalSubObject('plop')).not.toThrow();
    });

    it('constructs a subtype of a hidden (no introspection data) fundamental type', function() {
        expect(() => Regress.test_create_fundamental_hidden_class_instance()).not.toThrow();
    });
});
