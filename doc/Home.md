# GJS: Javascript Bindings for GNOME

The current stable series is built on Mozilla's SpiderMonkey 78,
featuring **ECMAScript 2019** and GObject Introspection making most of
the **GNOME platform API** available.

To find out when a language feature was implemented in GJS, review [NEWS][gjs-news] in the GitLab repository. In many cases older versions of GJS can be supported using [polyfills][mdn-polyfills] and [legacy-style GJS classes](Modules.md#lang).

GJS includes some built-in modules like Cairo and Gettext, as well as helpers for some core APIs like DBus and GVariants. See the [Modules](Modules.md) page for an overview of the built-in modules and their usage.

[gjs-news]: https://gitlab.gnome.org/GNOME/gjs/raw/HEAD/NEWS
[mdn-polyfills]: https://developer.mozilla.org/docs/Glossary/Polyfill

## GNOME API Documentation

There is official [GNOME API Documentation][gjs-docs] for GJS, including
everything from GLib and Gtk to Soup and WebKit2.

The [Mapping](Mapping.md) page has an overview of GNOME API usage in GJS such as subclassing, constants and flags, functions with multiple return values, and more.

There are also a growing number of [examples][gjs-examples] and thorough tests of language features in the [test suite][gjs-tests].

[gjs-docs]: https://gjs-docs.gnome.org/
[gjs-examples]: https://gitlab.gnome.org/GNOME/gjs/tree/HEAD/examples
[gjs-tests]: https://gitlab.gnome.org/GNOME/gjs/blob/HEAD/installed-tests/js

## Standalone Applications

It's possible to write standalone applications with GJS for the GNOME Desktop, and infrastructure for Gettext, GSettings and GResources via the `package` import. There is a package specification, template repository available and plans for an in depth tutorial.

* [GJS Package Specification](https://wiki.gnome.org/Projects/Gjs/Package/Specification.md)
* [GJS Package Template](https://github.com/gcampax/gtk-js-app)

GNOME Applications written in GJS:

* [GNOME Characters](https://gitlab.gnome.org/GNOME/gnome-characters)
* [GNOME Documents](https://gitlab.gnome.org/GNOME/gnome-documents)
* [GNOME Maps](https://gitlab.gnome.org/GNOME/gnome-maps)
* [GNOME Sound Recorder](https://gitlab.gnome.org/GNOME/gnome-sound-recorder)
* [GNOME Weather](https://gitlab.gnome.org/GNOME/gnome-weather)
* [GNOME Books](https://gitlab.gnome.org/GNOME/gnome-books)
* [Polari](https://gitlab.gnome.org/GNOME/polari) IRC Client

Third party applications written in GJS:

* [Tangram](https://github.com/sonnyp/Tangram)
* [Quick Lookup](https://github.com/johnfactotum/quick-lookup)
* [Foliate](https://github.com/johnfactotum/foliate)
* [Marker](https://github.com/fabiocolacio/Marker)
* [Gnomit](https://github.com/small-tech/gnomit)
* [Clapper](https://github.com/Rafostar/clapper/)
* [Flatseal](https://github.com/tchx84/Flatseal)
* [Almond](https://github.com/stanford-oval/almond-gnome/)
* [Commit](https://github.com/sonnyp/commit/)

## Getting Help

* Discourse: https://discourse.gnome.org/
* Chat: https://matrix.to/#/#javascript:gnome.org
* Issue/Bug Tracker: https://gitlab.gnome.org/GNOME/gjs/issues
* StackOverflow: https://stackoverflow.com/questions/tagged/gjs

## External Links

* [GObjectIntrospection](https://wiki.gnome.org/action/show/Projects/GObjectIntrospection)
* [GNOME Developer Platform Demo](https://developer.gnome.org/gnome-devel-demos/stable/js.html) (Some older examples that still might be informative)
* [Writing GNOME Shell Extensions](https://wiki.gnome.org/Projects/GNOMEShell/Extensions/Writing)
