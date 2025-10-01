// SPDX-FileCopyrightText: 2012 Giovanni Campagna
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later

/* exported checkSymbol, datadir, init, initFormat, initGettext, initSubmodule,
libdir, localedir, moduledir, name, pkgdatadir, pkglibdir, prefix, require,
requireSymbol, run, start, version */

/**
 * This module provides a set of convenience APIs for building packaged
 * applications.
 */
imports.gi.versions.GIRepository = '3.0';

const GLib = imports.gi.GLib;
const GIRepository = imports.gi.GIRepository;
const Gio = imports.gi.Gio;
const GObject = imports.gi.GObject;
const System = imports.system;

const Gettext = imports.gettext;

// public
var name;
var version;
var prefix;
var datadir;
var libdir;
var pkgdatadir;
var pkglibdir;
var moduledir;
var localedir;

// private
let _pkgname;
let _base;
let _submoduledir;

function _findEffectiveEntryPointName() {
    let entryPoint = System.programInvocationName;
    while (GLib.file_test(entryPoint, GLib.FileTest.IS_SYMLINK))
        entryPoint = GLib.file_read_link(entryPoint);

    return GLib.path_get_basename(entryPoint);
}

function _runningFromSource() {
    let binary = Gio.File.new_for_path(System.programInvocationName);
    let sourceBinary = Gio.File.new_for_path(`./src/${name}`);
    return binary.equal(sourceBinary);
}

function _runningFromMesonSource() {
    return GLib.getenv('MESON_BUILD_ROOT') &&
           GLib.getenv('MESON_SOURCE_ROOT');
}

/**
 * Initialize directories and global variables. Must be called
 * before any of other API in Package is used.
 * `params` must be an object with at least the following keys:
 *  - name: the package name ($(PACKAGE_NAME) in autotools,
 *          eg. org.foo.Bar)
 *  - version: the package version
 *  - prefix: the installation prefix
 *
 * init() will take care to check if the program is running from
 * the source directory or not, by looking for a 'src' directory.
 *
 * At the end, the global variable 'pkg' will contain the
 * Package module (imports.package). Additionally, the following
 * module variables will be available:
 *  - name: the base name of the entry point (eg. org.foo.Bar.App)
 *  - version: same as in @params
 *  - prefix: the installation prefix (as passed in @params)
 *  - datadir, libdir: the final datadir and libdir when installed;
 *                     usually, these would be prefix + '/share' and
 *                     and prefix + '/lib' (or '/lib64')
 *  - pkgdatadir: the directory to look for private data files, such as
 *                images, stylesheets and UI definitions;
 *                this will be datadir + name when installed and
 *                './data' when running from the source tree
 *  - pkglibdir: the directory to look for private typelibs and C
 *               libraries;
 *               this will be libdir + name when installed and
 *               './lib' when running from the source tree
 *  - moduledir: the directory to look for JS modules;
 *               this will be pkglibdir when installed and
 *               './src' when running from the source tree
 *  - localedir: the directory containing gettext translation files;
 *               this will be datadir + '/locale' when installed
 *               and './po' in the source tree
 *
 * All paths are absolute and will not end with '/'.
 *
 * As a side effect, init() calls GLib.set_prgname().
 *
 * @param {object} params package parameters
 */
function init(params) {
    globalThis.pkg = imports.package;
    _pkgname = params.name;
    name = _findEffectiveEntryPointName();
    version = params.version;

    // Must call it first, because it can only be called
    // once, and other library calls might have it as a
    // side effect
    GLib.set_prgname(name);

    prefix = params.prefix;
    libdir = params.libdir;
    datadir = GLib.build_filenamev([prefix, 'share']);
    let libpath, girpath;

    const loadResource = (path, baseName) => {
        const resource = Gio.Resource.load(GLib.build_filenamev([path,
            `${baseName}.src.gresource`]));
        resource._register();

        return `resource:///${baseName.replaceAll('.', '/')}/js`;
    };

    if (_runningFromMesonSource()) {
        log('Running from Meson, using local files');
        let bld = GLib.getenv('MESON_BUILD_ROOT');

        pkglibdir = libpath = girpath = GLib.build_filenamev([bld, 'lib']);
        pkgdatadir = GLib.build_filenamev([bld, 'data']);
        localedir = GLib.build_filenamev([bld, 'po']);
        _submoduledir = GLib.build_filenamev([bld, 'subprojects']);

        GLib.setenv('GSETTINGS_SCHEMA_DIR', pkgdatadir, true);
        const bldPath = GLib.build_filenamev([bld, 'src']);
        try {
            moduledir = loadResource(bldPath, name);
        } catch {
            try {
                moduledir = loadResource(bldPath, _pkgname);
                name = _pkgname;
            } catch {
                const src = GLib.getenv('MESON_SOURCE_ROOT');
                moduledir = GLib.build_filenamev([src, 'src']);
            }
        }
    } else if (_runningFromSource()) {
        log('Running from source tree, using local files');
        // Running from source directory
        _base = GLib.get_current_dir();
        _submoduledir = _base;
        pkglibdir = GLib.build_filenamev([_base, 'lib']);
        libpath = GLib.build_filenamev([pkglibdir, '.libs']);
        girpath = pkglibdir;
        pkgdatadir = GLib.build_filenamev([_base, 'data']);
        localedir = GLib.build_filenamev([_base, 'po']);
        moduledir = GLib.build_filenamev([_base, 'src']);

        GLib.setenv('GSETTINGS_SCHEMA_DIR', pkgdatadir, true);
    } else {
        _base = prefix;
        pkglibdir = GLib.build_filenamev([libdir, _pkgname]);
        libpath = pkglibdir;
        girpath = GLib.build_filenamev([pkglibdir, 'girepository-1.0']);
        pkgdatadir = GLib.build_filenamev([datadir, _pkgname]);
        localedir = GLib.build_filenamev([datadir, 'locale']);

        try {
            moduledir = loadResource(pkgdatadir, name);
        } catch {
            try {
                moduledir = loadResource(pkgdatadir, _pkgname);
                name = _pkgname;
            } catch {
                moduledir = pkgdatadir;
            }
        }
    }

    const giRepo = GIRepository.Repository.dup_default();

    imports.searchPath.unshift(moduledir);
    giRepo.prepend_search_path(girpath);
    giRepo.prepend_library_path(libpath);

    try {
        let resource = Gio.Resource.load(GLib.build_filenamev([pkgdatadir,
            `${name}.data.gresource`]));
        resource._register();
    } catch {
        try {
            let resource = Gio.Resource.load(GLib.build_filenamev([pkgdatadir,
                `${_pkgname}.data.gresource`]));
            resource._register();
        } catch {}
    }
}

/**
 * This is a convenience function if your package has a
 * single entry point.
 * You must define a main(ARGV) function inside a main.js
 * module in moduledir.
 *
 * @param {object} params see init()
 */
function start(params) {
    init(params);
    run(imports.main);
}

/**
 * This is the function to use if you want to have multiple
 * entry points in one package.
 * You must define a main(ARGV) function inside the passed
 * in module, and then the launcher would be
 *
 * imports.package.init(...);
 * imports.package.run(imports.entrypoint);
 *
 * @param {object} module the module to run
 * @returns {number|undefined} the exit code of the module's main() function
 */
function run(module) {
    return module.main([System.programInvocationName].concat(ARGV));
}

/**
 * Mark a dependency on a specific version of one or more
 * external GI typelibs.
 * `libs` must be an object whose keys are a typelib name,
 * and values are the respective version. The empty string
 * indicates any version.
 *
 * @param {object} libs the external dependencies to import
 */
function require(libs) {
    for (let l in libs)
        requireSymbol(l, libs[l]);
}

/**
 * As checkSymbol(), but exit with an error if the
 * dependency cannot be satisfied.
 *
 * @param {string} lib an external dependency to import
 * @param {string} [ver] version of the dependency
 * @param {string} [symbol] symbol to check for
 */
function requireSymbol(lib, ver, symbol) {
    if (!checkSymbol(lib, ver, symbol)) {
        if (symbol)
            printerr(`Unsatisfied dependency: No ${symbol} in ${lib}`);
        else
            printerr(`Unsatisfied dependency: ${lib}`);
        System.exit(1);
    }
}

/**
 * Check whether an external GI typelib can be imported
 * and provides @symbol.
 *
 * Symbols may refer to
 *  - global functions         ('main_quit')
 *  - classes                  ('Window')
 *  - class / instance methods ('IconTheme.get_default' / 'IconTheme.has_icon')
 *  - GObject properties       ('Window.default_height')
 *
 * @param {string} lib an external dependency to import
 * @param {string} [ver] version of the dependency
 * @param {string} [symbol] symbol to check for
 * @returns {boolean} true if `lib` can be imported and provides `symbol`, false
 * otherwise
 */
function checkSymbol(lib, ver, symbol) {
    let Lib = null;

    if (ver)
        imports.gi.versions[lib] = ver;

    try {
        Lib = imports.gi[lib];
    } catch {
        return false;
    }

    if (!symbol)
        return true; // Done

    let [klass, sym] = symbol.split('.');
    if (klass === symbol)
        return typeof Lib[symbol] !== 'undefined';

    let obj = Lib[klass];
    if (typeof obj === 'undefined')
        return false;

    if (typeof obj[sym] !== 'undefined' ||
        obj.prototype && typeof obj.prototype[sym] !== 'undefined')
        return true; // class- or object method

    // GObject property
    let pspec = null;
    if (GObject.type_is_a(obj.$gtype, GObject.TYPE_INTERFACE)) {
        let iface = GObject.type_default_interface_get(obj.$gtype);
        pspec = GObject.Object.interface_find_property(iface, sym);
    } else if (GObject.type_is_a(obj.$gtype, GObject.TYPE_OBJECT)) {
        pspec = GObject.Object.find_property.call(obj.$gtype, sym);
    }

    return pspec !== null;
}

function initGettext() {
    Gettext.bindtextdomain(_pkgname, localedir);
    Gettext.textdomain(_pkgname);

    let gettext = imports.gettext;
    globalThis._ = gettext.gettext;
    globalThis.C_ = gettext.pgettext;
    globalThis.N_ = function (x) {
        return x;
    };
}

function initFormat() {
    // eslint-disable-next-line no-restricted-properties
    let format = imports.format;
    String.prototype.format = format.format;
}

function initSubmodule(moduleName) {
    if (_runningFromMesonSource() || _runningFromSource()) {
        // Running from source tree, add './moduleName' to search paths

        let submoduledir = GLib.build_filenamev([_submoduledir, moduleName]);
        let libpath;
        if (_runningFromMesonSource())
            libpath = submoduledir;
        else
            libpath = GLib.build_filenamev([submoduledir, '.libs']);
        GIRepository.Repository.prepend_search_path(submoduledir);
        GIRepository.Repository.prepend_library_path(libpath);
    } else {
        // Running installed, submodule is in $(pkglibdir), nothing to do
    }
}
