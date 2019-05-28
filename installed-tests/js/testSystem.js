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

describe('System.addressOfGObject()', function () {
    it('gives different results for different objects', function () {
        let a = new GObject.Object({});
        let b = new GObject.Object({});
        expect(System.addressOfGObject(a)).toEqual(System.addressOfGObject(a));
        expect(System.addressOfGObject(a)).not.toEqual(System.addressOfGObject(b));
    });

    it('returns (nil) for non GObject objects', function () {
        let a = new GObject.Object({});
        expect(System.addressOfGObject(a)).not.toEqual(System.addressOfGObject({}));
        expect(System.addressOfGObject({})).toEqual('(nil)');
    });
});

describe('System.gc()', function () {
    it('does not crash the application', function () {
        expect(System.gc).not.toThrow();
    });
});

describe('System.dumpHeap()', function () {
    it('throws but does not crash when given a nonexistent path', function () {
        expect(() => System.dumpHeap('/does/not/exist')).toThrow();
    });
});
