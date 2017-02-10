const System = imports.system;
const GObject = imports.gi.GObject;

describe('System.addressOf()', function () {
    it('gives different results for different objects', function () {
        let a = {some: 'object'};
        let b = {different: 'object'};
        expect(System.addressOf(a)).not.toEqual(System.addressOf(b));
    });
});

describe('System.version', function () {
    it('gives a plausible number', function () {
        expect(System.version).not.toBeLessThan(14700);
        expect(System.version).toBeLessThan(20000);
    });
});

describe('System.refcount()', function () {
    it('gives the correct number', function () {
        let o = new GObject.Object({});
        expect(System.refcount(o)).toEqual(1);
    });
});

describe('System.gc()', function () {
    it('does not crash the application', function () {
        expect(System.gc).not.toThrow();
    });
});
