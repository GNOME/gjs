# Deprecated Modules

To preserve backwards compatibility GJS still bundles several **deprecated** modules which should not be used in new code.

## [jsUnit](https://gitlab.gnome.org/GNOME/gjs/blob/master/modules/script/jsUnit.js)

**DEPRECATED:  `const jsUnit = imports.jsUnit;`**

Deprecated unit test functions. [Jasmine][jasmine-gjs] for GJS should now be preferred, as demonstrated in the GJS [test suite][gjs-tests].

[jasmine-gjs]: https://github.com/ptomato/jasmine-gjs
[gjs-tests]: https://gitlab.gnome.org/GNOME/gjs/blob/master/installed-tests/js

## [`Lang`](https://gitlab.gnome.org/GNOME/gjs/blob/master/modules/script/lang.js)

**DEPRECATED:  `const Lang = imports.lang;`**

Lang is an obsolete library, it is only necessary for outdated and unsupported versions of GJS. It implements a variety of utilities which predate modern JavaScript features such as classes and arrow functions:

```js
const Lang = imports.lang;
const FnorbLib = imports.fborbLib;

const MyLegacyClass = new Lang.Class({
    _init: function() {
        let fnorb = new FnorbLib.Fnorb();
        fnorb.connect('frobate', Lang.bind(this, this._onFnorbFrobate));
    },

    _onFnorbFrobate: function(fnorb) {
        this._updateFnorb();
    }
});

var MyNewClass = class {
    constructor() {
        let fnorb = new FnorbLib.Fnorb();
        fnorb.connect('frobate', fnorb => this._onFnorbFrobate);
    }

    _onFnorbFrobate(fnorb) {
        this._updateFnorb();
    }
}
```

## [Tweener](https://gitlab.gnome.org/GNOME/gjs/blob/master/modules/script/tweener/)

**DEPRECATED: `const Tweener = imports.tweener.tweener;`**

Built-in version of the [Tweener][tweener-www] animation/property transition library. Tweener has been superseded in GNOME Shell and applications by animation and transition support in GTK and Clutter.

[tweener-www]: http://hosted.zeh.com.br/tweener/docs/
