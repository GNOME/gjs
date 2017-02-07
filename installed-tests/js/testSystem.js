const System = imports.system;

describe('System.addressOf()', function () {
    it('gives different results for different objects', function () {
        let a = {some: 'object'};
        let b = {different: 'object'};
        expect(System.addressOf(a)).not.toEqual(System.addressOf(b));
    });
});

describe('System.version', function () {
    it('gives a plausible number', function () {
        expect(System.version).not.toBeLessThan(13600);
    });
});
