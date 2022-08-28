# Gettext

> See also: [`examples/gettext.js`][examples-gettext] for usage examples

This module provides a convenience layer for the "gettext" family of functions,
relying on GLib for the actual implementation.

Example usage:

```js
const Gettext = imports.gettext;

Gettext.textdomain('myapp');
Gettext.bindtextdomain('myapp', '/usr/share/locale');

let translated = Gettext.gettext('Hello world!');
```

#### Import

When using ESModules:

```js
import Gettext from 'gettext';
```

When using legacy imports:

```js
const Gettext = imports.gettext;
```

[examples-gettext]: https://gitlab.gnome.org/GNOME/gjs/blob/HEAD/examples/gettext.js

### Gettext.LocaleCategory

An enumeration of locale categories supported by GJS.

* `CTYPE = 0` — Character classification
* `NUMERIC = 1` — Formatting of nonmonetary numeric values
* `TIME = 2` — Formatting of date and time values
* `COLLATE = 3` — String collation
* `MONETARY = 4` — Formatting of monetary values
* `MESSAGES = 5` — Localizable natural-language messages
* `ALL = 6` — All of the locale

### Gettext.setlocale(category, locale)

> Note: It is rarely, if ever, necessary to call this function in GJS

Parameters:
* category (`Gettext.LocaleCategory`) — A locale category
* locale (`String`|`null`) — A locale string, or `null` to query the locale

Returns:
* (`String`|`null`) — A locale string, or `null` if `locale` is not `null`

Set or query the program's current locale.

Example usage:

```js
Gettext.setlocale(Gettext.LocaleCategory.MESSAGES, 'en_US.UTF-8');
```

### Gettext.textdomain(domainName)

Parameters:
* domainName (`String`) — A translation domain

Set the default domain to `domainName`, which is used in all future gettext
calls. Note that this does not affect functions that take an explicit
`domainName` argument, such as `Gettext.dgettext()`.

Typically this will be the project name or another unique identifier. For
example, GNOME Calculator might use something like `"gnome-calculator"` while a
GNOME Shell Extension might use its extension UUID.

### Gettext.bindtextdomain(domainName, dirName)

Parameters:
* domainName (`String`) — A translation domain
* dirName (`String`) — A directory path

Specify `dirName` as the directory that contains translations for `domainName`.

In most cases, `dirName` will be the system locale directory, such as
`/usr/share/locale`. GNOME Shell's `ExtensionUtils.initTranslations()` method,
on the other hand, will check an extension's directory for a `locale`
subdirectory before falling back to the system locale directory.

### Gettext.gettext(msgid)

> Note: This is equivalent to calling `Gettext.dgettext(null, msgid)`

Parameters:
* msgid (`String`) — A string to translate

Returns:
* (`String`) — A translated message

This function is a wrapper of `dgettext()` which does not translate the message
if the default domain as set with `Gettext.textdomain()` has no translations for
the current locale.

### Gettext.dgettext(domainName, msgid)

> Note: This is an alias for [`GLib.dgettext()`][gdgettext]

Parameters:
* domainName (`String`|`null`) — A translation domain
* msgid (`String`) — A string to translate

Returns:
* (`String`) — A translated message

This function is a wrapper of `dgettext()` which does not translate the message
if the default domain as set with `Gettext.textdomain()` has no translations for
the current locale.

[gdgettext]: https://gjs-docs.gnome.org/glib20/glib.dgettext

### Gettext.dcgettext(domainName, msgid, category)

> Note: This is an alias for [`GLib.dcgettext()`][gdcgettext]

Parameters:
* domainName (`String`|`null`) — A translation domain
* msgid (`String`) — A string to translate
* category (`Gettext.LocaleCategory`) — A locale category

Returns:
* (`String`) — A translated message

This is a variant of `Gettext.dgettext()` that allows specifying a locale
category.

[gdcgettext]: https://gjs-docs.gnome.org/glib20/glib.dcgettext

### Gettext.ngettext(msgid1, msgid2, n)

> Note: This is equivalent to calling
> `Gettext.dngettext(null, msgid1, msgid2, n)`

Parameters:
* msgid1 (`String`) — The singular form of the string to be translated
* msgid2 (`String`) — The plural form of the string to be translated
* n (`Number`) — The number determining the translation form to use

Returns:
* (`String`) — A translated message

Translate a string that may or may not be plural, like "I have 1 apple" and
"I have 2 apples".

In GJS, this should be used in conjunction with [`Format.vprintf()`][vprintf],
which supports the same substitutions as `printf()`:

```js
const numberOfApples = Math.round(Math.random() + 1);
const translated = Format.vprintf(Gettext.ngettext('I have %d apple',
    'I have %d apples', numberOfApples), [numberOfApples]);
```

[vprintf]: https://gjs-docs.gnome.org/gjs/format.md#format-vprintf

### Gettext.dngettext(domainName, msgid1, msgid2, n)

> Note: This is an alias for [`GLib.dngettext()`][gdngettext]

Parameters:
* domainName (`String`|`null`) — A translation domain
* msgid1 (`String`) — A string to translate
* msgid2 (`String`) — A pluralized string to translate
* n (`Number`) — The number determining the translation form to use

Returns:
* (`String`) — A translated message

This function is a wrapper of `dngettext()` which does not translate the message
if the default domain as set with `textdomain()` has no translations for the
current locale.

[gdngettext]: https://gjs-docs.gnome.org/glib20/glib.dngettext

### Gettext.pgettext(context, msgid)

> Note: This is equivalent to calling `Gettext.dpgettext(null, context, msgid)`

Parameters:
* context (`String`|`null`) — A context to disambiguate `msgid`
* msgid (`String`) — A string to translate

Returns:
* (`String`) — A translated message

This is a variant of `Gettext.dgettext()` which supports a disambiguating
message context.

This is used to disambiguate a translation where the same word may be used
differently, depending on the situation. For example, in English "read" is the
same for both past and present tense, but may not be in other languages.

### Gettext.dpgettext(domainName, context, msgid)

> Note: This is an alias for [`GLib.dpgettext2()`][gdpgettext2]

Parameters:
* domainName (`String`|`null`) — A translation domain
* context (`String`|`null`) — A context to disambiguate `msgid`
* msgid (`String`) — A string to translate

Returns:
* (`String`) — A translated message

This is a variant of `Gettext.dgettext()` which supports a disambiguating
message context.

[gdpgettext2]: https://gjs-docs.gnome.org/glib20/glib.dpgettext2

### Gettext.domain(domainName)

> Note: This method is specific to GJS

Parameters:
* domainName (`String`) — A domain name

Returns:
* (`Object`) — An object with common gettext methods

Create an object with bindings for `Gettext.gettext()`, `Gettext.ngettext()`,
and `Gettext.pgettext()`, bound to a `domainName`.

