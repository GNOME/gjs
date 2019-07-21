const {GObject, Regress} = imports.gi;

describe('Fundamental type support', function () {
    it('constructs a subtype of a fundamental type', function () {
        expect(() => new Regress.TestFundamentalSubObject('plop')).not.toThrow();
    });

    it('constructs a subtype of a hidden (no introspection data) fundamental type', function() {
        expect(() => Regress.test_create_fundamental_hidden_class_instance()).not.toThrow();
    });

    it('can marshal a subtype of a custom fundamental type into a GValue', function () {
        const fund = new Regress.TestFundamentalSubObject('plop');
        expect(() => GObject.strdup_value_contents(fund)).not.toThrow();
    });
});
