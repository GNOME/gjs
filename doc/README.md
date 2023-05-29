# GJS

GJS is JavaScript bindings for the GNOME platform APIs. Powered by
Mozilla's [SpiderMonkey][spidermonkey] JavaScript engine and
[GObject Introspection][gobject-introspection], it opens the entire GNOME
ecosystem to JavaScript developers.

The stable version of GJS is based on the latest Extended Support Release (ESR)
of SpiderMonkey. To find out when a language feature was added to GJS, review
[NEWS][gjs-news] in the GitLab repository.

[gobject-introspection]: https://gi.readthedocs.io
[spidermonkey]: https://spidermonkey.dev/
[gjs-news]: https://gitlab.gnome.org/GNOME/gjs/raw/HEAD/NEWS

## Documentation

If you are reading this file in the GJS repository, you may find it more
convenient to browse and search using the [API Documentation][gjs-docs] website
instead. There is documentation for GLib, GTK, Adwaita, WebKit, and many more
libraries. General documentation about built-in modules and APIs is under the
[GJS Topic](https://gjs-docs.gnome.org/gjs).

[GJS Guide][gjs-guide] has many in-depth tutorials and examples for a number of
core GNOME APIs. The repository also has [code examples][gjs-examples] and
thorough coverage of language features in the [test suite][gjs-tests].

[GTK4 + GJS Book][gtk4-gjs-book] is a start to finish
walkthrough for creating GTK4 applications with GJS.

The [GNOME developer portal][gnome-developer] contains examples of a variety of
GNOME technologies written GJS, alongside other languages you may know.

[Workbench] is a code sandbox for GJS, CSS and GTK.
It features live preview and a library of examples and demos.

[gjs-docs]: https://gjs-docs.gnome.org/
[gjs-examples]: https://gitlab.gnome.org/GNOME/gjs/tree/HEAD/examples
[gjs-tests]: https://gitlab.gnome.org/GNOME/gjs/blob/HEAD/installed-tests/js
[gjs-guide]: https://gjs.guide
[gtk4-gjs-book]: https://rmnvgr.gitlab.io/gtk4-gjs-book/
[gnome-developer]: https://developer.gnome.org/
[workbench]: https://apps.gnome.org/app/re.sonny.Workbench/

## Applications

GJS is a great option to write applications for the GNOME Desktop.

The easiest way to get started is to use [GNOME Builder][gnome-builder], start a
new project and select `JavaScript` language.

[gnome-builder]: https://apps.gnome.org/app/org.gnome.Builder/

Here is a non-exhaustive list of applications written in GJS:

GNOME Apps

* [Characters](https://gitlab.gnome.org/GNOME/gnome-characters)
* [Maps](https://gitlab.gnome.org/GNOME/gnome-maps)
* [Weather](https://gitlab.gnome.org/GNOME/gnome-weather)
* [Extensions](https://gitlab.gnome.org/GNOME/gnome-shell/-/tree/HEAD/subprojects/extensions-app)
* [Polari](https://gitlab.gnome.org/GNOME/polari)
* [Tangram](https://github.com/sonnyp/Tangram)
* [Flatseal](https://github.com/tchx84/Flatseal)
* [Commit](https://github.com/sonnyp/commit/)
* [Junction](https://github.com/sonnyp/Junction)
* [Oh My SVG](https://github.com/sonnyp/OhMySVG)
* [Workbench](https://github.com/sonnyp/Workbench)
* [GNOME Sound Recorder](https://gitlab.gnome.org/GNOME/gnome-sound-recorder) (TypeScript)
* [Zap](https://apps.gnome.org/app/fr.romainvigier.zap/)

Others

* [Quick Lookup](https://github.com/johnfactotum/quick-lookup)
* [Foliate](https://github.com/johnfactotum/foliate)
* [Clapper](https://github.com/Rafostar/clapper/)
* [Almond](https://github.com/stanford-oval/almond-gnome/)
* [Lobjur](https://github.com/ranfdev/Lobjur) (Clojure)
* [Touché](https://github.com/JoseExposito/touche)
* [Annex](https://github.com/andyholmes/annex)
* [Bolso](https://github.com/felipeborges/bolso)
* [Design](https://github.com/dubstar-04/Design)
* [Capsule](https://gitlab.gnome.org/verdre/Capsule)
* [Spiel](https://gitlab.gnome.org/feaneron/spiel)
* [Retro](https://github.com/sonnyp/Retro)
* [libportal test](https://github.com/flatpak/libportal/tree/main/portal-test/gtk4)
* [Sticky](https://github.com/vixalien/sticky)
* [Playhouse](https://github.com/sonnyp/Playhouse)
* [Flatpak Manifest Editor](https://gitlab.gnome.org/feaneron/flatpak-manifest-editor)
* [Forge Sparks](https://github.com/rafaelmardojai/forge-sparks)
* [Diccionario de la Lengua](https://codeberg.org/rafaelmardojai/diccionario-lengua)

Archived

* [GNOME Books](https://gitlab.gnome.org/GNOME/gnome-books)
* [GNOME Documents](https://gitlab.gnome.org/GNOME/gnome-documents)

### GNOME Shell Extensions

GJS is used to write [GNOME Shell Extensions](https://extensions.gnome.org),
allowing anyone to make considerable modifications to the GNOME desktop. This
can also be a convenient way to prototype changes you may want to contribute to
the upstream GNOME Shell project.

There is documentation and tutorials specifically for extension authors at
[gjs.guide/extensions](https://gjs.guide/extensions).

### Embedding GJS

GJS can also be embedded in other applications, such as with GNOME Shell, to
provide a powerful scripting language with support for the full range of
libraries with GObject-Introspection.

## Getting Help

* Discourse: https://discourse.gnome.org/
* Chat: https://matrix.to/#/#javascript:gnome.org
* Issue Tracker: https://gitlab.gnome.org/GNOME/gjs/issues
* StackOverflow: https://stackoverflow.com/questions/tagged/gjs

