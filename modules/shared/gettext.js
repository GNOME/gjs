const is_legacy = typeof imports === 'object';

/** @type {Object.<string, any>} */
var module = {};

/**
 * @param {string} ns
 */
const $import = (ns) => {
    return is_legacy ? imports[ns] : require(ns);
}

const { GjsPrivate, GLib } = $import('gi');

var { LocaleCategory } = GjsPrivate;

/**
 * 
 * @param {*} category 
 * @param {*} locale 
 */
function setlocale(category, locale) {
    return GjsPrivate.setlocale(category, locale);
}

/**
 * 
 * @param {*} domain 
 */
function textdomain(domain) {
    return GjsPrivate.textdomain(domain);
}

/**
 * 
 * @param {*} domain 
 * @param {*} location 
 */
function bindtextdomain(domain, location) {
    return GjsPrivate.bindtextdomain(domain, location);
}

/**
 * 
 * @param {*} msgid 
 */
function gettext(msgid) {
    return GLib.dgettext(null, msgid);
}

/**
 * 
 * @param {*} domain 
 * @param {*} msgid 
 */
function dgettext(domain, msgid) {
    return GLib.dgettext(domain, msgid);
}

/**
 * 
 * @param {*} domain 
 * @param {*} msgid 
 * @param {*} category 
 */
function dcgettext(domain, msgid, category) {
    return GLib.dcgettext(domain, msgid, category);
}

/**
 * 
 * @param {*} msgid1 
 * @param {*} msgid2 
 * @param {*} n 
 */
function ngettext(msgid1, msgid2, n) {
    return GLib.dngettext(null, msgid1, msgid2, n);
}

/**
 * 
 * @param {*} domain 
 * @param {*} msgid1 
 * @param {*} msgid2 
 * @param {*} n 
 */
function dngettext(domain, msgid1, msgid2, n) {
    return GLib.dngettext(domain, msgid1, msgid2, n);
}

// FIXME: missing dcngettext ?

/**
 * 
 * @param {*} context 
 * @param {*} msgid 
 */
function pgettext(context, msgid) {
    return GLib.dpgettext2(null, context, msgid);
}

/**
 * 
 * @param {*} domain 
 * @param {*} context 
 * @param {*} msgid 
 */
function dpgettext(domain, context, msgid) {
    return GLib.dpgettext2(domain, context, msgid);
}

/**
 * Create an object with bindings for gettext, ngettext,
 * and pgettext bound to a particular translation domain.
 *
 * @type {(domainName: string) => 
 *    ({ 
 *         gettext: (msgid: string) => string;
 *         ngettext: (msgid1: string, msgid2: string, n: number) => string;
 *         pgettext: (context: string, msgid: string) => string;
 *    })
 * }
 * @param domainName Translation domain string
 * @returns an object with gettext bindings
 */
const domain = (domainName) => {
    return {
        gettext: (msgid) => GLib.dgettext(domainName, msgid),
        ngettext: (msgid1, msgid2, n) => GLib.dngettext(domainName, msgid1, msgid2, n),
        pgettext: (context, msgid) => GLib.dpgettext2(domainName, context, msgid)
    };
};


module.exports = {
    LocaleCategory,
    domain,
    setlocale,
    textdomain,
    bindtextdomain,
    gettext,
    pgettext,
    dpgettext,
    dngettext,
    dgettext,
    dcgettext,
    ngettext
};
