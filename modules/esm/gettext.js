// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2009 Red Hat, Inc.
// SPDX-FileCopyrightText: 2020 Evan Welsh <contact@evanwelsh.com>

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

import GLib from 'gi://GLib';
import GjsPrivate from 'gi://GjsPrivate';

export let LocaleCategory = GjsPrivate.LocaleCategory;

export function setlocale(category, locale) {
    return GjsPrivate.setlocale(category, locale);
}

export function textdomain(dom) {
    return GjsPrivate.textdomain(dom);
}

export function bindtextdomain(dom, location) {
    return GjsPrivate.bindtextdomain(dom, location);
}

export function gettext(msgid) {
    return GLib.dgettext(null, msgid);
}

export function dgettext(dom, msgid) {
    return GLib.dgettext(dom, msgid);
}

export function dcgettext(dom, msgid, category) {
    return GLib.dcgettext(dom, msgid, category);
}

export function ngettext(msgid1, msgid2, n) {
    return GLib.dngettext(null, msgid1, msgid2, n);
}

export function dngettext(dom, msgid1, msgid2, n) {
    return GLib.dngettext(dom, msgid1, msgid2, n);
}

// FIXME: missing dcngettext ?

export function pgettext(context, msgid) {
    return GLib.dpgettext2(null, context, msgid);
}

export function dpgettext(dom, context, msgid) {
    return GLib.dpgettext2(dom, context, msgid);
}

/**
 * Create an object with bindings for gettext, ngettext,
 * and pgettext bound to a particular translation domain.
 *
 * @param {string} domainName Translation domain string
 * @returns {object} an object with gettext bindings
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
        },
    };
}

export default {
    LocaleCategory,
    setlocale,
    textdomain,
    bindtextdomain,
    gettext,
    dgettext,
    dcgettext,
    ngettext,
    dngettext,
    pgettext,
    dpgettext,
    domain,
};
