// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2020 Marco Trevisan <marco.trevisan@canonical.com>

const {GLib, GObject, GIMarshallingTests} = imports.gi;

GObject.TYPE_SCHAR = GObject.TYPE_CHAR;

const SIGNED_TYPES = ['schar', 'int', 'int64', 'long'];
const UNSIGNED_TYPES = ['char', 'uchar', 'uint', 'uint64', 'ulong'];
const FLOATING_TYPES = ['double', 'float'];
const NUMERIC_TYPES = [...SIGNED_TYPES, ...UNSIGNED_TYPES, ...FLOATING_TYPES];
const SPECIFIC_TYPES = ['gtype', 'boolean', 'string', 'param', 'variant'];
const INSTANCED_TYPES = ['object'];
const ALL_TYPES = [...NUMERIC_TYPES, ...SPECIFIC_TYPES, ...INSTANCED_TYPES];

describe('GObject value (GValue)', function () {
    let v;
    beforeEach(function () {
        v = new GObject.Value();
    });

    function getDefaultContentByType(type) {
        if (SIGNED_TYPES.includes(type))
            return -((Math.random() * 100 | 0) + 1);
        if (UNSIGNED_TYPES.includes(type))
            return -getDefaultContentByType('int') + 2;
        if (FLOATING_TYPES.includes(type))
            return getDefaultContentByType('uint') + 0.5;
        if (type === 'string')
            return `Hello GValue! ${getDefaultContentByType('uint')}`;
        if (type === 'boolean')
            return !!(getDefaultContentByType('int') % 2);
        if (type === 'gtype') {
            const other = ALL_TYPES[Math.random() * ALL_TYPES.length | 0];
            return GObject[`TYPE_${other.toUpperCase()}`];
        }
        if (type === 'object') {
            const wasCreatingObject = this.creatingObject;
            this.creatingObject = true;
            const props = ALL_TYPES.filter(e =>
                (e !== 'object' || !wasCreatingObject) &&
                e !== 'gtype' &&
                e !== 'param' &&
                // Include string when gobject-introspection!268 is merged
                e !== 'string' &&
                e !== 'schar').reduce((ac, a) => ({
                ...ac, [`some-${a}`]: getDefaultContentByType(a)
            }), {});
            delete this.creatingObject;
            return new GIMarshallingTests.PropertiesObject(props);
        }
        if (type === 'param') {
            return GObject.ParamSpec.string('test-param', '', getDefaultContentByType('string'),
                GObject.ParamFlags.READABLE, '');
        }
        if (type === 'variant') {
            return new GLib.Variant('a{sv}', {
                pasta: new GLib.Variant('s', 'Carbonara (con guanciale)'),
                pizza: new GLib.Variant('s', 'Verace'),
                randomString: new GLib.Variant('s', getDefaultContentByType('string')),
            });
        }

        throw new Error(`No default content set for type ${type}`);
    }

    ALL_TYPES.forEach(type => {
        const gtype = GObject[`TYPE_${type.toUpperCase()}`];
        it(`initializes ${type}`, function () {
            v.init(gtype);
        });

        it(`${type} is compatible with itself`, function () {
            expect(GObject.Value.type_compatible(gtype, gtype)).toBeTruthy();
        });

        it(`${type} is transformable to itself`, function () {
            expect(GObject.Value.type_transformable(gtype, gtype)).toBeTruthy();
        });

        describe('initialized', function () {
            let randomContent;
            beforeEach(function () {
                v.init(gtype);
                randomContent = getDefaultContentByType(type);
            });

            it(`sets and gets ${type}`, function () {
                v[`set_${type}`](randomContent);
                expect(v[`get_${type}`]()).toEqual(randomContent);
            });

            it(`copies ${type}`, function () {
                v[`set_${type}`](randomContent);

                const other = new GObject.Value();
                other.init(gtype);
                v.copy(other);
                expect(other[`get_${type}`]()).toEqual(randomContent);
            });
        });
    });

    afterEach(function () {
        v.unset();
    });
});
