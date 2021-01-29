# Byte Arrays

**Access with `imports.byteArray`**

## Text Encoding Functions ##

### `fromString(s: string, encoding: string): Uint8Array` ###

Convert a String into a newly constructed `Uint8Array`; this creates a
new `Uint8Array` of the same length as the String, then assigns each
`Uint8Array` entry the corresponding byte value of the `string` encoded
according to the given encoding (or UTF-8 if not given).

### `toString(a: Uint8Array, encoding: string): string` ###

Converts the `Uint8Array` into a literal string. The bytes are
interpreted according to the given encoding (or UTF-8 if not given).

The resulting string is guaranteed to round-trip back into an identical ByteArray by passing the result to `ByteArray.fromString()`, i.e., `b === ByteArray.fromString(ByteArray.toString(b, encoding), encoding)`.

## [Deprecated Functions](https://gitlab.gnome.org/GNOME/gjs/blob/master/modules/script/byteArray.js)

The legacy `imports.byteArray` module was originally based on an
ECMAScript 4 proposal that was never adopted.
Now that ES6 has typed arrays, we use `Uint8Array` to represent byte
arrays in GJS. `imports.byteArray` contains functions for converting to and from
strings and `GLib.Bytes`, these can be replaced by methods on `GLib.Bytes`.

Unlike the old custom `ByteArray`, `Uint8Array` is not resizable. The main
goal for most gjs users will be to shovel bytes between various C
APIs, for example reading from an IO stream and then pushing the bytes
into a parser. Actually manipulating bytes in JS is likely to be
pretty rare, and slow ... an advantage of the
gjs/gobject-introspection setup is that stuff best done in C, like
messing with bytes, can be done in C.

The ByteArray module has the following functions:

### `fromGBytes(b: GLib.Bytes): Uint8Array` ###

> Prefer `gbytes.toArray()`.

Convert a `GLib.Bytes` instance into a newly constructed `Uint8Array`.
The contents are copied.

### `toGBytes(a: Uint8Array): GLib.Bytes` ###

> Prefer `new GLib.Bytes(array)`.

Converts the `Uint8Array` into a `GLib.Bytes` instance.
The contents are copied.

