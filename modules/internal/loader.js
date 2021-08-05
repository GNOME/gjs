// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2020 Evan Welsh <contact@evanwelsh.com>

import {ImportError, InternalModuleLoader, ModulePrivate} from './internalLoader.js';

class ModuleLoader extends InternalModuleLoader {
    /**
     * @param {typeof moduleGlobalThis} global the global object to register modules with.
     */
    constructor(global) {
        // Sets 'compileFunc' in InternalModuleLoader to be 'compileModule'
        super(global, compileModule);

        /**
         * @type {Set<string>}
         *
         * The set of "module" URIs (the module search path)
         * For example, having "resource:///org/gnome/gjs/modules/esm/" in this
         * set allows import "system" if
         * "resource:///org/gnome/gjs/modules/esm/system.js" exists.
         */
        this.moduleURIs = new Set([
            'resource:///org/gnome/gjs/modules/esm/',
        ]);

        /**
         * @type {Map<string, import("./internalLoader.js").SchemeHandler>}
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
            const builtURI = `${uri}/${specifier}.js`;
            builtURIs.push(builtURI);
        }

        return builtURIs;
    }

    /**
     * @param {string} scheme the URI scheme to register
     * @param {import("./internalLoader.js").SchemeHandler} handler a handler
     */
    registerScheme(scheme, handler) {
        this.schemeHandlers.set(scheme, handler);
    }

    /**
     * Overrides InternalModuleLoader.loadURI
     *
     * @param {import("./internalLoader.js").Uri} uri a Uri object to load
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
     * @param {import("./internalLoader.js").ModulePrivate} importingModulePriv
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
     * @param {import("./internalLoader.js").Uri} uri the URI to load
     */
    load(uri) {
        const namespace = uri.host;
        const version = uri.query.version;

        return [generateGIModule(namespace, version), true];
    },
    /**
     * @param {import("./internalLoader.js").Uri} uri the URI to load asynchronously
     */
    loadAsync(uri) {
        // gi: only does string manipulation, so it is safe to use the same code for sync and async.
        return this.load(uri);
    },
});
