// Copyright 2011 Giovanni Campagna
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to
// deal in the Software without restriction, including without limitation the
// rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.

const ByteArray = imports.byteArray;

let GLib;
let originalVariantClass;

const SIMPLE_TYPES = ['b', 'y', 'n', 'q', 'i', 'u', 'x', 't', 'h', 'd', 's', 'o', 'g'];

function _read_single_type(signature, forceSimple) {
    let char = signature.shift();
    let isSimple = false;

    if (SIMPLE_TYPES.indexOf(char) == -1) {
	if (forceSimple)
	    throw new TypeError('Invalid GVariant signature (a simple type was expected)');
    } else
	isSimple = true;

    if (char == 'm' || char == 'a')
	return [char].concat(_read_single_type(signature, false));
    if (char == '{') {
	let key = _read_single_type(signature, true);
	let val = _read_single_type(signature, false);
	let close = signature.shift();
	if (close != '}')
	    throw new TypeError('Invalid GVariant signature for type DICT_ENTRY (expected "}"');
	return [char].concat(key, val, close);
    }
    if (char == '(') {
	let res = [char];
	while (true) {
	    if (signature.length == 0)
		throw new TypeError('Invalid GVariant signature for type TUPLE (expected ")")');
	    let next = signature[0];
	    if (next == ')') {
		signature.shift();
		return res.concat(next);
	    }
	    let el = _read_single_type(signature);
	    res = res.concat(el);
	}
    }

    // Valid types are simple types, arrays, maybes, tuples, dictionary entries and variants
    if (!isSimple && char != 'v')
	throw new TypeError('Invalid GVariant signature (' + char + ' is not a valid type)');

    return [char];
}

function _makeBytes(byteArray) {
    if (byteArray instanceof ByteArray.ByteArray)
        return byteArray.toGBytes();
    else
        return new GLib.Bytes(byteArray);
}

function _pack_variant(signature, value) {
    if (signature.length == 0)
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
	if (value != null)
	    return GLib.Variant.new_maybe(null, _pack_variant(signature, value));
	else
	    return GLib.Variant.new_maybe(new GLib.VariantType(_read_single_type(signature, false).join('')), null);
    case 'a':
	let arrayType = _read_single_type(signature, false);
	if (arrayType[0] == 's') {
	    // special case for array of strings
	    return GLib.Variant.new_strv(value);
	}
	if (arrayType[0] == 'y') {
	    // special case for array of bytes
	    return GLib.Variant.new_from_bytes(new GLib.VariantType('ay'),
                                               _makeBytes(value), true);
	}

	let arrayValue = [];
	if (arrayType[0] == '{') {
	    // special case for dictionaries
	    for (let key in value) {
		let copy = [].concat(arrayType);
		let child = _pack_variant(copy, [key, value[key]]);
		arrayValue.push(child);
	    }
	} else {
	    for (let i = 0; i < value.length; i++) {
		let copy = [].concat(arrayType);
		let child = _pack_variant(copy, value[i]);
		arrayValue.push(child);
	    }
	}
	return GLib.Variant.new_array(new GLib.VariantType(arrayType.join('')), arrayValue);

    case '(':
	let children = [ ];
	for (let i = 0; i < value.length; i++) {
	    let next = signature[0];
	    if (next == ')')
		break;
	    children.push(_pack_variant(signature, value[i]));
	}

	if (signature[0] != ')')
	    throw new TypeError('Invalid GVariant signature for type TUPLE (expected ")")');
	signature.shift();
	return GLib.Variant.new_tuple(children);
    case '{':
	let key = _pack_variant(signature, value[0]);
	let child = _pack_variant(signature, value[1]);

	if (signature[0] != '}')
	    throw new TypeError('Invalid GVariant signature for type DICT_ENTRY (expected "}")');
	signature.shift();

	return GLib.Variant.new_dict_entry(key, child);
    default:
	throw new TypeError('Invalid GVariant signature (unexpected character ' + char + ')');
    }
}

function _unpack_variant(variant, deep) {
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
    case 'v':
	return variant.get_variant();
    case 'm':
	let val = variant.get_maybe();
	if (deep && val)
	    return _unpack_variant(val, deep);
	else
	    return val;
    case 'a':
	if (variant.is_of_type(new GLib.VariantType('a{?*}'))) {
	    // special case containers
	    let ret = { };
	    let nElements = variant.n_children();
	    for (let i = 0; i < nElements; i++) {
		// always unpack the dictionary entry, and always unpack
		// the key (or it cannot be added as a key)
		let val = _unpack_variant(variant.get_child_value(i), deep);
		let key;
		if (!deep)
		    key = _unpack_variant(val[0], true);
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
    case '{':
	let ret = [ ];
	let nElements = variant.n_children();
	for (let i = 0; i < nElements; i++) {
	    let val = variant.get_child_value(i);
	    if (deep)
		ret.push(_unpack_variant(val, deep));
	    else
		ret.push(val);
	}
	return ret;
    }

    throw new Error('Assertion failure: this code should not be reached');
}

function _init() {
    // this is imports.gi.GLib

    GLib = this;

    // small HACK: we add a matches() method to standard Errors so that
    // you can do "catch(e if e.matches(Ns.FooError, Ns.FooError.SOME_CODE))"
    // without checking instanceof
    Error.prototype.matches = function() { return false; };

    this.Variant._new_internal = function(sig, value) {
	let signature = Array.prototype.slice.call(sig);

	let variant = _pack_variant(signature, value);
	if (signature.length != 0)
	    throw new TypeError('Invalid GVariant signature (more than one single complete type)');

	return variant;
    };

    // Deprecate version of new GLib.Variant()
    this.Variant.new = function(sig, value) {
	return new GLib.Variant(sig, value);
    };
    this.Variant.prototype.unpack = function() {
	return _unpack_variant(this, false);
    };
    this.Variant.prototype.deep_unpack = function() {
	return _unpack_variant(this, true);
    };
    this.Variant.prototype.toString = function() {
	return '[object variant of type "' + this.get_type_string() + '"]';
    };

    this.Bytes.prototype.toArray = function() {
	return imports.byteArray.fromGBytes(this);
    };
}
