// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2020 Evan Welsh <contact@evanwelsh.com>

// eslint-disable-next-line spaced-comment
/// <reference path="./environment.d.ts" />
// @ts-check

/**
 * @param {string} message The error message
 * @returns {never}
 */
function throwTypeErrorInModuleGlobal(message) {
    throw new moduleGlobalThis.TypeError(message);
}

/**
 * @param {any} thrown - value thrown from parseURI or resolveRelativeResourceOrFile
 */
function rethrowNonImportError(thrown) {
    if (thrown?.name !== 'ImportError')
        throw thrown;
}

/**
 * https://html.spec.whatwg.org/multipage/webappapis.html#resolving-a-url-like-module-specifier
 *
 * @param {string} specifier Module specifier to resolve
 * @param {Uri | null} baseURL The URL to resolve relative specifiers against,
 *   or null if they cannot be resolved
 * @returns {[uri: Uri | null, mayBeBare?: boolean]}
 */
function resolveURLLikeModuleSpecifier(specifier, baseURL) {
    // 1. If specifier starts with "/", "./", or "../", then:
    if (specifier.startsWith('./') ||
        specifier.startsWith('../') ||
        specifier.startsWith('/')) {
        // 1.1. Let url be the result of parsing specifier with baseURL as the
        // base URL.
        if (!baseURL)
            return [null];
        try {
            // 1.3. Return url.
            return [resolveRelativeResourceOrFile(baseURL.uri, specifier)];
        } catch (error) {
            // 1.2. If url is failure, then return null.
            rethrowNonImportError(error);
            return [null];
        }
    }

    // 2. Let url be the result of parsing specifier (with no base URL).
    try {
        // 4. Return url.
        return [parseURI(specifier)];
    } catch (error) {
        // 3. If url is failure, then return null.
        // GJS Note: Return "mayBeBare" as this may be a bare specifier
        rethrowNonImportError(error);
        return [null, true];
    }
}

// https://html.spec.whatwg.org/multipage/webappapis.html#normalizing-a-specifier-key
/**
 * @param {string} specifierKey Key from the import map that is a module
 *   specifier
 * @param {Uri | null} baseURL The URL to resolve relative specifiers against
 * @returns {string | null}
 */
function normalizeSpecifierKey(specifierKey, baseURL) {
    // 1. If specifierKey is the empty string, then:
    // 1.1. Report a warning to the console that specifier keys cannot be the
    // empty string.
    // 1.2. Return null.
    if (specifierKey === '') {
        internalWarning('Import map has empty specifier key');
        return null;
    }

    // 2. Let url be the result of resolving a URL-like module specifier,
    // given specifierKey and baseURL.
    const [url, mayBeBare] = resolveURLLikeModuleSpecifier(specifierKey, baseURL);

    // GJS Note: Currently there is no base URL, so a relative key could never
    // match
    if (!url && !mayBeBare) {
        internalWarning(`Import map specifier key cannot be relative: ${specifierKey}`);
        return null;
    }

    // 3. If url is not null, then return the serialization of url.
    // 4. Return specifierKey.
    return url?.uriWithQuery ?? specifierKey;
}

/**
 * @template T
 * @param {[key: string, value: T]} a The first key-value pair to compare
 * @param {[key: string, value: T]} b The second key-value pair to compare
 */
function byKeys([a], [b]) {
    if (a < b)
        return 1;
    else if (a > b)
        return -1;
    else
        return 0;
}

// https://html.spec.whatwg.org/multipage/webappapis.html#sorting-and-normalizing-a-module-specifier-map
/**
 * @param {ParsedObject} originalMap The module specifier map object as parsed
 *   from the import map JSON
 * @param {Uri | null} baseURL The URL to resolve relative specifiers against
 * @returns {SpecifierMap}
 */
function sortAndNormalizeSpecifierMap(originalMap, baseURL) {
    // 1. Let normalized be an empty ordered map.
    /** @type {SpecifierMap} */
    const normalized = [];

    // 2. For each specifierKey → value of originalMap,
    for (const [specifierKey, value] of Object.entries(originalMap)) {
        // 2.1. Let normalizedSpecifierKey be the result of normalizing a
        // specifier key given specifierKey and baseURL.
        const normalizedSpecifierKey = normalizeSpecifierKey(specifierKey, baseURL);

        // 2.2. If normalizedSpecifierKey is null, then continue.
        if (normalizedSpecifierKey === null)
            continue;

        // 2.3. If value is not a string, then:
        // 2.3.1. The user agent may report a warning to the console indicating
        // that addresses need to be strings.
        // 2.3.2. Set normalized[normalizedSpecifierKey] to null.
        // 2.3.3. Continue.
        if (typeof value !== 'string') {
            internalWarning('Import map addresses must be string values');
            normalized.push([normalizedSpecifierKey, null]);
            continue;
        }

        // 2.4. Let addressURL be the result of resolving a URL-like module
        // specifier given value and baseURL.
        const [addressURL] = resolveURLLikeModuleSpecifier(value, baseURL);

        // 2.5. If addressURL is null, then:
        // 2.5.1. The user agent may report a warning to the console indicating
        // that the address was invalid.
        // 2.5.2. Set normalized[normalizedSpecifierKey] to null.
        // 2.5.3. Continue.
        if (addressURL === null) {
            internalWarning(`Import map contains invalid address: ${value}`);
            normalized.push([normalizedSpecifierKey, null]);
            continue;
        }

        // 2.6. If specifierKey ends with U+002F (/), and the serialization
        // of addressURL does not end with U+002F (/), then:
        // 2.6.1. The user agent may report a warning to the console indicating
        // that an invalid address was given for the specifier key specifierKey;
        // since specifierKey ends with a slash, the address needs to as well.
        // 2.6.2. Set normalized[normalizedSpecifierKey] to null.
        // 2.6.3. Continue.
        const serializedAddress = addressURL.uriWithQuery;
        if (specifierKey.endsWith('/') && !serializedAddress.endsWith('/')) {
            internalWarning(`Import map address does not end with slash when specifier does: { "${specifierKey}": "${serializedAddress}" }`);
            normalized.push([normalizedSpecifierKey, null]);
            continue;
        }

        // 2.7. Set normalized[normalizedSpecifierKey] to addressURL.
        normalized.push([normalizedSpecifierKey, addressURL]);
    }

    // 3: Return the result of sorting in descending order normalized, with an
    // entry a being less than an entry b if b’s key is code unit less than a’s
    // key.
    normalized.sort(byKeys);
    return normalized;
}

/**
 * Check if it's a map as defined in https://infra.spec.whatwg.org/#ordered-map
 * That is, a JS object that is not null nor an array.
 *
 * @param {unknown} mapVal The value parsed from JSON
 * @returns {mapVal is ParsedObject}
 */
function isOrderedMapObject(mapVal) {
    return !!mapVal && typeof mapVal === 'object' && !Array.isArray(mapVal);
}

/**
 * https://html.spec.whatwg.org/multipage/webappapis.html#sorting-and-normalizing-scopes
 *
 * @param {ParsedObject} originalMap The scopes object as parsed from the import
 *   map JSON
 * @param {Uri | null} baseURL The URL to resolve relative specifiers against
 * @returns {Scopes}
 */
function sortAndNormalizeScopes(originalMap, baseURL) {
    // 1. Let normalized be an empty map.
    /** @type {Scopes} */
    const normalized = [];

    // 2. For each scopePrefix → potentialSpecifierMap of originalMap:
    // 2.1. If potentialSpecifierMap is not an ordered map, then throw a
    // TypeError indicating that the value of the scope with prefix scopePrefix
    // needs to be a JSON object.
    for (const [scopePrefix, potentialSpecifierMap] of Object.entries(originalMap)) {
        if (!isOrderedMapObject(potentialSpecifierMap))
            throwTypeErrorInModuleGlobal(`Scope for ${scopePrefix} must be an object`);

        let scopePrefixURL;

        try {
            // 2.2. Let scopePrefixURL be the result of URL parsing scopePrefix
            // with baseURL.
            scopePrefixURL = baseURL
                ? resolveRelativeResourceOrFile(baseURL.uri, scopePrefix)
                : parseURI(scopePrefix);
        } catch (error) {
            // 2.3. If scopePrefixURL is failure, then:
            // 2.3.1. The user agent may report a warning to the console that
            // the scope prefix URL was not parseable.
            // 2.3.2. Continue.
            rethrowNonImportError(error);
            internalWarning(`Import map scope prefix is not parseable: ${scopePrefix}`);
            continue;
        }

        // 2.4. Let normalizedScopePrefix be the serialization of
        // scopePrefixURL.
        const normalizedScopePrefix = scopePrefixURL.uri;

        // 2.5. Set normalized[normalizedScopePrefix] to the result of sorting
        // and normalizing a specifier map given potentialSpecifierMap and baseURL.
        const specifierMap = sortAndNormalizeSpecifierMap(potentialSpecifierMap, baseURL);
        normalized.push([normalizedScopePrefix, specifierMap]);
    }

    // 3. Return the result of sorting in descending order normalized, with
    // an entry a being less than an entry b if a's key is code unit less than b's
    // key.
    normalized.sort(byKeys);
    return normalized;
}

export class ImportMap {
    /** @type {SpecifierMap} */
    imports;
    /** @type {Scopes} */
    scopes;

    /**
     * https://html.spec.whatwg.org/multipage/webappapis.html#parse-an-import-map-string
     *
     * @param {ParsedObject} parsed The object parsed from the import map JSON
     * @param {null} baseURL The URL to resolve relative URLs in the import map
     *   against. GJS Note: Currently there is no base URL, so this is null and
     *   relative URLs in the import map are invalid.
     */
    constructor(parsed, baseURL) {
        // 1. Let parsed be the result of parsing JSON into Infra values given
        // input. GJS Note: We parse the JSON prior to calling the algorithm.
        // 2. If parsed is not an ordered map, then throw a TypeError indicating
        // that the top-level value needs to be a JSON object.
        if (!isOrderedMapObject(parsed))
            throwTypeErrorInModuleGlobal('Import map must be an object');

        // 3. Let sortedAndNormalizedImports be an empty ordered map.
        this.imports = [];

        // 4. If parsed["imports"] exists, then:
        // 4.1. If parsed["imports"] is not an ordered map, then throw a
        // TypeError indicating that the "imports" top-level key needs to be a
        // JSON object.
        // 4.2. Set sortedAndNormalizedImports to the result of sorting and
        // normalizing a module specifier map given parsed["imports"] and
        // baseURL.
        const imports = parsed['imports'];
        if (imports !== undefined) {
            if (!isOrderedMapObject(imports))
                throwTypeErrorInModuleGlobal('"imports" must be an object');

            this.imports = sortAndNormalizeSpecifierMap(imports, baseURL);
        }

        // 5. Let sortedAndNormalizedScopes be an empty ordered map.
        this.scopes = [];

        // 6. If parsed["scopes"] exists, then:
        // 6.1. If parsed["scopes"] is not an ordered map, then throw a
        // TypeError indicating that the "scopes" top-level key needs to be a
        // JSON object.
        // 6.2. Set sortedAndNormalizedScopes to the result of sorting and
        // normalizing scopes given parsed["scopes"] and baseURL.
        const scopes = parsed['scopes'];
        if (scopes !== undefined) {
            if (!isOrderedMapObject(scopes))
                throwTypeErrorInModuleGlobal('"scopes" must be an object');

            this.scopes = sortAndNormalizeScopes(scopes, baseURL);
        }

        // Currently not supported:
        // 7. Let normalizedIntegrity be an empty ordered map.
        // 8. If parsed["integrity"] exists, then:
        // 8.1. If parsed["integrity"] is not an ordered map, then throw a
        // TypeError indicating that the value for the "integrity" top-level key
        // needs to be a JSON object.
        // 8.2. Set normalizedIntegrity to the result of normalizing a module
        // integrity map given parsed["integrity"] and baseURL.

        // 9. If parsed's keys contains any items besides "imports", "scopes",
        // or "integrity", then the user agent should report a warning to the
        // console indicating that an invalid top-level key was present in the
        // import map.
        const unknownKeys = Object.keys(parsed)
            .filter(key => key !== 'imports' && key !== 'scopes');
        if (unknownKeys.length)
            internalWarning(`Import map has invalid top level keys: ${unknownKeys.join(', ')}`);

        // 8. Return an import map whose imports are sortedAndNormalizedImports,
        // whose scopes are sortedAndNormalizedScopes, and whose integrity are
        // normalizedIntegrity.
    }

    /**
     * https://html.spec.whatwg.org/multipage/webappapis.html#resolve-a-module-specifier
     *
     * @param {Uri | null} scriptBaseURI The base URL of the importing module,
     *   or null if there is none
     * @param {string} specifier The module specifier to resolve
     * @returns {Uri | null} The mapped URI, or null if specifier was a bare
     *   specifier that wasn't mapped to anything else
     */
    resolveModuleSpecifier(scriptBaseURI, specifier) {
        // 2-3. baseURL is either referringScript's base URL if referringScript
        // is non-null, or the current settings object's API base URL.
        // GJS Note: Currently there is no API base URL, so baseURL is null if
        // there is no referringScript.
        const baseURL = scriptBaseURI;

        // 6. Let serializedBaseURL be baseURL, serialized.
        // GJS Note: serializedBaseURL can be null here, unlike in the spec.
        // Also, the importing module's URL does not include its query string,
        // so scope prefixes are compared without one.
        const serializedBaseURL = baseURL?.uri ?? null;

        // 7. Let asURL be the result of resolving a URL-like module specifier
        // given specifier and baseURL.
        const [asURL, mayBeBare] = resolveURLLikeModuleSpecifier(specifier, baseURL);

        // 8. Let normalizedSpecifier be the serialization of asURL, if asURL
        // is non-null; otherwise, specifier.
        const normalizedSpecifier = asURL?.uriWithQuery ?? specifier;

        // 9. Let result be a URL-or-null, initially null.
        let result = null;

        // 10. For each scopePrefix → scopeImports of importMap's scopes,
        for (const [scopePrefix, scopeImports] of this.scopes) {
            // 10.1. If scopePrefix is serializedBaseURL, or if scopePrefix ends
            // with U+002F (/) and scopePrefix is a code unit prefix of
            // serializedBaseURL, then:
            if (scopePrefix === serializedBaseURL ||
                (scopePrefix.endsWith('/') && serializedBaseURL?.startsWith(scopePrefix))) {
                // 10.1.1. Let scopeImportsMatch be the result of resolving an
                // imports match given normalizedSpecifier, asURL, and
                // scopeImports.
                const scopeImportsMatch = resolveImportsMatch(normalizedSpecifier,
                    asURL, scopeImports);

                // 10.1.2. If scopeImportsMatch is not null, then set result to
                // scopeImportsMatch, and break.
                if (scopeImportsMatch !== null) {
                    result = scopeImportsMatch;
                    break;
                }
            }
        }

        // 11. If result is null, set result be the result of resolving an
        // imports match given normalizedSpecifier, asURL, and importMap's
        // imports.
        result ??= resolveImportsMatch(normalizedSpecifier, asURL, this.imports);

        // 12. If result is null, set it to asURL. Note: By this point, if
        // result was null, specifier wasn't remapped to anything by importMap,
        // but it might have been able to be turned into a URL.
        result ??= asURL;

        // 13. If result is not null, then:
        // 13.1. Add module to resolved module set given settingsObject,
        // serializedBaseURL, normalizedSpecifier, and asURL. GJS Note: this
        // only applies if the global object implements Window.
        // 13.2. Return result.
        if (result !== null)
            return result;

        // 14. Throw a TypeError indicating that specifier was a bare specifier,
        // but was not remapped to anything by importMap.
        // GJS Note: It could still be a built-in module, return null instead to
        // continue resolving built-in bare specifiers.
        if (mayBeBare)
            return null;
        // GJS Note: We may have failed to parse a non-bare specifier if it
        // failed to resolve against the base URL.
        throwTypeErrorInModuleGlobal('Failed to resolve import map');
    }
}

// https://url.spec.whatwg.org/#special-scheme
const SPECIAL_SCHEMES = ['ftp', 'file', 'resource', 'http', 'https', 'ws', 'wss'];
/**
 * https://url.spec.whatwg.org/#is-special
 *
 * @param {Uri} uri URI to check the scheme of
 * @returns
 */
function isSpecialScheme(uri) {
    return SPECIAL_SCHEMES.includes(uri.scheme);
}

/**
 * https://html.spec.whatwg.org/multipage/webappapis.html#resolving-an-imports-match
 *
 * @param {string} normalizedSpecifier Module specifier to resolve
 * @param {Uri | null} asURL The specifier resolved to an URL, if applicable
 * @param {SpecifierMap} specifierMap The import map's specifier map
 * @returns {Uri | null}
 */
function resolveImportsMatch(normalizedSpecifier, asURL, specifierMap) {
    // 1. For each specifierKey → resolutionResult of specifierMap,
    for (const [specifierKey, resolutionResult] of specifierMap) {
        // 1.1. If specifierKey is normalizedSpecifier, then:
        if (specifierKey === normalizedSpecifier) {
            // 1.1.1. If resolutionResult is null, then throw a TypeError
            // indicating that resolution of specifierKey was blocked by a null
            // entry. Note: This will terminate the entire "resolve a module
            // specifier" algorithm, without any further fallbacks.
            if (resolutionResult === null)
                throwTypeErrorInModuleGlobal(`Specifier key ${specifierKey} has null entry in import map`);

            // 1.1.2. Assert: resolutionResult is a URL.
            // Note: parseURL asserts this when specifierMap is constructed
            // 1.1.3. Return resolutionResult.
            return resolutionResult;
        }

        // 1.2. If all of the following are true:
        // specifierKey ends with U+002F (/),
        // specifierKey is a code unit prefix of normalizedSpecifier, and
        // either asURL is null, or asURL is special
        if (specifierKey.endsWith('/') &&
            normalizedSpecifier.startsWith(specifierKey) &&
            (asURL === null || isSpecialScheme(asURL))) {
            // 1.2.1. If resolutionResult is null, then throw a TypeError
            // indicating that resolution of specifierKey was blocked by a null
            // entry. Note: This will terminate the entire "resolve a module
            // specifier" algorithm, without any further fallbacks.
            if (resolutionResult === null)
                throwTypeErrorInModuleGlobal(`Specifier key ${specifierKey} has null entry in import map`);

            // 1.2.2. Assert: resolutionResult is a URL.
            // Note: parseURL asserts this when specifierMap is constructed
            // 1.2.3. Let afterPrefix be the portion of normalizedSpecifier after
            // the initial specifierKey prefix.
            const afterPrefix = normalizedSpecifier.slice(specifierKey.length);

            // 1.2.4. Assert: resolutionResult, serialized, ends with U+002F (/),
            // as enforced during parsing
            // 1.2.5. Let url be the result of URL parsing afterPrefix with
            // resolutionResult.
            /** @type {Uri} */
            let url;

            try {
                url = resolveRelativeResourceOrFile(resolutionResult.uri, afterPrefix);
            } catch (error) {
                // 1.2.6. If url is failure, then throw a TypeError indicating
                // that resolution of normalizedSpecifier was blocked since the
                // afterPrefix portion could not be URL-parsed relative to the
                // resolutionResult mapped to by the specifierKey prefix. Note:
                // This will terminate the entire "resolve a module specifier"
                // algorithm, without any further fallbacks.
                rethrowNonImportError(error);
                throwTypeErrorInModuleGlobal(`Specifier ${normalizedSpecifier} could not be parsed as URL with prefix: ${afterPrefix}`);
            }

            // 1.2.7. Assert: url is a URL.
            // resolveRelativeResourceOrFile asserts url is a URL

            // 1.2.8. If the serialization of resolutionResult is not a code
            // unit prefix of the serialization of url, then throw a TypeError
            // indicating that resolution of normalizedSpecifier was blocked due
            // to it backtracking above its prefix specifierKey. Note: This will
            // terminate the entire "resolve a module specifier" algorithm,
            // without any further fallbacks.
            if (!url.uriWithQuery.startsWith(resolutionResult.uriWithQuery)) {
                throwTypeErrorInModuleGlobal(
                    `Specifier ${normalizedSpecifier} backtracked above its specifier key ${specifierKey
                    }, URL does not start with ${resolutionResult.uriWithQuery}`);
            }

            // 1.2.9. Return url.
            return url;
        }
    }

    // 2. Return null.
    return null;
}
