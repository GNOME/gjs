// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2020 Evan Welsh <contact@evanwelsh.com>

// InternalModuleLoader is the mechanism present on the internal global object
// which resolves (translates a module specifier such as './mymod.js' or 'gi' to
// an actual file) and loads internal modules.
// It is used to bootstrap the actual module loader in loader.js, and also
// serves as its base class.

/** @typedef {{ uri: string; scheme: string; host: string; path: string; query: Query }} Uri */

/**
 * Use '__internal: never' to prevent any object from being type compatible with Module
 * because it is an internal type.
 *
 * @typedef {{__internal: never;}} Module
 */
/** @typedef {typeof moduleGlobalThis | typeof globalThis} Global */
/** @typedef {{ load(uri: Uri): [contents: string, internal: boolean]; }} SchemeHandler */
/** @typedef {{ [key: string]: string | undefined; }} Query */
/** @typedef {(uri: string, contents: string) => Module} CompileFunc */

/**
 * Thrown when there is an error importing a module.
 */
export class ImportError extends Error {
    /**
     * @param {string | undefined} message the import error message
     */
    constructor(message) {
        super(message);

        this.name = 'ImportError';
    }
}

/**
 * ModulePrivate is the "private" object of every module.
 */
export class ModulePrivate {
    /**
     *
     * @param {string} id the module's identifier
     * @param {string} uri the module's URI
     * @param {boolean} [internal] whether this module is "internal"
     */
    constructor(id, uri, internal = false) {
        this.id = id;
        this.uri = uri;
        this.internal = internal;
    }
}

/**
 * Returns whether a string represents a relative path (e.g. ./, ../)
 *
 * @param {string} path a path to check if relative
 * @returns {boolean}
 */
function isRelativePath(path) {
    // Check if the path is relative. Note that this doesn't mean "relative
    // path" in the GLib sense, as in "not absolute" — it means a relative path
    // module specifier, which must start with a '.' or '..' path component.
    return path.startsWith('./') || path.startsWith('../');
}

/**
 * Handles resolving and loading URIs.
 *
 * @class
 */
export class InternalModuleLoader {
    /**
     * @param {typeof globalThis} global the global object to handle module
     *   resolution
     * @param {(string, string) => import("../types").Module} compileFunc the
     *   function to compile a source into a module for a particular global
     *   object. Should be compileInternalModule() for InternalModuleLoader,
     *   but overridden in ModuleLoader
     */
    constructor(global, compileFunc) {
        this.global = global;
        this.compileFunc = compileFunc;
    }

    /**
     * Loads a file or resource URI synchronously
     *
     * @param {Uri} uri the file or resource URI to load
     * @returns {[contents: string, internal?: boolean] | null}
     */
    loadURI(uri) {
        if (uri.scheme === 'file' || uri.scheme === 'resource')
            return [loadResourceOrFile(uri.uri)];

        return null;
    }

    /**
     * Resolves an import specifier given an optional parent importer.
     *
     * @param {string} specifier the import specifier
     * @param {string | null} [parentURI] the URI of the module importing the specifier
     * @returns {Uri | null}
     */
    resolveSpecifier(specifier, parentURI = null) {
        try {
            const uri = parseURI(specifier);

            if (uri)
                return uri;
        } catch (err) {
            // If it can't be parsed as a URI, try a relative path or return null.
        }

        if (isRelativePath(specifier)) {
            if (!parentURI)
                throw new ImportError('Cannot import relative path when module path is unknown.');

            return this.resolveRelativePath(specifier, parentURI);
        }

        return null;
    }

    /**
     * Resolves a path relative to a URI, throwing an ImportError if
     * the parentURI isn't valid.
     *
     * @param {string} relativePath the relative path to resolve against the base URI
     * @param {string} importingModuleURI the URI of the module triggering this
     *   resolve
     * @returns {Uri}
     */
    resolveRelativePath(relativePath, importingModuleURI) {
        // Ensure the parent URI is valid.
        parseURI(importingModuleURI);

        // Handle relative imports from URI-based modules.
        const relativeURI = resolveRelativeResourceOrFile(importingModuleURI, relativePath);
        if (!relativeURI)
            throw new ImportError('File does not have a valid parent!');
        return parseURI(relativeURI);
    }

    /**
     * Compiles a module source text with the module's URI
     *
     * @param {ModulePrivate} priv a module private object
     * @param {string} text the module source text to compile
     * @returns {Module}
     */
    compileModule(priv, text) {
        const compiled = this.compileFunc(priv.uri, text);

        setModulePrivate(compiled, priv);

        return compiled;
    }

    /**
     * @param {string} specifier the specifier (e.g. relative path, root package) to resolve
     * @param {string | null} importingModuleURI the URI of the module
     *   triggering this resolve
     *
     * @returns {Module | null}
     */
    resolveModule(specifier, importingModuleURI) {
        const registry = getRegistry(this.global);

        // Check if the module has already been loaded
        let module = registry.get(specifier);
        if (module)
            return module;

        // 1) Resolve path and URI-based imports.
        const uri = this.resolveSpecifier(specifier, importingModuleURI);
        if (uri) {
            module = registry.get(uri.uri);

            // Check if module is already loaded (relative handling)
            if (module)
                return module;

            const result = this.loadURI(uri);
            if (!result)
                return null;

            const [text, internal = false] = result;

            const priv = new ModulePrivate(uri.uri, uri.uri, internal);
            const compiled = this.compileModule(priv, text);

            registry.set(uri.uri, compiled);
            return compiled;
        }

        return null;
    }

    moduleResolveHook(importingModulePriv, specifier) {
        const resolved = this.resolveModule(specifier, importingModulePriv.uri ?? null);
        if (!resolved)
            throw new ImportError(`Module not found: ${specifier}`);

        return resolved;
    }

    moduleLoadHook(id, uri) {
        const priv = new ModulePrivate(id, uri);

        const result = this.loadURI(parseURI(uri));
        // result can only be null if `this` is InternalModuleLoader. If `this`
        // is ModuleLoader, then loadURI() will have thrown
        if (!result)
            throw new ImportError(`URI not found: ${uri}`);

        const [text] = result;
        const compiled = this.compileModule(priv, text);

        const registry = getRegistry(this.global);
        registry.set(id, compiled);

        return compiled;
    }
}

export const internalModuleLoader = new InternalModuleLoader(globalThis, compileInternalModule);
setGlobalModuleLoader(globalThis, internalModuleLoader);
