// We use Gio to have some objects that we know exist
const Gio = imports.gi.Gio;
const GObject = imports.gi.GObject;

describe('Looking up param specs', function () {
    let p1, p2;
    beforeEach(function () {
        let findProperty = GObject.Object.find_property;
        p1 = findProperty.call(Gio.ThemedIcon, 'name');
        p2 = findProperty.call(Gio.SimpleAction, 'enabled');
    });

    it('works', function () {
        expect(p1 instanceof GObject.ParamSpec).toBeTruthy();
        expect(p2 instanceof GObject.ParamSpec).toBeTruthy();
    });

    it('gives the correct name', function () {
        expect(p1.name).toEqual('name');
        expect(p2.name).toEqual('enabled');
    });

    it('gives the default value if present', function () {
        expect(p2.default_value).toBeTruthy();
    });
});

describe('GType object', function () {
    it('has a name', function () {
        expect(GObject.TYPE_NONE.name).toEqual('void');
        expect(GObject.TYPE_STRING.name).toEqual('gchararray');
    });

    it('has a read-only name', function () {
        try {
            GObject.TYPE_STRING.name = 'foo';
        } catch (e) {
        }
        expect(GObject.TYPE_STRING.name).toEqual('gchararray');
    });

    it('has an undeletable name', function () {
        try {
            delete GObject.TYPE_STRING.name;
        } catch (e) {
        }
        expect(GObject.TYPE_STRING.name).toEqual('gchararray');
    });

    it('has a string representation', function () {
        expect(GObject.TYPE_NONE.toString()).toEqual("[object GType for 'void']");
        expect(GObject.TYPE_STRING.toString()).toEqual("[object GType for 'gchararray']");
    });
});

describe('GType marshalling', function () {
    it('marshals the invalid GType object into JS null', function () {
        expect(GObject.type_from_name('NonexistentType')).toBeNull();
        expect(GObject.type_parent(GObject.TYPE_STRING)).toBeNull();
    });
});

describe('GType prototype object', function () {
    it('has no name', function () {
        expect(GIRepositoryGType.name).toBeNull();
    });

    it('has a string representation', function () {
        expect(GIRepositoryGType.toString()).toEqual('[object GType prototype]');
    });
});
