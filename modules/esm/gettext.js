// Copyright 2009 Red Hat, Inc.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to
// deal in the Software without restriction, including without limitation the
// rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.

/* exported bindtextdomain, dcgettext, dgettext, dngettext, domain, dpgettext,
gettext, LocaleCategory, ngettext, pgettext, setlocale, textdomain */

/**
 * This module provides a convenience layer for the "gettext" family of functions,
 * relying on GLib for the actual implementation.
 *
 * Usage:
 *
 * const Gettext = imports.gettext;
 *
 * Gettext.textdomain("myapp");
 * Gettext.bindtextdomain("myapp", "/usr/share/locale");
 *
 * let translated = Gettext.gettext("Hello world!");
 */
// @ts-ignore
import GLib from "gi://GLib?v=2.0";
import GjsPrivate from "gi://GjsPrivate"

/**
 * 
 * @param {*} category 
 * @param {*} locale 
 */
export function setlocale(category, locale) {
    return GjsPrivate.setlocale(category, locale);
}

/**
 * 
 * @param {*} domain 
 */
export function textdomain(domain) {
    return GjsPrivate.textdomain(domain);
}

/**
 * 
 * @param {*} domain 
 * @param {*} location 
 */
export function bindtextdomain(domain, location) {
    return GjsPrivate.bindtextdomain(domain, location);
}

/**
 * 
 * @param {*} msgid 
 */
export function gettext(msgid) {
    return GLib.dgettext(null, msgid);
}

/**
 * 
 * @param {*} domain 
 * @param {*} msgid 
 */
export function dgettext(domain, msgid) {
    return GLib.dgettext(domain, msgid);
}

/**
 * 
 * @param {*} domain 
 * @param {*} msgid 
 * @param {*} category 
 */
export function dcgettext(domain, msgid, category) {
    return GLib.dcgettext(domain, msgid, category);
}

/**
 * 
 * @param {*} msgid1 
 * @param {*} msgid2 
 * @param {*} n 
 */
export function ngettext(msgid1, msgid2, n) {
    return GLib.dngettext(null, msgid1, msgid2, n);
}

/**
 * 
 * @param {*} domain 
 * @param {*} msgid1 
 * @param {*} msgid2 
 * @param {*} n 
 */
export function dngettext(domain, msgid1, msgid2, n) {
    return GLib.dngettext(domain, msgid1, msgid2, n);
}
// FIXME: missing dcngettext ?

/**
 * 
 * @param {*} context 
 * @param {*} msgid 
 */
export function pgettext(context, msgid) {
    return GLib.dpgettext2(null, context, msgid);
}

/**
 * 
 * @param {*} domain 
 * @param {*} context 
 * @param {*} msgid 
 */
export function dpgettext(domain, context, msgid) {
    return GLib.dpgettext2(domain, context, msgid);
}

/**
 * Create an object with bindings for gettext, ngettext,
 * and pgettext bound to a particular translation domain.
 *
 * @type {(domainName: string) => 
 *    ({ 
 *         gettext: (msgid: string) => string;
 *         ngettext: (msgid1: string, msgid2, n) => string;
 *         pgettext: (context: string, msgid: string) => string;
 *    })
 * }
 * @param domainName Translation domain string
 * @returns an object with gettext bindings
 */
export const domain = (domainName) => {
    return {
        gettext: (msgid) => GLib.dgettext(domainName, msgid),
        ngettext: (msgid1, msgid2, n) => GLib.dngettext(domainName, msgid1, msgid2, n),
        pgettext: (context, msgid) => GLib.dpgettext2(domainName, context, msgid)
    };
};

