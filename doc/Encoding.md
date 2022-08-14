# Encoding

GJS implements the [WHATWG Encoding][whatwg-encoding] specification.

The `TextDecoder` interface represents a decoder for a specific text encoding,
such as `UTF-8`, `ISO-8859-2`, `KOI8-R`, `GBK`, etc. A decoder takes a list of
bytes as input and emits a list of code points.

The `TextEncoder` interface takes a list of code points as input and emits a
list of UTF-8 bytes.

#### Import

The functions in this module are available globally, without import.

[whatwg-encoding]: https://encoding.spec.whatwg.org/

### TextDecoder(utfLabel, options)

Type:
* Static

Parameters:
* utfLabel (`Number`) — Optional string, defaulting to `"utf-8"`, containing the
  label of the encoder.
* options (`Object`) — Optional dictionary with the `Boolean` property `fatal`,
  corresponding to the `TextDecoder.fatal` property.
  
Returns:
* (`TextDecoder`) — A newly created `TextDecoder` object

> New in GJS 1.70 (GNOME 41)

The `TextDecoder()` constructor returns a newly created `TextDecoder` object for
the encoding specified in parameter.

If the value for `utfLabel` is unknown, or is one of the two values leading to a
'replacement' decoding algorithm ("iso-2022-cn" or "iso-2022-cn-ext"), a
`RangeError` is thrown.

### TextDecoder.encoding

Type:
* `String`

> New in GJS 1.70 (GNOME 41)

The `TextDecoder.encoding` read-only property returns a string containing the
name of the decoding algorithm used by the specific decoder.

### TextDecoder.fatal

Type:
* `Boolean`

> New in GJS 1.70 (GNOME 41)

The fatal property of the `TextDecoder` interface is a `Boolean` indicating
whether the error mode is fatal. If this value is `true`, the processed text
cannot be decoded because of malformed data. If this value is `false` malformed
data is replaced with placeholder characters.

### TextDecoder.ignoreBOM

Type:
* `Boolean`

> New in GJS 1.70 (GNOME 41)

The `ignoreBOM` property of the `TextDecoder` interface is a `Boolean`
indicating whether the byte order mark is ignored.

### TextDecoder.decode(buffer, options)

Parameters:
* buffer (`Number`) — Optional `ArrayBuffer`, a `TypedArray` or a `DataView`
  object containing the text to decode.
* options (`Object`) — Optional dictionary with the `Boolean` property `fatal`,
  indicating that additional data will follow in subsequent calls to `decode()`.
  Set to `true` if processing the data in chunks, and `false` for the final
  chunk or if the data is not chunked. It defaults to `false`. 
  
Returns:
* (`String`) — A string result

> New in GJS 1.70 (GNOME 41)

The `TextDecode.decode()` method returns a string containing the text, given in
parameters, decoded with the specific method for that `TextDecoder` object.

### TextEncoder()

Type:
* Static

> New in GJS 1.70 (GNOME 41)

The `TextEncoder()` constructor returns a newly created `TextEncoder` object
that will generate a byte stream with UTF-8 encoding.

### TextEncoder.encoding

Type:
* `String`

> New in GJS 1.70 (GNOME 41)

The `TextEncoder.encoding` read-only property returns a string containing the
name of the encoding algorithm used by the specific encoder.

It can only have the following value `utf-8`.

### TextEncoder.encode(string)

Parameters:
* string (`String`) — A string containing the text to encode

Returns:
* (`Uint8Array`) — A `Uint8Array` object containing UTF-8 encoded text

> New in GJS 1.70 (GNOME 41)

The `TextEncoder.encode()` method takes a string as input, and returns a
`Uint8Array` containing the text given in parameters encoded with the specific
method for that `TextEncoder` object.

### TextEncoder.encodeInto(input, output)

Parameters:
* input (`String`) — A string containing the text to encode
* output (`Uint8Array`) — A `Uint8Array` object instance to place the resulting
  UTF-8 encoded text into.

Returns:
* (`{String: Number}`) — An object containing the number of UTF-16 units read
  and bytes written

> New in GJS 1.70 (GNOME 41)

The `TextEncoder.encode()` method takes a string as input, and returns a
`Uint8Array` containing the text given in parameters encoded with the specific
method for that `TextEncoder` object.

The returned object contains two members:
* `read`
  The number of UTF-16 units of code from the source that has been converted
  over to UTF-8. This may be less than `string.length` if `uint8Array` did not
  have enough space.
* `written`
  The number of bytes modified in the destination `Uint8Array`. The bytes
  written are guaranteed to form complete UTF-8 byte sequences.

