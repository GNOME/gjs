# Format

The `Format` module is a mostly deprecated module that implements `printf()`
style formatting for GJS.

In most cases, native [template literals][template-literals] should be preferred
now, except in few situations like Gettext (See [Bug #60027][bug-60027]).

```js
const foo = 'Pi';
const bar = 1;
const baz = Math.PI;

// expected result: "Pi to 2 decimal points: 3.14"

// Native template literals
const str1 = `${foo} to ${bar*2} decimal points: ${baz.toFixed(bar*2)}`

// Format.vprintf()
const str2 = Format.vprintf('%s to %d decimal points: %.2f', [foo, bar*2, baz]);
```

#### Import

> Attention: This module is not available as an ECMAScript Module

The `Format` module is available on the global `imports` object:

```js
const Format = imports.format;
```

[template-literals]: https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Template_literals
[bug-60027]: https://savannah.gnu.org/bugs/?60027

### Format.format(...args)

> Deprecated: Use [`Format.vprintf()`](#format-vprintf) instead

Type:
* Prototype Function

Parameters:
* args (`Any`) — Formatting substitutions

Returns:
* (`String`) — A new formatted string

This function was intended to extend the `String` object and provide a
`String.format` API for string formatting.

Example usage:

```js
const Format = imports.format;

// Applying format() to the string prototype.
//
// This is highly discouraged, especially in GNOME Shell extensions where other
// extensions might overwrite it. Use Format.vprintf() directly instead.
String.prototype.format = Format.format;

// Usage with String.prototype.format()
// expected result: "A formatted string"
const str = 'A %s %s'.format('formatted', 'string');
```

### Format.printf(fmt, ...args)

> Deprecated: Use [template literals][template-literals] with `print()` instead

Type:
* Static

Parameters:
* fmt (`String`) — A format template
* args (`Any`) — Formatting substitutions

Substitute the specifiers in `fmt` with `args` and print the result to `stdout`.

Example usage:

```js
// expected output: A formatted string
Format.printf('A %s %s', 'formatted', 'string');
```

### Format.vprintf(fmt, args)

> Deprecated: Prefer [template literals][template-literals] when possible

Type:
* Static

Parameters:
* fmt (`String`) — A format template
* args (`Array(Any)`) — Formatting substitutions

Returns:
* (`String`) — A new formatted string

Substitute the specifiers in `fmt` with `args` and return a new string. It
supports the `%s`, `%d`, `%x` and `%f` specifiers.

For `%f` it also supports precisions like `vprintf('%.2f', [1.526])`. All
specifiers can be prefixed with a minimum field width (e.g.
`vprintf('%5s', ['foo'])`). Unless the width is prefixed with `'0'`, the
formatted string will be padded with spaces.

Example usage:

```js
// expected result: "A formatted string"
const str = Format.vprintf('A %s %s', ['formatted', 'string']);

// Usage with Gettext
Format.vprintf(_('%d:%d'), [11, 59]);
Format.vprintf(
    Gettext.ngettext('I have %d apple', 'I have %d apples', num), [num]);
```

[template-literals]: https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Template_literals

