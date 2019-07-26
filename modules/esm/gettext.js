import GLib from "gi://GLib";

/**
 * @param {string} category 
 * @param {string} locale 
 */
export function setlocale(category, locale) {
    return imports.gettext.setlocale(category, locale);
}

/**
 * @param {string} domain  
 */
export function textdomain(domain) {
    return imports.gettext.textdomain(domain);
}

/**
 * 
 * @param {string} domain 
 * @param {string} location 
 */
export function bindtextdomain(domain, location) {
    return imports.gettext.bindtextdomain(domain, location);
}

/**
 * 
 * @param {string} msgid 
 */
export function gettext(msgid) {
    return imports.gettext.gettext(msgid);
}

export function dgettext(domain, msgid) {
    return imports.gettext.dgettext(domain, msgid);
}

export function dcgettext(domain, msgid, category) {
    return imports.gettext.dcgettext(domain, msgid, category);
}

export function ngettext(msgid1, msgid2, n) {
    return imports.gettext.ngettext(msgid1, msgid2, n);
}

export function dngettext(domain, msgid1, msgid2, n) {
    return imports.gettext.dngettext(domain, msgid1, msgid2, n);
}

export function pgettext(context, msgid) {
    return imports.gettext.pgettext(context, msgid);
}

export function dpgettext(domain, context, msgid) {
    return imports.gettext.dpgettext2(domain, context, msgid);
}

/**
 * @typedef GettextObject
 * @property {(msgid: string) => string} gettext
 * @property {(msgid1: string, msgid2: string, n: number) => string} ngettext
 * @property {(context: string, msgid: string) => string} pgettext
 */

/**
 * Create an object with bindings for gettext, ngettext,
 * and pgettext bound to a particular translation domain.
 *
 * @type {(domainName: string) => GettextObject}
 * @param {string} domainName Translation domain string
 * @returns {GettextObject} an object with gettext bindings
 */
export function domain(domainName) {
    return {
        gettext(msgid) {
            return GLib.dgettext(domainName, msgid);
        },

        ngettext(msgid1, msgid2, n) {
            return GLib.dngettext(domainName, msgid1, msgid2, n);
        },

        pgettext(context, msgid) {
            return GLib.dpgettext2(domainName, context, msgid);
        }
    };
};


export default (imports.gettext);
