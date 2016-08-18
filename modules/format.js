// -*- mode: js; js-indent-level: 4; indent-tabs-mode: nil -*-

const GjsPrivate = imports.gi.GjsPrivate;

function vprintf(str, args) {
    let i = 0;
    let usePos = false;
    return str.replace(/%(?:([1-9][0-9]*)\$)?(I+)?([0-9]+)?(?:\.([0-9]+))?(.)/g, function (str, posGroup, flagsGroup, widthGroup, precisionGroup, genericGroup) {
        if (precisionGroup != '' && genericGroup != 'f')
            throw new Error("Precision can only be specified for 'f'");

        let hasAlternativeIntFlag = (flagsGroup.indexOf('I') != -1);
        if (hasAlternativeIntFlag && genericGroup != 'd')
            throw new Error("Alternative output digits can only be specfied for 'd'");

        let pos = parseInt(posGroup, 10) || 0;
        if (usePos == false && i == 0)
            usePos = pos > 0;
        if (usePos && pos == 0 || !usePos && pos > 0)
            throw new Error("Numbered and unnumbered conversion specifications cannot be mixed");

        let fillChar = (widthGroup[0] == '0') ? '0' : ' ';
        let width = parseInt(widthGroup, 10) || 0;

        function fillWidth(s, c, w) {
            let fill = '';
            for (let i = 0; i < w; i++)
                fill += c;
            return fill.substr(s.length) + s;
        }

        function getArg() {
            return usePos ? args[pos - 1] : args[i++];
        }

        let s = '';
        switch (genericGroup) {
        case '%':
            return '%';
            break;
        case 's':
            s = String(getArg());
            break;
        case 'd':
            let intV = parseInt(getArg());
            if (hasAlternativeIntFlag)
                s = GjsPrivate.format_int_alternative_output(intV);
            else
                s = intV.toString();
            break;
        case 'x':
            s = parseInt(getArg()).toString(16);
            break;
        case 'f':
            if (precisionGroup === '' || precisionGroup === undefined)
                s = parseFloat(getArg()).toString();
            else
                s = parseFloat(getArg()).toFixed(parseInt(precisionGroup));
            break;
        default:
            throw new Error('Unsupported conversion character %' + genericGroup);
        }
        return fillWidth(s, fillChar, width);
    });
}

function printf() {
    let args = Array.prototype.slice.call(arguments);
    let fmt = args.shift();
    print(vprintf(fmt, args));
}

/*
 * This function is intended to extend the String object and provide
 * an String.format API for string formatting.
 * It has to be set up using String.prototype.format = Format.format;
 * Usage:
 * "somestring %s %d".format('hello', 5);
 * It supports %s, %d, %x and %f, for %f it also support precisions like
 * "%.2f".format(1.526). All specifiers can be prefixed with a minimum
 * field width, e.g. "%5s".format("foo"). Unless the width is prefixed
 * with '0', the formatted string will be padded with spaces.
 */
function format() {
    return vprintf(this, arguments);
}
