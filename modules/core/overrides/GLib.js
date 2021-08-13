// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2011 Giovanni Campagna

const ByteArray = imports.byteArray;

let GLib;

const SIMPLE_TYPES = ['b', 'y', 'n', 'q', 'i', 'u', 'x', 't', 'h', 'd', 's', 'o', 'g'];

function _readSingleType(signature, forceSimple) {
    let char = signature.shift();
    let isSimple = false;

    if (!SIMPLE_TYPES.includes(char)) {
        if (forceSimple)
            throw new TypeError('Invalid GVariant signature (a simple type was expected)');
    } else {
        isSimple = true;
    }

    if (char === 'm' || char === 'a')
        return [char].concat(_readSingleType(signature, false));
    if (char === '{') {
        let key = _readSingleType(signature, true);
        let val = _readSingleType(signature, false);
        let close = signature.shift();
        if (close !== '}')
            throw new TypeError('Invalid GVariant signature for type DICT_ENTRY (expected "}"');
        return [char].concat(key, val, close);
    }
    if (char === '(') {
        let res = [char];
        while (true) {
            if (signature.length === 0)
                throw new TypeError('Invalid GVariant signature for type TUPLE (expected ")")');
            let next = signature[0];
            if (next === ')') {
                signature.shift();
                return res.concat(next);
            }
            let el = _readSingleType(signature);
            res = res.concat(el);
        }
    }

    // Valid types are simple types, arrays, maybes, tuples, dictionary entries and variants
    if (!isSimple && char !== 'v')
        throw new TypeError(`Invalid GVariant signature (${char} is not a valid type)`);

    return [char];
}

function _packVariant(signature, value) {
    if (signature.length === 0)
        throw new TypeError('GVariant signature cannot be empty');

    let char = signature.shift();
    switch (char) {
    case 'b':
        return GLib.Variant.new_boolean(value);
    case 'y':
        return GLib.Variant.new_byte(value);
    case 'n':
        return GLib.Variant.new_int16(value);
    case 'q':
        return GLib.Variant.new_uint16(value);
    case 'i':
        return GLib.Variant.new_int32(value);
    case 'u':
        return GLib.Variant.new_uint32(value);
    case 'x':
        return GLib.Variant.new_int64(value);
    case 't':
        return GLib.Variant.new_uint64(value);
    case 'h':
        return GLib.Variant.new_handle(value);
    case 'd':
        return GLib.Variant.new_double(value);
    case 's':
        return GLib.Variant.new_string(value);
    case 'o':
        return GLib.Variant.new_object_path(value);
    case 'g':
        return GLib.Variant.new_signature(value);
    case 'v':
        return GLib.Variant.new_variant(value);
    case 'm':
        if (value !== null) {
            return GLib.Variant.new_maybe(null, _packVariant(signature, value));
        } else {
            return GLib.Variant.new_maybe(new GLib.VariantType(
                _readSingleType(signature, false).join('')), null);
        }
    case 'a': {
        let arrayType = _readSingleType(signature, false);
        if (arrayType[0] === 's') {
            // special case for array of strings
            return GLib.Variant.new_strv(value);
        }
        if (arrayType[0] === 'y') {
            // special case for array of bytes
            let bytes;
            if (typeof value === 'string') {
                let byteArray = ByteArray.fromString(value);
                if (byteArray[byteArray.length - 1] !== 0)
                    byteArray = Uint8Array.of(...byteArray, 0);
                bytes = ByteArray.toGBytes(byteArray);
            } else {
                bytes = new GLib.Bytes(value);
            }
            return GLib.Variant.new_from_bytes(new GLib.VariantType('ay'),
                bytes, true);
        }

        let arrayValue = [];
        if (arrayType[0] === '{') {
            // special case for dictionaries
            for (let key in value) {
                let copy = [].concat(arrayType);
                let child = _packVariant(copy, [key, value[key]]);
                arrayValue.push(child);
            }
        } else {
            for (let i = 0; i < value.length; i++) {
                let copy = [].concat(arrayType);
                let child = _packVariant(copy, value[i]);
                arrayValue.push(child);
            }
        }
        return GLib.Variant.new_array(new GLib.VariantType(arrayType.join('')), arrayValue);
    }

    case '(': {
        let children = [];
        for (let i = 0; i < value.length; i++) {
            let next = signature[0];
            if (next === ')')
                break;
            children.push(_packVariant(signature, value[i]));
        }

        if (signature[0] !== ')')
            throw new TypeError('Invalid GVariant signature for type TUPLE (expected ")")');
        signature.shift();
        return GLib.Variant.new_tuple(children);
    }
    case '{': {
        let key = _packVariant(signature, value[0]);
        let child = _packVariant(signature, value[1]);

        if (signature[0] !== '}')
            throw new TypeError('Invalid GVariant signature for type DICT_ENTRY (expected "}")');
        signature.shift();

        return GLib.Variant.new_dict_entry(key, child);
    }
    default:
        throw new TypeError(`Invalid GVariant signature (unexpected character ${char})`);
    }
}

function _unpackVariant(variant, deep, recursive = false) {
    switch (String.fromCharCode(variant.classify())) {
    case 'b':
        return variant.get_boolean();
    case 'y':
        return variant.get_byte();
    case 'n':
        return variant.get_int16();
    case 'q':
        return variant.get_uint16();
    case 'i':
        return variant.get_int32();
    case 'u':
        return variant.get_uint32();
    case 'x':
        return variant.get_int64();
    case 't':
        return variant.get_uint64();
    case 'h':
        return variant.get_handle();
    case 'd':
        return variant.get_double();
    case 'o':
    case 'g':
    case 's':
        // g_variant_get_string has length as out argument
        return variant.get_string()[0];
    case 'v': {
        const ret = variant.get_variant();
        if (deep && recursive && ret instanceof GLib.Variant)
            return _unpackVariant(ret, deep, recursive);
        return ret;
    }
    case 'm': {
        let val = variant.get_maybe();
        if (deep && val)
            return _unpackVariant(val, deep, recursive);
        else
            return val;
    }
    case 'a':
        if (variant.is_of_type(new GLib.VariantType('a{?*}'))) {
            // special case containers
            let ret = { };
            let nElements = variant.n_children();
            for (let i = 0; i < nElements; i++) {
                // always unpack the dictionary entry, and always unpack
                // the key (or it cannot be added as a key)
                let val = _unpackVariant(variant.get_child_value(i), deep,
                    recursive);
                let key;
                if (!deep)
                    key = _unpackVariant(val[0], true);
                else
                    key = val[0];
                ret[key] = val[1];
            }
            return ret;
        }
        if (variant.is_of_type(new GLib.VariantType('ay'))) {
            // special case byte arrays
            return variant.get_data_as_bytes().toArray();
        }

        // fall through
    case '(':
    case '{': {
        let ret = [];
        let nElements = variant.n_children();
        for (let i = 0; i < nElements; i++) {
            let val = variant.get_child_value(i);
            if (deep)
                ret.push(_unpackVariant(val, deep, recursive));
            else
                ret.push(val);
        }
        return ret;
    }
    }

    throw new Error('Assertion failure: this code should not be reached');
}

function _notIntrospectableError(funcName, replacement) {
    return new Error(`${funcName} is not introspectable. Use ${replacement} instead.`);
}

function _warnNotIntrospectable(funcName, replacement) {
    logError(_notIntrospectableError(funcName, replacement));
}

function _escapeCharacterSetChars(char) {
    if ('-^]\\'.includes(char))
        return `\\${char}`;
    return char;
}

function _init() {
    // this is imports.gi.GLib

    GLib = this;

    // small HACK: we add a matches() method to standard Errors so that
    // you can do "if (e.matches(Ns.FooError, Ns.FooError.SOME_CODE))"
    // without checking instanceof
    Error.prototype.matches = function () {
        return false;
    };

    // Guard against domains that aren't valid quarks and would lead
    // to a crash
    const quarkToString = this.quark_to_string;
    const realNewLiteral = this.Error.new_literal;
    this.Error.new_literal = function (domain, code, message) {
        if (quarkToString(domain) === null)
            throw new TypeError(`Error.new_literal: ${domain} is not a valid domain`);
        return realNewLiteral(domain, code, message);
    };

    this.Variant._new_internal = function (sig, value) {
        let signature = Array.prototype.slice.call(sig);

        let variant = _packVariant(signature, value);
        if (signature.length !== 0)
            throw new TypeError('Invalid GVariant signature (more than one single complete type)');

        return variant;
    };

    // Deprecate version of new GLib.Variant()
    this.Variant.new = function (sig, value) {
        return new GLib.Variant(sig, value);
    };
    this.Variant.prototype.unpack = function () {
        return _unpackVariant(this, false);
    };
    this.Variant.prototype.deepUnpack = function () {
        return _unpackVariant(this, true);
    };
    // backwards compatibility alias
    this.Variant.prototype.deep_unpack = this.Variant.prototype.deepUnpack;

    // Note: discards type information, if the variant contains any 'v' types
    this.Variant.prototype.recursiveUnpack = function () {
        return _unpackVariant(this, true, true);
    };

    this.Variant.prototype.toString = function () {
        return `[object variant of type "${this.get_type_string()}"]`;
    };

    this.Bytes.prototype.toArray = function () {
        return imports._byteArrayNative.fromGBytes(this);
    };

    this.log_structured =
    /**
     * @param {string} logDomain
     * @param {GLib.LogLevelFlags} logLevel
     * @param {Record<string, unknown>} stringFields
     * @returns {void}
     */
    function log_structured(logDomain, logLevel, stringFields) {
        /** @type {Record<string, GLib.Variant>} */
        let fields = {};

        for (let key in stringFields) {
            const field = stringFields[key];

            if (field instanceof Uint8Array) {
                fields[key] = new GLib.Variant('ay', field);
            } else if (typeof field === 'string') {
                fields[key] = new GLib.Variant('s', field);
            } else if (field instanceof GLib.Variant) {
                // GLib.log_variant converts all Variants that are
                // not 'ay' or 's' type to strings by printing
                // them.
                //
                // https://gitlab.gnome.org/GNOME/glib/-/blob/a380bfdf93cb3bfd3cd4caedc0127c4e5717545b/glib/gmessages.c#L1894
                fields[key] = field;
            } else {
                throw new TypeError(`Unsupported value ${field}, log_structured supports GLib.Variant, Uint8Array, and string values.`);
            }
        }

        GLib.log_variant(logDomain, logLevel, new GLib.Variant('a{sv}', fields));
    };

    // GjsPrivate depends on GLib so we cannot import it
    // before GLib is fully resolved.

    this.log_set_writer_func_variant = function (...args) {
        const {log_set_writer_func} = imports.gi.GjsPrivate;

        log_set_writer_func(...args);
    };

    this.log_set_writer_default = function (...args) {
        const {log_set_writer_default} = imports.gi.GjsPrivate;

        log_set_writer_default(...args);
    };

    this.log_set_writer_func = function (writer_func) {
        const {log_set_writer_func} = imports.gi.GjsPrivate;

        if (typeof writer_func !== 'function') {
            log_set_writer_func(writer_func);
        } else {
            log_set_writer_func(function (logLevel, stringFields) {
                const stringFieldsObj = {...stringFields.recursiveUnpack()};
                return writer_func(logLevel, stringFieldsObj);
            });
        }
    };

    this.VariantDict.prototype.lookup = function (key, variantType = null, deep = false) {
        if (typeof variantType === 'string')
            variantType = new GLib.VariantType(variantType);

        const variant = this.lookup_value(key, variantType);
        if (variant === null)
            return null;
        return _unpackVariant(variant, deep);
    };

    // Prevent user code from calling GLib string manipulation functions that
    // return the same string that was passed in. These can't be annotated
    // properly, and will mostly crash.
    // Here we provide approximate implementations of the functions so that if
    // they had happened to work in the past, they will continue working, but
    // log a stack trace and a suggestion of what to use instead.
    // Exceptions are thrown instead for GLib.stpcpy() of which the return value
    // is useless anyway and GLib.ascii_formatd() which is too complicated to
    // implement here.

    this.stpcpy = function () {
        throw _notIntrospectableError('GLib.stpcpy()', 'the + operator');
    };

    this.strstr_len = function (haystack, len, needle) {
        _warnNotIntrospectable('GLib.strstr_len()', 'String.indexOf()');
        let searchString = haystack;
        if (len !== -1)
            searchString = searchString.slice(0, len);
        const index = searchString.indexOf(needle);
        if (index === -1)
            return null;
        return haystack.slice(index);
    };

    this.strrstr = function (haystack, needle) {
        _warnNotIntrospectable('GLib.strrstr()', 'String.lastIndexOf()');
        const index = haystack.lastIndexOf(needle);
        if (index === -1)
            return null;
        return haystack.slice(index);
    };

    this.strrstr_len = function (haystack, len, needle) {
        _warnNotIntrospectable('GLib.strrstr_len()', 'String.lastIndexOf()');
        let searchString = haystack;
        if (len !== -1)
            searchString = searchString.slice(0, len);
        const index = searchString.lastIndexOf(needle);
        if (index === -1)
            return null;
        return haystack.slice(index);
    };

    this.strup = function (string) {
        _warnNotIntrospectable('GLib.strup()',
            'String.toUpperCase() or GLib.ascii_strup()');
        return string.toUpperCase();
    };

    this.strdown = function (string) {
        _warnNotIntrospectable('GLib.strdown()',
            'String.toLowerCase() or GLib.ascii_strdown()');
        return string.toLowerCase();
    };

    this.strreverse = function (string) {
        _warnNotIntrospectable('GLib.strreverse()',
            'Array.reverse() and String.join()');
        return [...string].reverse().join('');
    };

    this.ascii_dtostr = function (unused, len, number) {
        _warnNotIntrospectable('GLib.ascii_dtostr()', 'JS string conversion');
        return `${number}`.slice(0, len);
    };

    this.ascii_formatd = function () {
        throw _notIntrospectableError('GLib.ascii_formatd()',
            'Number.toExponential() and string interpolation');
    };

    this.strchug = function (string) {
        _warnNotIntrospectable('GLib.strchug()', 'String.trimStart()');
        return string.trimStart();
    };

    this.strchomp = function (string) {
        _warnNotIntrospectable('GLib.strchomp()', 'String.trimEnd()');
        return string.trimEnd();
    };

    // g_strstrip() is a macro and therefore doesn't even appear in the GIR
    // file, but we may as well include it here since it's trivial
    this.strstrip = function (string) {
        _warnNotIntrospectable('GLib.strstrip()', 'String.trim()');
        return string.trim();
    };

    this.strdelimit = function (string, delimiters, newDelimiter) {
        _warnNotIntrospectable('GLib.strdelimit()', 'String.replace()');

        if (delimiters === null)
            delimiters = GLib.STR_DELIMITERS;
        if (typeof newDelimiter === 'number')
            newDelimiter = String.fromCharCode(newDelimiter);

        const delimiterChars = delimiters.split('');
        const escapedDelimiterChars = delimiterChars.map(_escapeCharacterSetChars);
        const delimiterRegex = new RegExp(`[${escapedDelimiterChars.join('')}]`, 'g');
        return string.replace(delimiterRegex, newDelimiter);
    };

    this.strcanon = function (string, validChars, substitutor) {
        _warnNotIntrospectable('GLib.strcanon()', 'String.replace()');

        if (typeof substitutor === 'number')
            substitutor = String.fromCharCode(substitutor);

        const validArray = validChars.split('');
        const escapedValidArray = validArray.map(_escapeCharacterSetChars);
        const invalidRegex = new RegExp(`[^${escapedValidArray.join('')}]`, 'g');
        return string.replace(invalidRegex, substitutor);
    };
}
