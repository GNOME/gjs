// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2020 Evan Welsh <contact@evanwelsh.com>

// eslint-disable-next-line spaced-comment
/// <reference path="./environment.d.ts" />
// @ts-check

import {ImportError, InternalModuleLoader, ModulePrivate} from './internalLoader.js';
import {extractUrl} from './source-map/extractUrl.js';
import {SourceMapConsumer} from './source-map/source-map-consumer.js';

const DATA_URI_PREFIX = 'data:application/json;base64,';

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
     * Overrides InternalModuleLoader.isInternal
     *
     * @param {Uri} uri real URI of the module (file:/// or resource:///)
     */
    isInternal(uri) {
        const s = uri.uri;
        for (const internalURIPattern of this.moduleURIs) {
            const [start, end] = internalURIPattern.split('*');
            if (s.startsWith(start) && s.endsWith(end))
                return true;
        }
        return false;
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
     * @returns {string}
     */
    loadURI(uri) {
        if (uri.scheme) {
            const loader = this.schemeHandlers.get(uri.scheme);

            if (loader)
                return loader.load(uri);
        }

        return super.loadURI(uri);
    }

    /**
     * Overrides InternalModuleLoader.resolveSpecifier. Adds the behaviour of
     * resolving a bare specifier like 'system' against internal resources.
     *
     * @param {string} specifier the import specifier
     * @param {Uri | null} [parentURI] the URI of the module importing the
     *   specifier
     * @returns {Uri}
     */
    resolveSpecifier(specifier, parentURI = null) {
        try {
            return super.resolveSpecifier(specifier, parentURI);
        } catch (error) {
            // On failure due to bare specifiers, try resolving the bare
            // specifier to an internal module
            if (!specifier.startsWith('.')) {
                const realURI = this.buildInternalURIs(specifier).find(uriExists);
                if (realURI)
                    return parseURI(realURI);
            }

            // Re-throw other errors
            throw error;
        }
    }

    /**
     * Populates the source map registry of a given module
     * Extracts the source map URL from the given code, parses the source map and build the SourceMapConsumer
     * This function will fail gracefully and not throw
     *
     * @param {string} text The JS code of the module
     * @param {string} uri The URI of the module or file with the sourceMappingURL definition
     * @param {string} [absoluteUri] The Absolute URI of the file containing the
     *   sourceMappingURL definition. This is only used for non-module files.
     */
    populateSourceMap(text, uri, absoluteUri) {
        if (!text)
            return;
        const sourceMapUrl = extractUrl(text);
        if (!sourceMapUrl)
            return;

        let jsonText = null;
        try {
            // check if we have an inlined data uri
            if (sourceMapUrl?.startsWith(DATA_URI_PREFIX)) {
                jsonText = atob(sourceMapUrl.substring(DATA_URI_PREFIX.length));
            } else {
                // load the source map resource or file
                // resolve the source map file relative to the source file
                const sourceMapUri = resolveRelativeResourceOrFile(absoluteUri ?? uri,
                    `./${sourceMapUrl}`);
                jsonText = this.loadURI(sourceMapUri);
            }
        } catch {}

        if (jsonText) {
            try {
                const sourceMapRegistry = getSourceMapRegistry(this.global);
                const sourceMap = JSON.parse(jsonText);
                const consumer = new SourceMapConsumer(sourceMap);

                sourceMapRegistry.set(uri, consumer);
            } catch {}
        }
    }

    /**
     * Resolves a module import with optional handling for relative imports.
     * Overrides InternalModuleLoader.moduleResolveHook
     *
     * @param {ModulePrivate | null} importingModulePriv - the private object of
     *   the module initiating the import, null if the import is not coming from
     *   a file that can resolve relative imports
     * @param {string} specifier the module specifier to resolve for an import
     * @returns {Module}
     */
    moduleResolveHook(importingModulePriv, specifier) {
        const importingModuleURI = importingModulePriv ? parseURI(importingModulePriv.uri) : null;
        const [module, text, uri] = this.resolveModule(specifier, importingModuleURI);

        this.populateSourceMap(text, uri);

        return module;
    }

    /**
     * Overrides InternalModuleLoader.moduleLoadHook
     *
     * @param {string} id - the module specifier
     * @param {string} uri - the URI where the module is to be found
     * @returns {Module}
     */
    moduleLoadHook(id, uri) {
        const text = this.loadURI(parseURI(uri));
        this.populateSourceMap(text, uri);
        return super.moduleLoadHook(id, uri);
    }

    /**
     * Resolves a module import with optional handling for relative imports asynchronously.
     *
     * @param {ModulePrivate | null} importingModulePriv - the private object of
     *   the module initiating the import, null if the import is not coming from
     *   a file that can resolve relative imports
     * @param {string} specifier - the specifier (e.g. relative path, root
     *   package) to resolve
     * @returns {Promise<Module>}
     */
    async moduleResolveAsyncHook(importingModulePriv, specifier) {
        const registry = getRegistry(this.global);

        // Check if the module has already been loaded
        let module = registry.get(specifier);
        if (module)
            return module;

        // 1) Resolve path and URI-based imports.
        const importingModuleURI = importingModulePriv ? parseURI(importingModulePriv.uri) : null;
        const uri = this.resolveSpecifier(specifier, importingModuleURI);

        module = registry.get(uri.uriWithQuery);

        // Check if module is already loaded (relative handling)
        if (module)
            return module;

        const text = await this.loadURIAsync(uri);

        // Check if module loaded while awaiting.
        module = registry.get(uri.uriWithQuery);
        if (module)
            return module;

        const internal = this.isInternal(uri);
        const priv = new ModulePrivate(uri.uriWithQuery, uri.uri, internal);
        const compiled = this.compileModule(priv, text);

        registry.set(uri.uriWithQuery, compiled);

        this.populateSourceMap(text, uri.uri);
        return compiled;
    }

    /**
     * Loads a file or resource URI asynchronously
     *
     * @param {Uri} uri the file or resource URI to load
     * @returns {Promise<string>}
     */
    loadURIAsync(uri) {
        if (uri.scheme) {
            const loader = this.schemeHandlers.get(uri.scheme);

            if (loader)
                return loader.loadAsync(uri);
        }

        if (uri.scheme === 'file' || uri.scheme === 'resource')
            return loadResourceOrFileAsync(uri.uri);

        throw new ImportError(`Unsupported URI scheme for importing: ${uri.scheme ?? uri}`);
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

        return generateGIModule(namespace, version);
    },
    /**
     * @param {Uri} uri the URI to load asynchronously
     */
    loadAsync(uri) {
        // gi: only does string manipulation, so it is safe to use the same code for sync and async.
        return Promise.resolve(this.load(uri));
    },
});
