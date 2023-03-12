const { setPrettyPrintFunction } = import.meta.importSync('_print');

/**
 * @param {unknown} value
 * @returns
 */
export function prettyPrint(value) {
    if (value === null) return 'null';
    if (value === undefined) return 'undefined';
 
    switch (typeof value) {
    case 'object':
        if (value.toString === Object.prototype.toString ||
            value.toString === Array.prototype.toString ||
            value.toString === Date.prototype.toString) {
            const printedObjects = new WeakSet();
            return formatObject(value, printedObjects);
        }
        // If the object has a nonstandard toString, prefer that
        return value.toString();
    case 'function':
        if (value.toString === Function.prototype.toString)
            return formatFunction(value);
        return value.toString();
    case 'string':
        return JSON.stringify(value);
    case 'symbol':
        return formatSymbol(value);
    default:
        return value.toString();
    }
}

function formatPropertyKey(key) {
    if (typeof key === 'symbol')
        return `[${formatSymbol(key)}]`;
    return `${key}`;
}

function formatObject(obj, printedObjects) {
    printedObjects.add(obj);
    if (Array.isArray(obj))
        return formatArray(obj, printedObjects).toString();

    if (obj instanceof Date)
        return formatDate(obj);

    if (obj[Symbol.toStringTag] === 'GIRepositoryNamespace')
        return obj.toString();

    const formattedObject = [];
    const keys = Object.getOwnPropertyNames(obj).concat(Object.getOwnPropertySymbols(obj));
    for (const propertyKey of keys) {
        const value = obj[propertyKey];
        const key = formatPropertyKey(propertyKey);
        switch (typeof value) {
        case 'object':
            if (printedObjects.has(value))
                formattedObject.push(`${key}: [Circular]`);
            else
                formattedObject.push(`${key}: ${formatObject(value, printedObjects)}`);
            break;
        case 'function':
            formattedObject.push(`${key}: ${formatFunction(value)}`);
            break;
        case 'string':
            formattedObject.push(`${key}: "${value}"`);
            break;
        case 'symbol':
            formattedObject.push(`${key}: ${formatSymbol(value)}`);
            break;
        default:
            formattedObject.push(`${key}: ${value}`);
            break;
        }
    }
    return Object.keys(formattedObject).length === 0 ? '{}'
        : `{ ${formattedObject.join(', ')} }`;
}

function formatArray(arr, printedObjects) {
    const formattedArray = [];
    for (const [key, value] of arr.entries()) {
        if (printedObjects.has(value))
            formattedArray[key] = '[Circular]';
        else
            formattedArray[key] = prettyPrint(value);
    }
    return `[${formattedArray.join(', ')}]`;
}

function formatDate(date) {
    return date.toISOString();
}

function formatFunction(func) {
    let funcOutput = `[ Function: ${func.name} ]`;
    return funcOutput;
}

function formatSymbol(sym) {
    // Try to format Symbols in the same way that they would be constructed.

    // First check if this is a global registered symbol
    const globalKey = Symbol.keyFor(sym);
    if (globalKey !== undefined)
        return `Symbol.for("${globalKey}")`;

    const descr = sym.description;
    // Special-case the 'well-known' (built-in) Symbols
    if (descr.startsWith('Symbol.'))
        return descr;

    // Otherwise, it's just a regular symbol
    return `Symbol("${descr}")`;
}

setPrettyPrintFunction(globalThis, prettyPrint);
