# Legacy Imports

Prior to the introduction of [ES Modules](ESModules.md), GJS had its own import system.

**imports** is a global object that you can use to import any js file or GObject
Introspection lib as module, there are 4 special properties of **imports**:

 * `searchPath`

    An array of path that used to look for files, if you want to prepend a path
    you can do something like `imports.searchPath.unshift(myPath)`.

 * `__modulePath__`
 * `__moduleName__`
 * `__parentModule__`

    These 3 properties is intended to be used internally, you should not use them.

Any other properties of **imports** is treated as a module, if you access these
properties, an import is attempted. Gjs try to look up a js file or directory by property name
from each location in `imports.searchPath`. For `imports.foo`, if a file named
`foo.js` is found, this file is executed and then imported as a module object; else if
a directory `foo` is found, a new importer object is returned and its `searchPath` property
is replaced by the path of `foo`.

Note that any variable, function and class declared at the top level,
except those declared by `let` or `const`, are exported as properties of the module object,
and one js file is executed only once at most even if it is imported multiple times.
