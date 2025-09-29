// SPDX-FileCopyrightText: 2025 Florian Müllner <fmuellner@gnome.org>
// SPDX-License-Identifier: MIT OR LGPL-2.1-or-later

import {defineConfig} from '@eslint/config-helpers';
import globals from 'globals';
import gnome from 'eslint-config-gnome';

export default defineConfig([
    gnome.configs.recommended,
    gnome.configs.jsdoc,
    {
        ignores: [
            'installed-tests/js/jasmine.js',
            'installed-tests/js/modules/badOverrides/WarnLib.js',
            'installed-tests/js/modules/subBadInit/__init__.js',
            'modules/script/jsUnit.js',
            '_*/',
            'builddir',
            'modules/internal/source-map/',
            'installed-tests/debugger/sourcemap*',
            'test/source-maps/',
            'subprojects/',
        ],
    },
    {
        languageOptions: {
            sourceType: 'script',
        },
        rules: {
            'no-prototype-builtins': 'error',
            // Override eslint-config-gnome no-restricted-properties
            // Cannot merge into the existing rule:
            // https://github.com/eslint/eslint/issues/17389
            'no-restricted-properties': [
                'error',
                {
                    object: 'imports',
                    property: 'format',
                    message: 'Use template strings',
                },
                {
                    object: 'pkg',
                    property: 'initFormat',
                    message: 'Use template strings',
                },
                {
                    object: 'Lang',
                    property: 'copyProperties',
                    message: 'Use Object.assign()',
                },
                {
                    object: 'Lang',
                    property: 'bind',
                    message: 'Use arrow notation or Function.prototype.bind()',
                },
                {
                    object: 'Lang',
                    property: 'Class',
                    message: 'Use ES6 classes',
                },
            ],
        },
    },
    {
        files: [
            '**/eslint.config.js',
            'examples/**',
            'installed-tests/js/**',
            'modules/esm/**',
            'modules/internal/**',
            'test/modules/**',
        ],
        languageOptions: {
            sourceType: 'module',
        },
    },
    {
        files: [
            'installed-tests/js/modules/dynamic.js',
            'installed-tests/js/modules/foobar.js',
            'installed-tests/js/modules/lexicalScope.js',
            'installed-tests/js/modules/modunicode.js',
            'installed-tests/js/modules/mutualImport/*.js',
            'installed-tests/js/modules/overrides/*.js',
            'installed-tests/js/modules/sloppy.js',
            'installed-tests/js/modules/subA/subB/*.js',
            'installed-tests/js/testFormat.js',
            'installed-tests/js/testImporter.js',
            'installed-tests/js/testImporter2.js',
            'installed-tests/js/testLang.js',
            'installed-tests/js/testLegacyByteArray.js',
            'installed-tests/js/testLegacyCairo.js',
            'installed-tests/js/testLegacyClass.js',
            'installed-tests/js/testLegacyGObject.js',
            'installed-tests/js/testMainloop.js',
            'installed-tests/js/testOverrides.js',
            'installed-tests/js/testPackage.js',
            'installed-tests/js/testSignals.js',
            'installed-tests/js/testTweener.js',
        ],
        languageOptions: {
            sourceType: 'script',
        },
    },
    {
        files: [
            'modules/internal/**',
        ],
        languageOptions: {
            globals: {
                ARGV: 'off',
                GIRepositoryGType: 'off',
                imports: 'off',
                log: 'off',
                logError: 'off',
                print: 'off',
                printerr: 'off',

                atob: 'readonly',
                compileInternalModule: 'readonly',
                compileModule: 'readonly',
                getRegistry: 'readonly',
                getSourceMapRegistry: 'readonly',
                loadResourceOrFile: 'readonly',
                loadResourceOrFileAsync: 'readonly',
                moduleGlobalThis: 'readonly',
                parseURI: 'readonly',
                resolveRelativeResourceOrFile: 'readonly',
                setGlobalModuleLoader: 'readonly',
                setModulePrivate: 'readonly',
                uriExists: 'readonly',
            },
        },
    },
    {
        files: [
            'installed-tests/**',
            'modules/core/**',
            'modules/script/**',
            'examples/**',
        ],
        rules: {
            'jsdoc/require-jsdoc': 'off',
        },
    },
    {
        files: [
            'examples/**',
            'modules/script/tweener/**',
        ],
        rules: {
            'jsdoc/require-param': 'off',
            'jsdoc/require-param-type': 'off',
            'jsdoc/require-returns': 'off',
            'jsdoc/check-tag-names': 'off',
            'jsdoc/check-param-names': 'off',
        },
    },
    {
        files: [
            'modules/script/_bootstrap/**',
        ],
        languageOptions: {
            globals: {
                log: 'off',
                logError: 'off',
                print: 'off',
                printerr: 'off',
            },
        },
    },
    {
        files: [
            'installed-tests/js/**',
        ],
        languageOptions: {
            globals: {
                ...globals.jasmine,
            },
        },
        rules: {
            'no-restricted-globals': [
                'error',
                {
                    name: 'fdescribe',
                    message: 'Do not commit fdescribe(). Use describe() instead.',
                },
                {
                    name: 'fit',
                    message: 'Do not commit fit(). Use it() instead.',
                },
            ],
            'no-restricted-syntax': [
                'error',
                {
                    selector: 'CallExpression[callee.name="it"] > ArrowFunctionExpression',
                    message: 'Arrow functions can mess up some Jasmine APIs. Use function () instead',
                },
                {
                    selector: 'CallExpression[callee.name="beforeEach"] > ArrowFunctionExpression',
                    message: 'Arrow functions can mess up some Jasmine APIs. Use function () instead',
                },
                {
                    selector: 'CallExpression[callee.name="afterEach"] > ArrowFunctionExpression',
                    message: 'Arrow functions can mess up some Jasmine APIs. Use function () instead',
                },
                {
                    selector: 'CallExpression[callee.name="beforeAll"] > ArrowFunctionExpression',
                    message: 'Arrow functions can mess up some Jasmine APIs. Use function () instead',
                },
                {
                    selector: 'CallExpression[callee.name="afterAll"] > ArrowFunctionExpression',
                    message: 'Arrow functions can mess up some Jasmine APIs. Use function () instead',
                },
            ],
        },
    },
    {
        files: [
            'installed-tests/js/modules/overrides/**',
            'modules/core/overrides/**',
        ],
        rules: {
            'no-unused-vars': ['error', {
                varsIgnorePattern: '^_init$',
            }],
        },
    },
    {
        files: [
            'installed-tests/js/modules/badOverrides/**',
            'installed-tests/js/modules/badOverrides2/**',
        ],
        rules: {
            'no-throw-literal': 'off', // these are intended to be bad code
            'no-unused-vars': ['error', {
                varsIgnorePattern: '^_init$',
            }],
        },
    },
    {
        files: [
            'installed-tests/debugger/**',
        ],
        rules: {
            'no-debugger': 'off',
        },
    },
    {
        files: [
            'installed-tests/debugger/keys.debugger.js',
            'installed-tests/debugger/print.debugger.js',
        ],
        rules: {
            'no-unused-private-class-members': 'off',
        },
    },
]);
