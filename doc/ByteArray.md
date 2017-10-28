The `imports.byteArray` module was originally based on an
ECMAScript 4 proposal that was never adopted.
Now that ES6 has typed arrays, we use `Uint8Array` to represent byte
arrays in GJS and add some extra functions for conversion to and from
strings and `GLib.Bytes`.

Unlike the old custom `ByteArray`, `Uint8Array` is not resizable. The main
goal for most gjs users will be to shovel bytes between various C
APIs, for example reading from an IO stream and then pushing the bytes
into a parser. Actually manipulating bytes in JS is likely to be
pretty rare, and slow ... an advantage of the
gjs/gobject-introspection setup is that stuff best done in C, like
messing with bytes, can be done in C.

---

## ByteArray Functions ##

The ByteArray module has the following functions:

### `fromString(s:String, encoding:String):Uint8Array` ###

Convert a String into a newly constructed `Uint8Array`; this creates a
new `Uint8Array` of the same length as the String, then assigns each
`Uint8Array` entry the corresponding byte value of the String encoded
according to the given encoding (or UTF-8 if not given).

### `toString(a:Uint8Array, encoding:String):String` ###

Converts the `Uint8Array` into a literal string. The bytes are
interpreted according to the given encoding (or UTF-8 if not given).

The resulting string is guaranteed to round-trip back into an identical ByteArray by passing the result to `ByteArray.fromString()`, i.e., `b === ByteArray.fromString(ByteArray.toString(b, encoding), encoding)`.

### `fromGBytes(b:GLib.Bytes):Uint8Array` ###

Convert a `GLib.Bytes` instance into a newly constructed `Uint8Array`.
The contents are copied.

### `toGBytes(a:Uint8Array):GLib.Bytes` ###

Converts the `Uint8Array` into a `GLib.Bytes` instance.
The contents are copied.
