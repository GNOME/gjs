import gettext from 'gettext';
import cairo from 'cairo';

import '../../deprecated/imports.js';

if ('imports' in globalThis) {
    Object.assign(imports.gettext, gettext);
    Object.assign(imports.cairo, cairo);
}
