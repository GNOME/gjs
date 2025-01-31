/* global atob, getSourceMapRegistry */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2020 Evan Welsh <contact@evanwelsh.com>

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
     * @returns {Module}
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
                const sourceMapUri = this.resolveSpecifier(`./${sourceMapUrl}`, absoluteUri ? absoluteUri : uri);
                const result = this.loadURI(sourceMapUri);
                if (!result)
                    return;
                jsonText = result[0];
            }
        } catch (e) {}

        if (jsonText) {
            try {
                const sourceMapRegistry = getSourceMapRegistry(this.global);
                const sourceMap = JSON.parse(jsonText);
                const consumer = new SourceMapConsumer(sourceMap);

                sourceMapRegistry.set(uri, consumer);
            } catch (e) {}
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
        const [module, text, uri] = this.resolveModule(specifier, importingModulePriv?.uri ?? null);

        this.populateSourceMap(text, uri);

        if (module)
            return module;

        return this.resolveBareSpecifier(specifier);
    }

    moduleLoadHook(id, uri) {
        const result = this.loadURI(parseURI(uri));
        // result can only be null if `this` is InternalModuleLoader. If `this`
        // is ModuleLoader, then loadURI() will have thrown
        if (!result)
            throw new ImportError(`URI not found: ${uri}`);

        const [text] = result;
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
        const importingModuleURI = importingModulePriv?.uri;
        const registry = getRegistry(this.global);

        // Check if the module has already been loaded
        let module = registry.get(specifier);
        if (module)
            return module;

        // 1) Resolve path and URI-based imports.
        const uri = this.resolveSpecifier(specifier, importingModuleURI);
        if (uri) {
            module = registry.get(uri.uriWithQuery);

            // Check if module is already loaded (relative handling)
            if (module)
                return module;

            const result = await this.loadURIAsync(uri);
            if (!result)
                return null;

            // Check if module loaded while awaiting.
            module = registry.get(uri.uriWithQuery);
            if (module)
                return module;

            const [text, internal = false] = result;

            const priv = new ModulePrivate(uri.uriWithQuery, uri.uri, internal);
            const compiled = this.compileModule(priv, text);

            registry.set(uri.uriWithQuery, compiled);

            this.populateSourceMap(text, uri);
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
        return Promise.resolve(this.load(uri));
    },
});
