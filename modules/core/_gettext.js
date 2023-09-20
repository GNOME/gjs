// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2009 Red Hat, Inc.

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

const GLib = imports.gi.GLib;
const GjsPrivate = imports.gi.GjsPrivate;

var LocaleCategory = GjsPrivate.LocaleCategory;

function setlocale(category, locale) {
    return GjsPrivate.set_thread_locale(category, locale);
}

function textdomain(dom) {
    return GjsPrivate.textdomain(dom);
}
function bindtextdomain(dom, location) {
    return GjsPrivate.bindtextdomain(dom, location);
}

function gettext(msgid) {
    return GLib.dgettext(null, msgid);
}
function dgettext(dom, msgid) {
    return GLib.dgettext(dom, msgid);
}
function dcgettext(dom, msgid, category) {
    return GLib.dcgettext(dom, msgid, category);
}

function ngettext(msgid1, msgid2, n) {
    return GLib.dngettext(null, msgid1, msgid2, n);
}
function dngettext(dom, msgid1, msgid2, n) {
    return GLib.dngettext(dom, msgid1, msgid2, n);
}
// FIXME: missing dcngettext ?

function pgettext(context, msgid) {
    return GLib.dpgettext2(null, context, msgid);
}
function dpgettext(dom, context, msgid) {
    return GLib.dpgettext2(dom, context, msgid);
}

/**
 * Create an object with bindings for gettext, ngettext,
 * and pgettext bound to a particular translation domain.
 *
 * @param {string} domainName Translation domain string
 * @returns {object} an object with gettext bindings
 */
function domain(domainName) {
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
