# ByteArray

The `ByteArray` module provides a number of utilities for converting between
[`GLib.Bytes`][gbytes] object, `String` values and `Uint8Array` objects.

It was originally based on an ECMAScript 4 proposal that was never adopted, but
now that ES6 has typed arrays, we use the standard `Uint8Array` to represent
byte arrays in GJS.

The primary use for most GJS users will be to exchange bytes between various C
APIs, like reading from an IO stream and then pushing the bytes into a parser.
Actually manipulating bytes in GJS is likely to be pretty slow and fortunately
rarely necessary. An advantage of the GJS and GObject-Introspection setup is
that most of the tasks best done in C, like messing with bytes, can be.

[gbytes]: https://gjs-docs.gnome.org/glib20/glib.bytes

#### Import

> Attention: This module is not available as an ECMAScript Module

The `ByteArray` module is available on the global `imports` object:

```js
const ByteArray = imports.byteArray;
```

### ByteArray.fromString(string, encoding)

> Deprecated: Use [`TextEncoder.encode()`][textencoder-encode] instead

Type:
* Static

Parameters:
* string (`String`) — A string to encode
* encoding (`String`) — Optional encoding of `string`

Returns:
* (`Uint8Array`) — A byte array

Convert a String into a newly constructed `Uint8Array`; this creates a
new `Uint8Array` of the same length as the String, then assigns each
`Uint8Array` entry the corresponding byte value of the String encoded
according to the given encoding (or UTF-8 if not given).

[textencoder-encode]: https://gjs-docs.gnome.org/gjs/encoding.md#textencoder-encode

### ByteArray.toString(byteArray, encoding)

> Deprecated: Use [`TextDecoder.decode()`][textdecoder-decode] instead

Type:
* Static

Parameters:
* byteArray (`Uint8Array`) — A byte array to decode
* encoding (`String`) — Optional encoding of `byteArray`

Returns:
* (`String`) — A string

Converts the `Uint8Array` into a literal string. The bytes are
interpreted according to the given encoding (or UTF-8 if not given).

The resulting string is guaranteed to round-trip back into an identical
ByteArray by passing the result to `ByteArray.fromString()`. In other words,
this check is guaranteed to pass:

```js
const original = ByteArray.fromString('foobar');
const copy = ByteArray.fromString(ByteArray.toString(original));

console.assert(original.every((value, index) => value === copy[index]));
```

[textdecoder-decode]: https://gjs-docs.gnome.org/gjs/encoding.md#textdecoder-decode

### ByteArray.fromGBytes(bytes)

> Deprecated: Use [`GLib.Bytes.toArray()`][gbytes-toarray] instead

Type:
* Static

Parameters:
* bytes (`GLib.Bytes`) — A [`GLib.Bytes`][gbytes] to convert

Returns:
* (`Uint8Array`) — A new byte array

Convert a [`GLib.Bytes`][gbytes] instance into a newly constructed `Uint8Array`.

The contents are copied.

[gbytes]: https://gjs-docs.gnome.org/glib20/glib.bytes
[gbytes-toarray]: https://gjs-docs.gnome.org/gjs/overrides.md#glib-bytes-toarray

### ByteArray.toGBytes(byteArray)

> Deprecated: Use [`new GLib.Bytes()`][gbytes] instead

Type:
* Static

Parameters:
* byteArray (`Uint8Array`) — A byte array to convert

Returns:
* (`GLib.Bytes`) — A new [`GLib.Bytes`][gbytes]

Converts the `Uint8Array` into a [`GLib.Bytes`][gbytes] instance.

The contents are copied.

[gbytes]: https://gjs-docs.gnome.org/glib20/glib.bytes

