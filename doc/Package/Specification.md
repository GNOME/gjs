This document aims to build a set of conventions for JS applications using GJS and GObjectIntrospection.

## Rationale

It is believed that the current deployment facilities for GJS apps, ie autotools, [bash wrapper scripts](https://git.gnome.org/browse/gnome-documents/tree/src/gnome-documents.in) and [sed invocations](https://git.gnome.org/browse/gnome-documents/tree/src/Makefile.am#n26) represent a huge obstacle in making the GJS application platform palatable for newcomers. Additionally, the lack of standardization on the build system hinders the diffusion of pure JS utility / convenience modules.

The goal is to create a standard packaging method for GJS, similar to Python's .

The choice of keeping the autotools stems from the desire of integration with GNOME submodules such as libgd and egg-list-box. While those are temporary and will enter GTK in due time, it is still worthy for free software applications to be able to share submodules easily.

Moreover, so far the autotools have the best support for generating GObjectIntrospection information, and it is sometimes necessary for JS apps to use a private helper library in a compiled language.

## Requirements

* Implementation details, whenever exposed to the app developers because of limitations of the underlying tools, must be copy-pastable between packages.
* The application must be fully functional when run uninstalled. In particular, it must not fail because it lacks GtkBuilder files, images, CSS or GSettings.
* The application must honor `--prefix` and `--libdir` (which must be a subdirectory of `--prefix`) at configure time.
* The application must not require more than `--prefix` and `--libdir` to work.
* The application must be installable by a regular user, provided he has write permission in `--prefix`
* The format must allow the application to be comprised of one or more JS entry points, and one or more introspection based libraries

## Prior Art

* [setuptools](https://pypi.python.org/pypi/setuptools) and [distutils-extra](https://launchpad.net/python-distutils-extra) (for Python)
 * [Ubuntu Quickly](https://wiki.ubuntu.com/Quickly|Ubuntu Quickly) (again, for Python)
 * [CommonJS package format](http://wiki.commonjs.org/wiki/Packages) (only describes the package layout, and does not provide runtime services)
 * https://live.gnome.org/BuilDj (build system only)

## Specification

The following meta variable are used throughout this document:

* **${package-name}**: the fully qualified ID of the package, in DBus name format. Example: org.gnome.Weather.
* **${entry-point-name}**: the fully qualified ID of an entry point, in DBus name format. Example: org.gnome.Weather.Application. This must be a sub ID of **${package-name}**
* **${entry-point-path}**: the entry point ID, converted to a DBus path in the same way GApplication does it (prepend /, replace . with /)
* **${package-tarname}**: the short, but unambiguous, short name of the package, such as gnome-weather
* **${package-version}**: the version of the package

This specification is an addition to the Gjs style guide, and it inherits all requirements.

## Package layout

* The application package is expected to use autotools, or a compatible build system. In particular, it must optionally support recursive configure and recursive make.
* The following directories and files in the toplevel package must exist:

    * **src/**: contains JS modules
    * **src/${entry-point-name}.src.gresource.xml**: the GResource XML for JS files for the named entry point (see below)
    * **src/${entry-point-name}.src.gresource**: the compiled GResource for JS files
    * **data/**: contains misc application data (CSS, GtkBuilder definitions, images...)

    * **data/${entry-point-name}.desktop**: contains the primary desktop file for the application
    * *(OPTIONAL)* **data/${entry-point-name}.data.gresource**: contains the primary application resource
    * *(OPTIONAL)* **data/${entry-point-name}.gschema.xml**: contains the primary GSettings schema
    * *(OPTIONAL)* **data/gschemas.compiled**: compiled version of GSettings schemas in data/, for uninstalled use
    * *(OPTIONAL)* **lib/**: contains sources and .la files of private shared libraries
    * *(OPTIONAL)* **lib/.libs**: contains the compiled (.so) version of private libraries
    * *(OPTIONAL)* another toplevel directory such as libgd or egg-list-box: same as lib/, but for shared submodules
    * **po/**: contains intltool PO files and templates; the translation domain must be ${package-name}

* The package must be installed as following:
    * **${datadir}** must be configured as **${prefix}/share**
    * Arch-independent private data (CSS, GtkBuilder, GResource) must be installed in **${datadir}/${package-name}**, aka **${pkgdatadir}**

    * Source files must be compiled in a GResource with path **${entry-point-path}/js**, in a bundle called **${entry-point-name}.src.gresource** installed in **${pkgdatadir}**
    * Private libraries must be **${libdir}/${package-name}**, aka ${pkglibdir}
    * Typelib for private libraries must be in **${pkglibdir}/girepository-1.0**
    * Translations must be in **${datadir}/locale/**
    * Other files (launches, GSettings schemas, icons, etc) must be in their specified locations, relative to **${prefix}** and **${datadir}**

## Usage

Applications complying with this specification will have one application script, installed in **${prefix}/share/${package-name}** (aka **${pkgdatadir}**), and named as **${entry-point-name}**, without any extension or mangling.

Optionally, one or more symlinks will be placed in ${bindir}, pointing to the appropriate script in ${pkgdatadir} and named in a fashion more suitable for command line usage (usually ${package-tarname}). Alternatively, a script that calls "gapplication launch ${package-name}" can be used.

The application itself will be DBus activated from a script called **src/${entry-point-name}**, generated from configure substitution of the following **${entry-point-name}.in**:

```sh
#!@GJS@
imports.package.init({ name: "${package-name}", version: "@PACKAGE_VERSION@", prefix: "@prefix@" });
imports.package.run(${main-module})
```

Where **${main-module}** is a module containing the `main()` function that will be invoked to start the process. This function should accept a single argument, an array of command line args. The first element in the array will be the full resolved path to the entry point itself (unlike the global ARGV variable for gjs). Also unlike ARGV, it is safe to modify this array.

This `main()` function should initialize a GApplication whose id is **${entry-point-name}**, and do all the work inside the GApplication `vfunc_*` handlers.

> **`[!]`** Users should refer to https://github.com/gcampax/gtk-js-app for a full example of the build environment.

## Runtime support

The following API will be available to applications, through the [`package.js`](https://gitlab.gnome.org/GNOME/gjs/blob/HEAD/modules/script/package.js) module.

* `globalThis.pkg` (ie `pkg` on the global object) will provide access to the package module
* `pkg.name` and `pkg.version` will return the package name and version, as passed to `pkg.init()`
* `pkg.prefix`, `pkg.datadir`, `pkg.libdir` will return the installed locations of those folders
* `pkg.pkgdatadir`, `pkg.moduledir`, `pkg.pkglibdir`, `pkg.localedir` will return the respective directories, or the appropriate subdirectory of the current directory if running uninstalled
* `pkg.initGettext()` will initialize gettext. After calling `globalThis._`, `globalThis.C_` and `globalThis.N_` will be available
* `pkg.initSubmodule(name)` will initialize a submodule named @name. It must be called before accessing the typelibs installed by that submodule
* `pkg.loadResource(name)` will load and register a GResource named @name. @name is optional and defaults to ${package-name}
* `pkg.require(deps)` will mark a set of dependencies on GI and standard JS modules. **@deps** is a object whose keys are repository names and whose values are API versions. If the dependencies are not satisfied, `pkg.require()` will print an error message and quit.
