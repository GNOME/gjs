/* exported ByteArray, fromArray, fromGBytes, fromString, toGBytes, toString */

var {fromGBytes, fromString, toGBytes, toString} = imports._byteArrayNative;

// For backwards compatibility

function fromArray(a) {
    return Uint8Array.from(a);
}

var ByteArray = Uint8Array;

Uint8Array.prototype.toString = function(encoding = 'UTF-8') {
    return toString(this, encoding);
};

Uint8Array.prototype.toGBytes = function() {
    return toGBytes(this);
};
