The ByteArray type in the `imports.byteArray` module is based on an
ECMAScript 4 proposal that was never adopted. This can be found at:
http://wiki.ecmascript.org/doku.php?id=proposals:bytearray
and the wikitext of that page is appended to this file.

The main difference from the ECMA proposal is that gjs's ByteArray is
inside a module, and `toString()`/`fromString()` default to UTF-8 and take
optional encoding arguments.

There are a number of more elaborate byte array proposals in the
Common JS project at http://wiki.commonjs.org/wiki/Binary

We went with the ECMA proposal because it seems simplest, and the main
goal for most gjs users will be to shovel bytes between various C
APIs, for example reading from an IO stream and then pushing the bytes
into a parser. Actually manipulating bytes in JS is likely to be
pretty rare, and slow ... an advantage of the
gjs/gobject-introspection setup is that stuff best done in C, like
messing with bytes, can be done in C.

---

ECMAScript proposal follows; remember it's almost but not quite like
gjs ByteArray, in particular we use UTF-8 instead of busted Latin-1 as
default encoding.

---

# ByteArray #

(Also see the [discussion page][1] for this proposal)

## Overview ##

In previous versions of ECMAScript, there wasn't a good way to efficiently represent a packed array of arbitrary bytes. The predefined core object Array is inefficient for this purpose; some developers have (mis)used character strings for this purpose, which may be slightly more efficient for some implementations, but still a misuse of the string type and either a less efficient use of memory (if one byte per character was stored) or cycles (if two bytes per char).

Edition 4 will add a new predefined core object, ByteArray. A ByteArray can be thought of as similar to an Array of uint ([uint]) with each element truncated to the integer range of 0..255.

## Creating a ByteArray ##

To create a ByteArray object:

```js
byteArrayObject = new ByteArray(byteArrayLength:uint)
```

byteArrayLength is the initial length of the ByteArray, in bytes. If omitted, the initial length is zero.

All elements in a ByteArray are initialized to the value of zero.

Unlike Array, there is no special form that allows you to list the initial values for the ByteArray's elements. However, the ByteArray class has an `intrinsic::to` static method that can convert an Array to a ByteArray, and implementations are free to optimize away the Array instance if it is used exclusively to initialize a ByteArray:

```js
var values:ByteArray = [1, 2, 3, 4];	// legal by virtue of ByteArray.intrinsic::to
```

## Populating a ByteArray ##

You can populate a ByteArray by assigning values to its elements. Each element can hold only an unsigned integer in the range 0..255 (inclusive). Values will be converted to unsigned integer (if necessary), then truncated to the least-significant 8 bits.

For example,

```js
var e = new ByteArray(3);

e[0] = 0x12;		// stores 18
e[1] = Math.PI;		// stores 3
e[2] = "foo";		// stores 0 (generates compile error in strict mode)
e[2] = "42";		// stores 42 (generates compile error in strict mode)
e[2] = null;		// stores 0
e[2] = undefined;	// stores 0
```

## Referring to ByteArray Elements ##

You refer to a ByteArray's elements by using the element's ordinal number; you refer to the first element of the array as `myArray[0]` and the second element of the array as `myArray[1]`, etc. The index of the elements begins with zero (0), but the length of array (for example, `myArray.length`) reflects the number of elements in the array.

## ByteArray Methods ##

The ByteArray object has the follow methods:

### `static function fromString(s:String):ByteArray` ###

Convert a String into newly constructed ByteArray; this creates a new ByteArray of the same length as the String, then assigns each ByteArray entry the corresponding charCodeAt() value of the String. As with other ByteArray assignments, only the lower 8 bits of the charCode value will be retained.

```js
class ByteArray {
  ...
  static function fromString(s:String):ByteArray
  {
    var a:ByteArray = new Bytearray(s.length);
    for (var i:int = 0; i < s.length; ++i)
      a[i] = s.charCodeAt(i);
    return a;
  }
  ...
}
```

### `static function fromArray(s:Array):ByteArray` ###

Converts an Array into a newly constructed ByteArray; this creates a new ByteArray of the same length as the input Array, then assigns each ByteArray entry the corresponding entry value of the Array. As with other ByteArray assignments, only the lower 8 bits of the charCode value will be retained.

```js
class ByteArray {
  ...
  static function fromArray(s:Array):ByteArray
  {
    var a:ByteArray = new Bytearray(s.length);
    for (var i:int = 0; i < s.length; ++i)
      a[i] = s[i];
    return a;
  ...
}
```

### `function toString():String` ###

Converts the ByteArray into a literal string, with each character entry set to the value of the corresponding ByteArray entry.

The resulting string is guaranteed to round-trip back into an identical ByteArray by passing the result to `ByteArray.fromString()`, i.e., `b === ByteArray.fromString(b.toString())`. (Note that the reverse is not guaranteed to be true: `ByteArray.fromString(s).toString != s` unless all character codes in `s` are <= 255)

```js
class ByteArray {
  ...
  function toString():String
  {
    // XXX return String.fromCharCode.apply(String, this);
    var s:String = "";
    for (var i:int = 0; i < this.length; ++i)
      s += String.fromCharCode(this[i]);
    return s;
  }
  ...
}
```

## ByteArray Properties ##

The ByteArray object has the following properties:

### `length:uint` ###

This property contains the number of bytes in the ByteArray. Setting the length property to a smaller value decreases the size of the ByteArray, discarding the unused bytes. Setting the length property to a larger value increases the size of the ByteArray, initializing all newly-allocated elements to zero.

### `prototype:Object` ###

This property contains the methods of ByteArray.

## Prior Art ##

Adobe's ActionScript 3.0 provides [`flash.utils.ByteArray`][2], which was the primary inspiration for this proposal. Note that the ActionScript version of ByteArray includes additional functionality which has been omitted here for the sake of allowing small implementations; it is anticipated that the extra functionality can be written in ES4 itself and provided as a standard library.

[Synopsis of ActionScript's implementation too detailed and moved to [discussion][1] page]

[1] http://wiki.ecmascript.org/doku.php?id=discussion:bytearray
[2] http://livedocs.macromedia.com/flex/2/langref/flash/utils/ByteArray.html
