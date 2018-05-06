[![Build Status](https://gitlab.gnome.org/GNOME/gjs/badges/master/build.svg)](https://gitlab.gnome.org/GNOME/gjs/pipelines)
[![coverage report](https://gitlab.gnome.org/GNOME/gjs/badges/master/coverage.svg)](https://gnome.pages.gitlab.gnome.org/gjs/)
[![License](https://img.shields.io/badge/License-LGPL%20v2%2B-blue.svg)](https://gitlab.gnome.org/GNOME/gjs/blob/master/COPYING)
[![License](https://img.shields.io/badge/License-MIT-blue.svg)](https://gitlab.gnome.org/GNOME/gjs/blob/master/COPYING)

JavaScript bindings for GNOME
=============================

It's mainly based on Spidermonkey javascript engine and the GObject introspection framework.

Available as part of your GNOME distribution. Powers GNOME Shell, Polari,
GNOME Documents, and many other apps.

Wiki: https://wiki.gnome.org/action/show/Projects/Gjs

How to build and run if you want to contribute to GJS: see doc/Hacking.md

#### Testing

Our CI (continuous integration) testing scheme stresses the source code using:
- Fedora 27, Ubuntu 18.04, Fedora 29 (devel), and Ubuntu 18.10 (devel);
- gcc 7.3 and gcc 8.0;
- clang 6.0;
- C/C++ and Javascript Linters;
- Code Climate Engine (https://codeclimate.com/);
- ASAN (address sanitizer) and UBSAN (undefined behavior sanitizer);
- Valgrind (https://en.wikipedia.org/wiki/Valgrind);
- Code Coverage (https://en.wikipedia.org/wiki/Code_coverage);
- Text only and graphics builds;
- Profiler enabled and disabled builds;
- And DevOps with Flatpak.

## License

Dual licensed under LGPL 2.0+ and MIT.
