// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2020 Marco Trevisan <marco.trevisan@canonical.com>

const {GLib, GObject, GIMarshallingTests, Regress} = imports.gi;

const SIGNED_TYPES = ['schar', 'int', 'int64', 'long'];
const UNSIGNED_TYPES = ['char', 'uchar', 'uint', 'uint64', 'ulong'];
const FLOATING_TYPES = ['double', 'float'];
const NUMERIC_TYPES = [...SIGNED_TYPES, ...UNSIGNED_TYPES, ...FLOATING_TYPES];
const SPECIFIC_TYPES = ['gtype', 'boolean', 'string', 'param', 'variant', 'boxed', 'gvalue'];
const INSTANCED_TYPES = ['object', 'instance'];
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
        if (type === 'gtype')
            return getGType(ALL_TYPES[Math.random() * ALL_TYPES.length | 0]);

        if (type === 'boxed' || type === 'boxed-struct') {
            return new GIMarshallingTests.BoxedStruct({
                long_: getDefaultContentByType('long'),
                // string_: getDefaultContentByType('string'), not supported
            });
        }
        if (type === 'object') {
            const wasCreatingObject = this.creatingObject;
            this.creatingObject = true;
            const props = ALL_TYPES.filter(e =>
                (e !== 'object' || !wasCreatingObject) &&
                e !== 'boxed' &&
                e !== 'gtype' &&
                e !== 'instance' &&
                e !== 'param' &&
                // Include string when gobject-introspection!268 is merged
                e !== 'string' &&
                e !== 'schar').concat([
                'boxed-struct',
            ]).reduce((ac, a) => ({
                ...ac, [`some-${a}`]: getDefaultContentByType(a),
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
        if (type === 'gvalue') {
            const value = new GObject.Value();
            const valueType = NUMERIC_TYPES[Math.random() * NUMERIC_TYPES.length | 0];
            value.init(getGType(valueType));
            setContent(value, valueType, getDefaultContentByType(valueType));
            return value;
        }
        if (type === 'instance')
            return new Regress.TestFundamentalSubObject(getDefaultContentByType('string'));


        throw new Error(`No default content set for type ${type}`);
    }

    function getGType(type) {
        if (type === 'schar')
            return GObject.TYPE_CHAR;

        if (type === 'boxed' || type === 'gvalue' || type === 'instance')
            return getDefaultContentByType(type).constructor.$gtype;

        return GObject[`TYPE_${type.toUpperCase()}`];
    }

    function getContent(gvalue, type) {
        if (type === 'gvalue')
            type = 'boxed';

        if (type === 'instance') {
            pending('https://gitlab.gnome.org/GNOME/gobject-introspection/-/merge_requests/268');
            return GIMarshallingTests.gvalue_round_trip(gvalue);
        }

        return gvalue[`get_${type}`]();
    }

    function setContent(gvalue, type, content) {
        if (type === 'gvalue')
            type = 'boxed';

        if (type === 'instance')
            pending('https://gitlab.gnome.org/GNOME/gjs/-/issues/402');

        return gvalue[`set_${type}`](content);
    }

    function skipUnsupported(type) {
        if (type === 'boxed')
            pending('https://gitlab.gnome.org/GNOME/gjs/-/issues/402');

        if (type === 'gvalue')
            pending('https://gitlab.gnome.org/GNOME/gjs/-/issues/272');
    }

    ALL_TYPES.forEach(type => {
        const gtype = getGType(type);
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
                skipUnsupported(type);
                setContent(v, type, randomContent);
                expect(getContent(v, type)).toEqual(randomContent);
            });

            it(`can be passed to a function and returns a ${type}`, function () {
                skipUnsupported(type);
                setContent(v, type, randomContent);
                expect(GIMarshallingTests.gvalue_round_trip(v)).toEqual(randomContent);
                expect(GIMarshallingTests.gvalue_copy(v)).toEqual(randomContent);
            }).pend('https://gitlab.gnome.org/GNOME/gobject-introspection/-/merge_requests/268');

            it(`copies ${type}`, function () {
                skipUnsupported(type);
                setContent(v, type, randomContent);

                const other = new GObject.Value();
                other.init(gtype);
                v.copy(other);
                expect(getContent(other, type)).toEqual(randomContent);
            });
        });

        it(`can be marshalled and un-marshalled from JS ${type}`, function () {
            if (['gtype', 'gvalue'].includes(type))
                pending('Not supported - always implicitly converted');
            const content = getDefaultContentByType(type);
            expect(GIMarshallingTests.gvalue_round_trip(content)).toEqual(content);
        }).pend('https://gitlab.gnome.org/GNOME/gobject-introspection/-/merge_requests/268');
    });

    ['int', 'uint', 'boolean', 'gtype', ...FLOATING_TYPES].forEach(type => {
        it(`can be marshalled and un-marshalled from JS gtype of ${type}`, function () {
            const gtype = getGType(type);
            expect(GIMarshallingTests.gvalue_round_trip(gtype).constructor.$gtype).toEqual(gtype);
        }).pend('https://gitlab.gnome.org/GNOME/gobject-introspection/-/merge_requests/268');
    });

    INSTANCED_TYPES.forEach(type => {
        it(`initializes from instance of ${type}`, function () {
            skipUnsupported(type);
            const instance = getDefaultContentByType(type);
            v.init_from_instance(instance);
            expect(getContent(v, type)).toEqual(instance);
        });
    });

    afterEach(function () {
        v.unset();
    });
});
