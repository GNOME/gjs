// @ts-check

// Use a list of reserved words to avoid declaring an export with a reserved word.
const reserved = [
    "abstract", "arguments", "await*", "boolean",
    "break", "byte", "case", "catch",
    "char", "class", "const", "continue",
    "debugger", "default", "delete", "do",
    "double", "else", "enum", "eval",
    "export", "extends", "false", "final",
    "finally", "float", "for", "function",
    "goto", "if", "implements", "import",
    "in", "instanceof", "int", "interface",
    "let", "long", "native", "new",
    "null", "package", "private", "protected",
    "public", "return", "short", "static",
    "super", "switch", "synchronized", "this",
    "throw", "throws", "transient", "true",
    "try", "typeof", "var", "void",
    "volatile", "while", "with", "yield"
];


// TODO Doesn't support unicode characters
const basicVariableTest = /[a-zA-Z_$][0-9a-zA-Z_$]*/.compile();

/**
 * Generate an export, filtering for troublesome variables.
 * TODO: This doesn't allow unicode variable names, but native modules shouldn't need them.
 * 
 * @param {string} ns
 * @param {string} name 
 */
function $export(ns, name) {
    return `let $${name} = ${ns}["${name}"];
${ basicVariableTest.test(name) && !reserved.includes(name) ?
            `export { $${name} as ${name} };`
            :
            `console.log(\`Invalid export from ${ns}: ${name}\`);`
        }
`
}

/**
 * @typedef {Object} Module
 * @property {Object.<string, any>} exports
 * @property {boolean} [isNative]
 * @property {string} [nativeName]
 */

/**
 * @typedef {Object} ScriptScope
 * @property {Module} [module]
 */

/**
 * Wrap an existing "script"/legacy module in an ESM module.
 * @param {string} name
 * @param {ScriptScope} native_module 
 */
function wrap(name, native_module) {
    let moduleExports = Object.getOwnPropertyNames(native_module).filter(f => !f.startsWith('__'));
    let isNative = false;
    
    if (typeof native_module.module !== 'undefined') {
        const moduleMeta = native_module.module;

        if (moduleMeta.exports) {
            moduleExports = Object.getOwnPropertyNames(moduleMeta.exports);
        }

        if ('nativeName' in moduleMeta && moduleMeta.nativeName) {
            name = moduleMeta.nativeName;
        }

        if ('isNative' in moduleMeta && moduleMeta.isNative) {
            isNative = true;
        }
    }

    const translation = moduleExports.map(e => $export(`$$`, e)).join('\n');

    return `const _$$ = import.meta.require('${name}');
const $$ = ${!isNative ? `_$$.module.exports` : `_$$`};

${translation}

export default $$;
    `;
}