// -*- mode: js; js-indent-level: 4; indent-tabs-mode: nil -*-
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2012 Red Hat, Inc.
// SPDX-FileCopyrightText: 2012 Giovanni Campagna <scampa.giovanni@gmail.com>

/* exported vprintf */

let numberFormatter = null;

function vprintf(string, args) {
    let i = 0;
    let usePos = false;
    return string.replace(/%(?:([1-9][0-9]*)\$)?(I+)?([0-9]+)?(?:\.([0-9]+))?(.)/g, function (str, posGroup, flagsGroup, widthGroup, precisionGroup, genericGroup) {
        if (precisionGroup !== '' && precisionGroup !== undefined &&
            genericGroup !== 'f')
            throw new Error("Precision can only be specified for 'f'");

        let hasAlternativeIntFlag = flagsGroup &&
            flagsGroup.indexOf('I') !== -1;
        if (hasAlternativeIntFlag && genericGroup !== 'd')
            throw new Error("Alternative output digits can only be specified for 'd'");

        let pos = parseInt(posGroup, 10) || 0;
        if (!usePos && i === 0)
            usePos = pos > 0;
        if (usePos && pos === 0 || !usePos && pos > 0)
            throw new Error('Numbered and unnumbered conversion specifications cannot be mixed');

        let fillChar = widthGroup && widthGroup[0] === '0' ? '0' : ' ';
        let width = parseInt(widthGroup, 10) || 0;

        function fillWidth(s, c, w) {
            let fill = c.repeat(w);
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
            s = String(getArg());
            break;
        case 'd': {
            let intV = parseInt(getArg());
            if (hasAlternativeIntFlag) {
                numberFormatter ??= new Intl.NumberFormat();
                s = numberFormatter.format(intV);
            } else {
                s = intV.toString();
            }
            break;
        }
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
            throw new Error(`Unsupported conversion character %${genericGroup}`);
        }
        return fillWidth(s, fillChar, width);
    });
}
