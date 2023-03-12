// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2020 Marco Trevisan <marco.trevisan@canonical.com>

const {GLib, GObject, GIMarshallingTests, Regress} = imports.gi;

export const SIGNED_TYPES = ['schar', 'int', 'int64', 'long'];
export const UNSIGNED_TYPES = ['char', 'uchar', 'uint', 'uint64', 'ulong'];
export const FLOATING_TYPES = ['double', 'float'];
export const NUMERIC_TYPES = [...SIGNED_TYPES, ...UNSIGNED_TYPES, ...FLOATING_TYPES];
export const SPECIFIC_TYPES = ['gtype', 'boolean', 'string', 'param', 'variant', 'boxed', 'gvalue'];
export const INSTANCED_TYPES = ['object', 'instance'];
export const ALL_TYPES = [...NUMERIC_TYPES, ...SPECIFIC_TYPES, ...INSTANCED_TYPES];

export function getDefaultContentByType(type) {
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

export function getGType(type) {
    if (type === 'schar')
        return GObject.TYPE_CHAR;

    if (type === 'boxed' || type === 'gvalue' || type === 'instance')
        return getDefaultContentByType(type).constructor.$gtype;

    return GObject[`TYPE_${type.toUpperCase()}`];
}

export function getContent(gvalue, type) {
    if (type === 'gvalue')
        type = 'boxed';

    if (type === 'instance')
        return GIMarshallingTests.gvalue_round_trip(gvalue);

    return gvalue[`get_${type}`]();
}

export function setContent(gvalue, type, content) {
    if (type === 'gvalue')
        type = 'boxed';

    if (type === 'instance')
        pending('https://gitlab.gnome.org/GNOME/gjs/-/issues/402');

    return gvalue[`set_${type}`](content);
}

export function skipUnsupported(type) {
    if (type === 'boxed')
        pending('https://gitlab.gnome.org/GNOME/gjs/-/issues/402');

    if (type === 'gvalue')
        pending('https://gitlab.gnome.org/GNOME/gjs/-/issues/272');
}
