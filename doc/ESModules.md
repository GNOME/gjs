# ECMAScript Modules

> _This documentation is inspired by [Node.js' documentation](https://github.com/nodejs/node/blob/HEAD/doc/api/esm.md)
> on ECMAScript modules._

ECMAScript Modules or "ES modules" are the [official ECMAScript
standard][] for importing, exporting, and reusing JavaScript code.

ES modules can export `function`, `class`, `const`, `let`, and `var`
statements using the `export` keyword.

```js
// animalSounds.js
export function bark(num) {
  log('bark');
}

export const ANIMALS = ['dog', 'cat'];
```

Other ES modules can then import those declarations using `import`
statements like the one below.

```js
// main.js
import { ANIMALS, bark } from './animalSounds.js';

// Logs 'bark'
bark();

// Logs 'dog, cat'
log(ANIMALS);
```

## Loading ES Modules

### Command Line

From the command line ES modules can be loaded with the `-m` flag:

```sh
gjs -m module.js
```

### JavaScript

ES modules cannot be loaded from strings at this time.

Besides the import expression syntax described above, Dynamic [`import()` statements][] can be used to load modules from any GJS script or module.

```js
import('./animalSounds.js').then((module) => {
    // module.default is the default export
    // named exports are accessible as properties
    // module.bark
}).catch(logError)
```

Because `import()` is asynchronous, you will need a mainloop running.

### C API

Using the C API in `gjs.h`, ES modules can be loaded from a file or
resource using `gjs_load_module_file()`. <!-- TODO -->

### Shebang

`example.js`

```js
#!/usr/bin/env -S gjs -m

import GLib from 'gi://GLib';
log(GLib);
```

```sh
chmod +x example.js
./example.js
```


## `import` Specifiers

### Terminology

The _specifier_ of an `import` statement is the string after the `from`
keyword, e.g. `'path'` in `import { sep } from 'path'`.
Specifiers are also used in `export from` statements, and as the
argument to an `import()` expression.

There are three types of specifiers:

* _Relative specifiers_ like `'./window.js'`.
  They refer to a path relative to the location of the importing file.
  _The file extension is always necessary for these._

* _Bare specifiers_ like `'some-package'`.
  In GJS bare specifiers typically refer to built-in modules like `gi`.

* _Absolute specifiers_ like `'file:///usr/share/gjs-app/file.js'`.
  They refer directly and explicitly to a full path or library.

Bare specifier resolutions import built-in modules.
All other specifier resolutions are always only resolved with the
standard relative URL resolution semantics.

### Mandatory file extensions

A file extension must be provided when using the `import` keyword to
resolve relative or absolute specifiers.
Directory files (e.g. `'./extensions/__init__.js'`) must also be fully
specified.

The recommended replacement for directory files (`__init__.js`) is:

```js
'./extensions.js'
'./extensions/a.js'
'./extensions/b.js'
```

Because file extensions are required, folders and `.js` files with the
same "name" should not conflict as they did with `imports`.

### URLs

ES modules are resolved and cached as URLs.
This means that files containing special characters such as `#` and `?`
need to be escaped.

`file:`, `resource:`, and `gi:` URL schemes are supported.
A specifier like `'https://example.com/app.js'` is not supported in GJS.

#### `file:` URLs

Modules are loaded multiple times if the `import` specifier used to
resolve them has a different query or fragment.

```js
import './foo.js?query=1'; // loads ./foo.js with query of "?query=1"
import './foo.js?query=2'; // loads ./foo.js with query of "?query=2"
```

The root directory may be referenced via `file:///`.

#### `gi:` Imports

`gi:` URLs are supported as an alternative means to load GI (GObject
Introspected) modules.

`gi:` URLs support declaring libraries' versions.
An error will be thrown when resolving imports if multiple versions of a
library are present and a version has not been specified.
The version is cached, so it only needs to be specified once.

```js
import Gtk from 'gi://Gtk?version=4.0';
import Gdk from 'gi://Gdk?version=4.0';
import GLib from 'gi://GLib';
// GLib, GObject, and Gio are required by GJS so no version is necessary.
```

It is recommended to create a "version block" at your application's
entry point.

```js
import 'gi://Gtk?version=3.0'
import 'gi://Gdk?version=3.0'
import 'gi://Hdy?version=1.0'
```

After these declarations, you can import the libraries without version
parameters.

```js
import Gtk from 'gi://Gtk';
import Gdk from 'gi://Gdk';
import Hdy from 'gi://Hdy';
```

## Built-in modules

Built-in modules provide a default export with all their exported functions and properties. Most modules provide named exports too. `cairo` does not provide named exports of its API.

Modifying the values of the default export _does not_ change the values of named exports.

```js
import system from 'system';
system.exit(1);
```

```js
import { ngettext as _ } from 'gettext';
_('Hello!');
```

## `import.meta`

* {Object}

The `import.meta` meta property is an `Object` that contains the
following properties:

### `import.meta.url`

* {string} The absolute `file:` or `resource:` URL of the module.

This is identical to Node.js and browser environments.
It will always provide the URI of the current module.

This enables useful patterns such as relative file loading:

```js
import Gio from 'gi://Gio';
const file = Gio.File.new_for_uri(import.meta.url);
const data = file.get_parent().resolve_relative_path('data.json');
const [, contents] = data.load_contents(null);
```

or if you want the path for the current file or directory

```js
import GLib from 'gi://GLib';
const [filename] = GLib.filename_from_uri(import.meta.url);
const dirname = GLib.path_get_dirname(filename);
```

## Interoperability with legacy `imports` modules

Because `imports` is a global object, it is still available in ES
modules.
It is not recommended to purposely mix import styles unless absolutely
necessary.

### `import` statements

An `import` statement can only reference an ES module.
`import` statements are permitted only in ES modules, but dynamic
[`import()`][] expressions is supported in legacy `imports` modules
for loading ES modules.

When importing legacy `imports` modules, all `var` declarations are
provided as properties on the default export.

### Differences between ES modules and legacy `imports` modules

#### No `imports` and `var` exports

You must use the [`export`][] syntax instead.

#### No meta path properties

These `imports` properties are not available in ES modules:

 * `__modulePath__`
 * `__moduleName__`
 * `__parentModule__`

`__modulePath__`, `__moduleName__` and `__parentModule__` use cases can
be replaced with [`import.meta.url`][].

[`export`]: https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Statements/export
[`import()`]: #esm_import_expressions
[`import()` statements]: https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Statements/import#dynamic_imports
[`import.meta.url`]: #esm_import_meta_url
[`import`]: https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Statements/import
[`string`]: https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/String
[special scheme]: https://url.spec.whatwg.org/#special-scheme
[official ECMAScript standard]: https://tc39.github.io/ecma262/#sec-modules
