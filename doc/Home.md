# GJS: Javascript Bindings for GNOME

The current stable series is built on Mozilla's SpiderMonkey 91,
featuring **ECMAScript 2022** and GObject Introspection making most of
the **GNOME platform API** available.

To find out when a language feature was implemented in GJS, review [NEWS][gjs-news] in the GitLab repository. In many cases older versions of GJS can be supported using [polyfills][mdn-polyfills] and [legacy-style GJS classes](Modules.md#lang).

GJS includes some built-in modules like Cairo and Gettext, as well as helpers for some core APIs like DBus and GVariants. See the [Modules](Modules.md) page for an overview of the built-in modules and their usage.

[gjs-news]: https://gitlab.gnome.org/GNOME/gjs/raw/HEAD/NEWS
[mdn-polyfills]: https://developer.mozilla.org/docs/Glossary/Polyfill

## Documentation

There is official [GNOME API Documentation][gjs-docs] for GJS, including
everything from GLib and Gtk to Soup and WebKit2.

The [Mapping](Mapping.md) page has an overview of GNOME API usage in GJS such as subclassing, constants and flags, functions with multiple return values, and more.

Additional GJS documentation can be found under [doc](.).

[GJS Guide](gjs-guide) contains tutorials for begineers.

[GTK4 + GJS Book](https://rmnvgr.gitlab.io/gtk4-gjs-book/) is a start to finish walkthrough for the creation of GTK4 + GJS applications.

The main [GNOME developer portal][gnome-developer] contains numerous examples in JavaScript for GJS.

There are also a growing number of [examples][gjs-examples] and thorough tests of language features in the [test suite][gjs-tests].

[gjs-docs]: https://gjs-docs.gnome.org/
[gjs-examples]: https://gitlab.gnome.org/GNOME/gjs/tree/HEAD/examples
[gjs-tests]: https://gitlab.gnome.org/GNOME/gjs/blob/HEAD/installed-tests/js
[gjs-guide]: https://gjs.guide/
[gnome-developer]: https://developer.gnome.org/

## Applications

GJS is a great option to write applications for the GNOME Desktop.

The easiest way to get started is to use [GNOME Builder](gnome-builder), start a new project and select `JavaScript` language.

There is a also a [package specification] and [template repository] available.

[gnome-builder]: https://apps.gnome.org/app/org.gnome.Builder/
[package specification]: https://gitlab.gnome.org/GNOME/gjs/-/blob/HEAD/doc/Package/Specification.md
[template repository]: https://github.com/gcampax/gtk-js-app

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
* [Junction](https://github.com/sonnyp/Junction)
* [Oh My SVG](https://github.com/sonnyp/OhMySVG)
* [Lobjur](https://github.com/ranfdev/Lobjur)
* [Touché](https://github.com/JoseExposito/touche)
* [Annex](https://github.com/andyholmes/annex)
* [Bolso](https://github.com/felipeborges/bolso)
* [Workbench](https://github.com/sonnyp/Workbench)

## Getting Help

* Discourse: https://discourse.gnome.org/
* Chat: https://matrix.to/#/#javascript:gnome.org
* Issue/Bug Tracker: https://gitlab.gnome.org/GNOME/gjs/issues
* StackOverflow: https://stackoverflow.com/questions/tagged/gjs

## External Links

* [GObjectIntrospection](https://wiki.gnome.org/action/show/Projects/GObjectIntrospection)
* [GNOME Developer Platform Demo](https://developer-old.gnome.org/gnome-devel-demos/stable/js.html) (Some older examples that still might be informative)
* [GNOME Shell Extensions](https://gjs.guide/extensions)
