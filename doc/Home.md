# GJS: Javascript Bindings for Gnome

The current stable series (1.58.x) is built on Mozilla's SpiderMonkey 60 featuring **ES6 (ECMAScript 2015)** and GObjectIntrospection making most of the **Gnome API library** available.

To find out when a language feature was implemented in GJS, review [NEWS][gjs-news] in the GitLab repository. In many cases older versions of GJS can be supported using [polyfills][mdn-polyfills] and [legacy-style GJS classes](Modules.md#lang).

GJS includes some built-in modules like Cairo and Gettext, as well as helpers for some core APIs like DBus and GVariants. See the [Modules](Modules.md) page for an overview of the built-in modules and their usage.

[gjs-news]: https://gitlab.gnome.org/GNOME/gjs/raw/master/NEWS
[mdn-polyfills]: https://developer.mozilla.org/docs/Glossary/Polyfill

## Gnome API Documentation

There is now official [Gnome API Documentation][gjs-docs] for GJS, including everything from GLib and Gtk to Soup and WebKit2.

The [Mapping](Mapping.md) page has an overview of Gnome API usage in GJS such as subclassing, constants and flags, functions with multiple return values, and more.

There are also a growing number of [examples][gjs-examples] and thorough tests of language features in the [test suite][gjs-tests].

[gjs-docs]: https://gjs-docs.gnome.org/
[gjs-examples]: https://gitlab.gnome.org/GNOME/gjs/tree/master/examples
[gjs-tests]: https://gitlab.gnome.org/GNOME/gjs/blob/master/installed-tests/js



## Standalone Applications

It's possible to write standalone applications with GJS for the Gnome Desktop, and infrastructure for Gettext, GSettings and GResources via the `package` import. There is a package specification, template repository available and plans for an in depth tutorial.

* [GJS Package Specification](https://wiki.gnome.org/Projects/Gjs/Package/Specification.md)
* [GJS Package Template](https://github.com/gcampax/gtk-js-app)

Gnome Applications written in GJS:

* [Gnome Characters](https://gitlab.gnome.org/GNOME/gnome-characters)
* [Gnome Documents](https://gitlab.gnome.org/GNOME/gnome-documents)
* [Gnome Maps](https://gitlab.gnome.org/GNOME/gnome-maps)
* [Gnome Sound Recorder](https://gitlab.gnome.org/GNOME/gnome-sound-recorder)
* [Gnome Weather](https://gitlab.gnome.org/GNOME/gnome-weather)
* [Polari](https://gitlab.gnome.org/GNOME/polari) IRC Client

## Getting Help

* Mailing List: http://mail.gnome.org/mailman/listinfo/javascript-list
* IRC: irc://irc.gnome.org/#javascript
* Issue/Bug Tracker: https://gitlab.gnome.org/GNOME/gjs/issues
* StackOverflow: https://stackoverflow.com/questions/tagged/gjs

## External Links

* [GObjectIntrospection](https://wiki.gnome.org/action/show/Projects/GObjectIntrospection)
* [Gnome Developer Platform Demo](https://developer.gnome.org/gnome-devel-demos/stable/js.html) (Some older examples that still might be informative)
* [Writing GNOME Shell Extensions](https://wiki.gnome.org/Projects/GnomeShell/Extensions/Writing)