const is_legacy = typeof imports === 'object';

/**
 * @param {string} ns
 */
const $import = (ns) => {
    return is_legacy ? imports[ns] : require(ns);
}

const { GjsPrivate } = $import('gi');

/** @type {Object.<string, any>} */
var module = {};

/**
 * 
 * @param {string} str 
 * @param {any} args 
 * @returns {string}
 */
function vprintf(str, args) {
    if (!Array.isArray(args)) {
        throw new TypeError(`Expected array for args but received ${(args && args.class) || typeof args} instead!`);
    }

    let i = 0;
    let usePos = false;

    return str.replace(/%(?:([1-9][0-9]*)\$)?(I+)?([0-9]+)?(?:\.([0-9]+))?(.)/g, (str, posGroup, flagsGroup, widthGroup, precisionGroup, genericGroup) => {
        if (precisionGroup !== '' && precisionGroup !== undefined &&
            genericGroup != 'f')
            throw new Error("Precision can only be specified for 'f'");

        let hasAlternativeIntFlag = (flagsGroup &&
            flagsGroup.indexOf('I') != -1);
        if (hasAlternativeIntFlag && genericGroup != 'd')
            throw new Error("Alternative output digits can only be specfied for 'd'");

        let pos = Number.parseInt(posGroup, 10) || 0;
        if (usePos == false && i == 0)
            usePos = pos > 0;
        if (usePos && pos == 0 || !usePos && pos > 0)
            throw new Error("Numbered and unnumbered conversion specifications cannot be mixed");

        let fillChar = (widthGroup && widthGroup[0] == '0') ? '0' : ' ';
        let width = Number.parseInt(widthGroup, 10) || 0;

        /**
         * @param {string} s 
         * @param {string} c 
         * @param {number} w 
         */
        function fillWidth(s, c, w) {
            const fill = ''.padEnd(w, c);
            return fill.substr(s.length) + s;
        }

        function getArg() {
            return usePos ? args[pos - 1] : args[i++];
        }

        let s = '';
        switch (genericGroup) {
            case '%':
                return '%';
            case 's':
                s = `${getArg()}`;
                break;
            case 'd':
                const intV = Number.parseInt(getArg());
                if (hasAlternativeIntFlag)
                    s = GjsPrivate.format_int_alternative_output(intV);
                else
                    s = intV.toString();
                break;
            case 'x':
                s = Number.parseInt(getArg()).toString(16);
                break;
            case 'f':
                if (precisionGroup === '' || typeof precisionGroup === 'undefined')
                    s = Number.parseFloat(getArg()).toString();
                else
                    s = Number.parseFloat(getArg()).toFixed(Number.parseInt(precisionGroup));
                break;
            default:
                throw new Error(`Unsupported conversion character % ${genericGroup}`);
        }

        return fillWidth(s, fillChar, width);
    });
}

module.exports = {
    vprintf
};