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
        ],
    },
    {
        languageOptions: {
            sourceType: 'script',
        },
    },
    {
        files: [
            '**/eslint.config.js',
            'examples/**',
            'installed-tests/js/matchers.js',
            'installed-tests/js/minijasmine-executor.js',
            'installed-tests/js/minijasmine.js',
            'installed-tests/js/modules/exports.js',
            'installed-tests/js/modules/greet.js',
            'installed-tests/js/modules/importmeta.js',
            'installed-tests/js/modules/networkURI.js',
            'installed-tests/js/modules/say.js',
            'installed-tests/js/modules/scaryURI.js',
            'installed-tests/js/modules/sideEffect4.js',
            'installed-tests/js/testAsync.js',
            'installed-tests/js/testAsyncMainloop.js',
            'installed-tests/js/testCairoModule.js',
            'installed-tests/js/testConsole.js',
            'installed-tests/js/testEncoding.js',
            'installed-tests/js/testESModules.js',
            'installed-tests/js/testGLibLogWriter.js',
            'installed-tests/js/testTimers.js',
            'installed-tests/js/testUtility.js',
            'installed-tests/js/testWeakRef.js',
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
