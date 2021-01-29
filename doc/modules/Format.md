# [Format](https://gitlab.gnome.org/GNOME/gjs/blob/master/modules/esm/format.js)

**Import with `import format from 'format';`**

The format module is mostly obsolete, providing only `sprintf()`. Native [template literals][template-literals] should be preferred now, except in few situations like Gettext (See [Bug #50920][bug-50920]).
We plan to increase support for template literals in translations so `sprintf()` is no longer necessary.

```js
import { sprintf } from 'format';

// Using sprintf() (Output: Pi to 2 decimal points: 3.14)
sprintf("%s to %d decimal points: %.2f", foo, bar*2, baz);

// Using sprintf() with Gettext
sprintf(_("%d:%d"), 11, 59);

let foo = "Pi";
let bar = 1;
let baz = Math.PI;

// Using native template literals (Output: Pi to 2 decimal points: 3.14)
`${foo} to ${bar*2} decimal points: ${baz.toFixed(bar*2)}`
```

## [Deprecated Functions](https://gitlab.gnome.org/GNOME/gjs/blob/master/modules/script/format.js)

**Import with `const Format = imports.format;`**

```js
// Applying format() to the string prototype
const Format = imports.format;
String.prototype.format = Format.format;

// Using format() (Output: Pi to 2 decimal points: 3.14)
"%s to %d decimal points: %.2f".format(foo, bar*2, baz);

// Using format() with Gettext
_("%d:%d").format(11, 59);
Gettext.ngettext("I have %d apple", "I have %d apples", num).format(num);
```

[template-literals]: https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Template_literals
[bug-50920]: https://savannah.gnu.org/bugs/?50920
