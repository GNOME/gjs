/* This file contains code derived from xpcdebug.cpp in Mozilla.  The license
 * for that file follows:
 */
/*
 * ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is Mozilla Communicator client code, released
 * March 31, 1998.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1999
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   John Bandhauer <jband@netscape.com> (original author)
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either of the GNU General Public License Version 2 or later (the "GPL"),
 * or the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

#include <stdio.h>  // for stderr

#include <glib-object.h>
#include <glib.h>

#include "gjs/jsapi-wrapper.h"

#include "gjs/context.h"

void
gjs_context_print_stack_stderr(GjsContext *context)
{
    JSContext *cx = (JSContext*) gjs_context_get_native_context(context);

    g_printerr("== Stack trace for context %p ==\n", context);
    js::DumpBacktrace(cx, stderr);
}

void
gjs_dumpstack(void)
{
    GList *contexts = gjs_context_get_all();
    GList *iter;

    for (iter = contexts; iter; iter = iter->next) {
        GjsContext *context = (GjsContext*)iter->data;
        gjs_context_print_stack_stderr(context);
        g_object_unref(context);
    }
    g_list_free(contexts);
}
