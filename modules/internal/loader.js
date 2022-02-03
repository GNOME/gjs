// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2020 Evan Welsh <contact@evanwelsh.com>

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
class ImportError extends moduleGlobalThis.Error {
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
class ModulePrivate {
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
class InternalModuleLoader {
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

class ModuleLoader extends InternalModuleLoader {
    /**
     * @param {typeof moduleGlobalThis} global the global object to register modules with.
     */
    constructor(global) {
        // Sets 'compileFunc' in InternalModuleLoader to be 'compileModule'
        super(global, compileModule);

        /**
         * The set of "module" URI globs (the module search path)
         *
         * For example, having `"resource:///org/gnome/gjs/modules/esm/*.js"` in this
         * set allows `import "system"` if
         * `"resource:///org/gnome/gjs/modules/esm/system.js"` exists.
         *
         * Only `*` is supported as a replacement character, `**` is not supported.
         *
         * @type {Set<string>}
         */
        this.moduleURIs = new Set([
            'resource:///org/gnome/gjs/modules/esm/*.js',
        ]);

        /**
         * @type {Map<string, SchemeHandler>}
         *
         * A map of handlers for URI schemes (e.g. gi://)
         */
        this.schemeHandlers = new Map();
    }

    /**
     * @param {string} specifier the package specifier
     * @returns {string[]} the possible internal URIs
     */
    buildInternalURIs(specifier) {
        const {moduleURIs} = this;
        const builtURIs = [];

        for (const uri of moduleURIs) {
            const builtURI = uri.replace('*', specifier);
            builtURIs.push(builtURI);
        }

        return builtURIs;
    }

    /**
     * @param {string} scheme the URI scheme to register
     * @param {SchemeHandler} handler a handler
     */
    registerScheme(scheme, handler) {
        this.schemeHandlers.set(scheme, handler);
    }

    /**
     * Overrides InternalModuleLoader.loadURI
     *
     * @param {Uri} uri a Uri object to load
     */
    loadURI(uri) {
        if (uri.scheme) {
            const loader = this.schemeHandlers.get(uri.scheme);

            if (loader)
                return loader.load(uri);
        }

        const result = super.loadURI(uri);

        if (result)
            return result;

        throw new ImportError(`Invalid module URI: ${uri.uri}`);
    }

    /**
     * Resolves a bare specifier like 'system' against internal resources,
     * erroring if no resource is found.
     *
     * @param {string} specifier the module specifier to resolve for an import
     * @returns {import("./internalLoader").Module}
     */
    resolveBareSpecifier(specifier) {
        // 2) Resolve internal imports.

        const uri = this.buildInternalURIs(specifier).find(uriExists);

        if (!uri)
            throw new ImportError(`Unknown module: '${specifier}'`);

        const parsed = parseURI(uri);
        if (parsed.scheme !== 'file' && parsed.scheme !== 'resource')
            throw new ImportError('Only file:// and resource:// URIs are currently supported.');

        const text = loadResourceOrFile(parsed.uri);
        const priv = new ModulePrivate(specifier, uri, true);
        const compiled = this.compileModule(priv, text);

        const registry = getRegistry(this.global);
        if (!registry.has(specifier))
            registry.set(specifier, compiled);

        return compiled;
    }

    /**
     * Resolves a module import with optional handling for relative imports.
     * Overrides InternalModuleLoader.moduleResolveHook
     *
     * @param {ModulePrivate} importingModulePriv
     *   the private object of the module initiating the import
     * @param {string} specifier the module specifier to resolve for an import
     * @returns {import("./internalLoader").Module}
     */
    moduleResolveHook(importingModulePriv, specifier) {
        const module = this.resolveModule(specifier, importingModulePriv.uri);
        if (module)
            return module;

        return this.resolveBareSpecifier(specifier);
    }

    moduleResolveAsyncHook(importingModulePriv, specifier) {
        // importingModulePriv should never be missing. If it is then a JSScript
        // is missing a private object
        if (!importingModulePriv || !importingModulePriv.uri)
            throw new ImportError('Cannot resolve relative imports from an unknown file.');

        return this.resolveModuleAsync(specifier, importingModulePriv.uri);
    }

    /**
     * Resolves a module import with optional handling for relative imports asynchronously.
     *
     * @param {string} specifier the specifier (e.g. relative path, root package) to resolve
     * @param {string | null} importingModuleURI the URI of the module
     *   triggering this resolve
     * @returns {import("../types").Module}
     */
    async resolveModuleAsync(specifier, importingModuleURI) {
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

            const result = await this.loadURIAsync(uri);
            if (!result)
                return null;

            // Check if module loaded while awaiting.
            module = registry.get(uri.uri);
            if (module)
                return module;

            const [text, internal = false] = result;

            const priv = new ModulePrivate(uri.uri, uri.uri, internal);
            const compiled = this.compileModule(priv, text);

            registry.set(uri.uri, compiled);
            return compiled;
        }

        // 2) Resolve internal imports.

        return this.resolveBareSpecifier(specifier);
    }

    /**
     * Loads a file or resource URI asynchronously
     *
     * @param {Uri} uri the file or resource URI to load
     * @returns {Promise<[string] | [string, boolean] | null>}
     */
    async loadURIAsync(uri) {
        if (uri.scheme) {
            const loader = this.schemeHandlers.get(uri.scheme);

            if (loader)
                return loader.loadAsync(uri);
        }

        if (uri.scheme === 'file' || uri.scheme === 'resource') {
            const result = await loadResourceOrFileAsync(uri.uri);
            return [result];
        }

        return null;
    }
}

const moduleLoader = new ModuleLoader(moduleGlobalThis);
setGlobalModuleLoader(moduleGlobalThis, moduleLoader);

/**
 * Creates a module source text to expose a GI namespace via a default export.
 *
 * @param {string} namespace the GI namespace to import
 * @param {string} [version] the version string of the namespace
 *
 * @returns {string} the generated module source text
 */
function generateGIModule(namespace, version) {
    return `
    import $$gi from 'gi';
    export default $$gi.require('${namespace}'${version !== undefined ? `, '${version}'` : ''});
    `;
}

moduleLoader.registerScheme('gi', {
    /**
     * @param {Uri} uri the URI to load
     */
    load(uri) {
        const namespace = uri.host;
        const version = uri.query.version;

        return [generateGIModule(namespace, version), true];
    },
    /**
     * @param {Uri} uri the URI to load asynchronously
     */
    loadAsync(uri) {
        // gi: only does string manipulation, so it is safe to use the same code for sync and async.
        return this.load(uri);
    },
});
