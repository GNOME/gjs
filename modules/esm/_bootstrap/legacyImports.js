import gettext from 'gettext';
import cairo from 'cairo';

import '../../deprecated/legacyImports.js';

if ('imports' in globalThis) {
    Object.assign(imports.gettext, gettext);
    Object.assign(imports.cairo, cairo);
}
