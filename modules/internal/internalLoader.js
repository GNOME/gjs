// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2020 Evan Welsh <contact@evanwelsh.com>

// eslint-disable-next-line spaced-comment
/// <reference path="./environment.d.ts" />
// @ts-check

/**
 * Thrown when there is an error importing a module.
 */
export class ImportError extends moduleGlobalThis.Error {
    name = 'ImportError';
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
     * @param {CompileFunc} compileFunc the function to compile a source into a
     *   module for a particular global object. Should be
     *   compileInternalModule() for InternalModuleLoader, but overridden in
     *   ModuleLoader
     */
    constructor(global, compileFunc) {
        this.global = global;
        this.compileFunc = compileFunc;
    }

    /**
     * Determines whether a module gets access to import.meta.importSync().
     *
     * @param {Uri} uri real URI of the module (file:/// or resource:///)
     */
    isInternal(uri) {
        void uri;
        return false;
    }

    /**
     * Loads a file or resource URI synchronously
     *
     * @param {Uri} uri the file or resource URI to load
     * @returns {string} the contents of the module
     */
    loadURI(uri) {
        if (uri.scheme === 'file' || uri.scheme === 'resource')
            return loadResourceOrFile(uri.uri);

        throw new ImportError(`Unsupported URI scheme for importing: ${uri.scheme ?? uri}`);
    }

    /**
     * Resolves an import specifier given an optional parent importer.
     *
     * @param {string} specifier the import specifier
     * @param {Uri | null} [parentURI] the URI of the module importing the specifier
     * @returns {Uri}
     */
    resolveSpecifier(specifier, parentURI = null) {
        try {
            const uri = parseURI(specifier);

            if (uri)
                return uri;
        } catch {
            // If it can't be parsed as a URI, try a relative path or return null.
        }

        if (isRelativePath(specifier)) {
            if (!parentURI)
                throw new ImportError('Cannot import relative path when module path is unknown.');

            return resolveRelativeResourceOrFile(parentURI.uriWithQuery, specifier);
        }

        throw new ImportError(`Module not found: ${specifier}`);
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
     * @param {Uri | null} importingModuleURI the URI of the module
     *   triggering this resolve
     *
     * @returns {ResolvedModule}
     */
    resolveModule(specifier, importingModuleURI) {
        const registry = getRegistry(this.global);

        // Check if the module has already been loaded
        let module = registry.get(specifier);
        if (module)
            return [module, '', ''];

        // 1) Resolve path and URI-based imports.
        const uri = this.resolveSpecifier(specifier, importingModuleURI);

        module = registry.get(uri.uriWithQuery);

        // Check if module is already loaded (relative handling)
        if (module)
            return [module, '', ''];

        const text = this.loadURI(uri);
        const internal = this.isInternal(uri);
        const priv = new ModulePrivate(uri.uriWithQuery, uri.uri, internal);
        const compiled = this.compileModule(priv, text);

        registry.set(uri.uriWithQuery, compiled);
        return [compiled, text, uri.uri];
    }

    /**
     * Called by SpiderMonkey as part of gjs_module_resolve().
     *
     * @param {ModulePrivate | null} importingModulePriv - the private object of
     *   the module initiating the import, null if the import is not coming from
     *   a file that can resolve relative imports
     * @param {string} specifier - the specifier (e.g. relative path, root
     *   package) to resolve
     * @returns {Module}
     */
    moduleResolveHook(importingModulePriv, specifier) {
        const importingModuleURI = importingModulePriv ? parseURI(importingModulePriv.uri) : null;
        const [resolved] = this.resolveModule(specifier, importingModuleURI);
        return resolved;
    }

    /**
     * Called by SpiderMonkey as part of gjs_module_load().
     *
     * @param {string} id - the module specifier
     * @param {string} uri - the URI where the module is to be found
     * @returns {Module}
     */
    moduleLoadHook(id, uri) {
        const priv = new ModulePrivate(id, uri);

        const text = this.loadURI(parseURI(uri));
        const compiled = this.compileModule(priv, text);

        const registry = getRegistry(this.global);
        registry.set(id, compiled);

        return compiled;
    }
}

export const internalModuleLoader = new InternalModuleLoader(globalThis, compileInternalModule);
setGlobalModuleLoader(globalThis, internalModuleLoader);
