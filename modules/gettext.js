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

/**
 * This module provides bindings for the "gettext" family of functions.
 * Usage:
 *
 * const Gettext = imports.gettext;
 *
 * Gettext.textdomain("myapp");
 * Gettext.bindtextdomain("myapp", "/usr/share/locale");
 *
 * let translated = Gettext.gettext("Hello world!");
 */

const Lang = imports.lang;

// Merge stuff defined in native code
Lang.copyProperties(imports.gettextNative, this);

/**
 * Create an object with bindings for gettext, ngettext,
 * and pgettext bound to a particular translation domain.
 *
 * @param domainName Translation domain string
 * @returns: an object with gettext bindings
 * @type: function
 */
var domain = function(domainName) {
    return {
        gettext: function(msgid) {
            return dgettext(domainName, msgid);
        },

        ngettext: function(msgid1, msgid2, n) {
            return dngettext(domainName, msgid1, msgid2, n);
        },

        pgettext: function(context, msgid) {
            return dpgettext(domainName, context, msgid);
        }
    }
};

