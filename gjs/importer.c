/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * Copyright (c) 2008-2010  litl, LLC
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <config.h>

#include <util/log.h>
#include <util/glib.h>

#include <gjs/gjs-module.h>
#include <gjs/importer.h>
#include <gjs/compat.h>

#include <string.h>

#define MODULE_INIT_PROPERTY "__init__"
#define MODULE_INIT_FILENAME MODULE_INIT_PROPERTY".js"

static char **gjs_search_path = NULL;

typedef struct {
    void *dummy;
} Importer;

typedef struct {
    GPtrArray *elements;
    unsigned int index;
} ImporterIterator;

static struct JSClass gjs_importer_class;

GJS_DEFINE_PRIV_FROM_JS(Importer, gjs_importer_class)

static JSBool
define_meta_properties(JSContext  *context,
                       JSObject   *module_obj,
                       const char *module_name,
                       JSObject   *parent)
{
    gboolean parent_is_module;

    /* We define both __moduleName__ and __parentModule__ to null
     * on the root importer
     */
    parent_is_module = JS_InstanceOf(context, parent, &gjs_importer_class, NULL);

    gjs_debug(GJS_DEBUG_IMPORTER, "Defining parent %p of %p '%s' is mod %d",
              parent, module_obj, module_name ? module_name : "<root>", parent_is_module);

    if (!JS_DefineProperty(context, module_obj,
                           "__moduleName__",
                           parent_is_module ?
                           STRING_TO_JSVAL(JS_NewStringCopyZ(context, module_name)) :
                           JSVAL_NULL,
                           NULL, NULL,
                           /* don't set ENUMERATE since we wouldn't want to copy
                            * this symbol to any other object for example.
                            */
                           JSPROP_READONLY | JSPROP_PERMANENT))
        return JS_FALSE;

    if (!JS_DefineProperty(context, module_obj,
                           "__parentModule__",
                           parent_is_module ? OBJECT_TO_JSVAL(parent) : JSVAL_NULL,
                           NULL, NULL,
                           /* don't set ENUMERATE since we wouldn't want to copy
                            * this symbol to any other object for example.
                            */
                           JSPROP_READONLY | JSPROP_PERMANENT))
        return JS_FALSE;

    return JS_TRUE;
}

static JSBool
import_directory(JSContext   *context,
                 JSObject    *obj,
                 const char  *name,
                 const char **full_paths)
{
    JSObject *importer;

    gjs_debug(GJS_DEBUG_IMPORTER,
              "Importing directory '%s'",
              name);

    /* We define a sub-importer that has only the given directories on
     * its search path. gjs_define_importer() exits if it fails, so
     * this always succeeds.
     */
    importer = gjs_define_importer(context, obj, name, full_paths, FALSE);
    if (importer == NULL)
        return JS_FALSE;

    return JS_TRUE;
}

static JSBool
finish_import(JSContext  *context,
              const char *name)
{
    if (JS_IsExceptionPending(context)) {
        /* I am not sure whether this can happen, but if it does we want to trap it.
         */
        gjs_debug(GJS_DEBUG_IMPORTER,
                  "Module '%s' reported an exception but gjs_import_native_module() returned TRUE",
                  name);
        return JS_FALSE;
    }

    return JS_TRUE;
}

static JSBool
define_import(JSContext  *context,
              JSObject   *obj,
              JSObject   *module_obj,
              const char *name)
{
    if (!JS_DefineProperty(context, obj,
                           name, OBJECT_TO_JSVAL(module_obj),
                           NULL, NULL,
                           GJS_MODULE_PROP_FLAGS & ~JSPROP_PERMANENT)) {
        gjs_debug(GJS_DEBUG_IMPORTER,
                  "Failed to define '%s' in importer",
                  name);
        return JS_FALSE;
    }

    return JS_TRUE;
}

/* Make the property we set in define_import permament;
 * we do this after the import succesfully completes.
 */
static JSBool
seal_import(JSContext  *context,
            JSObject   *obj,
            const char *name)
{
    JSBool found;
    uintN attrs;

    if (!JS_GetPropertyAttributes(context, obj, name,
                                  &attrs, &found) || !found) {
        gjs_debug(GJS_DEBUG_IMPORTER,
                  "Failed to get attributes to seal '%s' in importer",
                  name);
        return JS_FALSE;
    }

    attrs |= JSPROP_PERMANENT;

    if (!JS_SetPropertyAttributes(context, obj, name,
                                  attrs, &found) || !found) {
        gjs_debug(GJS_DEBUG_IMPORTER,
                  "Failed to set attributes to seal '%s' in importer",
                  name);
        return JS_FALSE;
    }

    return JS_TRUE;
}

/* An import failed. Delete the property pointing to the import
 * from the parent namespace. In complicated situations this might
 * not be sufficient to get us fully back to a sane state. If:
 *
 *  - We import module A
 *  - module A imports module B
 *  - module B imports module A, storing a reference to the current
 *    module A module object
 *  - module A subsequently throws an exception
 *
 * Then module B is left imported, but the imported module B has
 * a reference to the failed module A module object. To handle this
 * we could could try to track the entire "import operation" and
 * roll back *all* modifications made to the namespace objects.
 * It's not clear that the complexity would be worth the small gain
 * in robustness. (You can still come up with ways of defeating
 * the attempt to clean up.)
 */
static void
cancel_import(JSContext  *context,
              JSObject   *obj,
              const char *name)
{
    gjs_debug(GJS_DEBUG_IMPORTER,
              "Cleaning up from failed import of '%s'",
              name);

    if (!JS_DeleteProperty(context, obj, name)) {
        gjs_debug(GJS_DEBUG_IMPORTER,
                  "Failed to delete '%s' in importer",
                  name);
    }
}

static JSBool
import_native_file(JSContext  *context,
                   JSObject   *obj,
                   const char *name,
                   const char *full_path)
{
    JSObject *module_obj;
    GjsNativeFlags flags;
    JSBool retval = JS_FALSE;

    gjs_debug(GJS_DEBUG_IMPORTER,
              "Importing '%s' from '%s'", name, full_path ? full_path : "<internal>");

    module_obj = JS_ConstructObject(context, NULL, NULL, NULL);
    if (module_obj == NULL) {
        return JS_FALSE;
    }

    /* We store the module object into the parent module before
     * initializing the module. If the module has the
     * GJS_NATIVE_SUPPLIES_MODULE_OBJ flag, it will just overwrite
     * the reference we stored when it initializes.
     */
    if (!define_import(context, obj, module_obj, name))
        return JS_FALSE;

    if (!define_meta_properties(context, module_obj, name, obj))
        goto out;

    if (!gjs_import_native_module(context, module_obj, full_path, &flags))
        goto out;

    if (!finish_import(context, name))
        goto out;

    if (!seal_import(context, obj, name))
        goto out;

    retval = JS_TRUE;

 out:
    if (!retval)
        cancel_import(context, obj, name);

    return retval;
}

static JSObject *
load_module_init(JSContext  *context,
                 JSObject   *in_object,
                 const char *full_path)
{
    char *script;
    gsize script_len;
    jsval script_retval;
    JSObject *module_obj;

    /* First we check if js module has already been loaded  */
    if (gjs_object_has_property(context, in_object, MODULE_INIT_PROPERTY)) {
        jsval module_obj_val;

        if (gjs_object_get_property(context,
                                    in_object,
                                    MODULE_INIT_PROPERTY,
                                    &module_obj_val)) {
            return JSVAL_TO_OBJECT(module_obj_val);
        }
    }

    module_obj = JS_NewObject(context, NULL, NULL, NULL);
    if (module_obj == NULL) {
        return JS_FALSE;
    }

    /* https://bugzilla.mozilla.org/show_bug.cgi?id=599651 means we
     * can't just pass in the global as the parent */
    JS_SetParent(context, module_obj,
                 gjs_get_import_global (context));

    /* Define module in importer for future use and to avoid module_obj
     * object to be garbage collected during the evaluation of the script */
    JS_DefineProperty(context, in_object,
                      MODULE_INIT_PROPERTY, OBJECT_TO_JSVAL(module_obj),
                      NULL, NULL,
                      GJS_MODULE_PROP_FLAGS & ~JSPROP_PERMANENT);

    script = NULL;
    script_len = 0;

    if (!g_file_get_contents(full_path, &script, &script_len, NULL)) {
        return NULL;
    }

    g_assert(script != NULL);

    gjs_debug(GJS_DEBUG_IMPORTER, "Importing %s", full_path);

    if (!JS_EvaluateScript(context,
                           module_obj,
                           script,
                           script_len,
                           full_path,
                           1, /* line number */
                           &script_retval)) {
        g_free(script);

        /* If JSOPTION_DONT_REPORT_UNCAUGHT is set then the exception
         * would be left set after the evaluate and not go to the error
         * reporter function.
         */
        if (JS_IsExceptionPending(context)) {
            gjs_debug(GJS_DEBUG_IMPORTER,
                      "Module " MODULE_INIT_FILENAME " left an exception set");
            gjs_log_and_keep_exception(context, NULL);
        } else {
            gjs_throw(context,
                      "JS_EvaluateScript() returned FALSE but did not set exception");
        }

        return NULL;
    }

    g_free(script);

    return module_obj;
}

static void
load_module_elements(JSContext *context,
                     JSObject *in_object,
                     ImporterIterator *iter,
                     const char *init_path) {
    JSObject *module_obj;
    JSObject *jsiter;

    module_obj = load_module_init(context, in_object, init_path);

    if (module_obj != NULL) {
        jsid idp;

        jsiter = JS_NewPropertyIterator(context, module_obj);

        if (jsiter == NULL) {
            return;
        }

        if (!JS_NextProperty(context, jsiter, &idp)) {
            return;
        }

        while (!JSID_IS_VOID(idp)) {
            char *name;

            if (!gjs_get_string_id(context, idp, &name)) {
                continue;
            }

            /* Pass ownership of name */
            g_ptr_array_add(iter->elements, name);

            if (!JS_NextProperty(context, jsiter, &idp)) {
                break;
            }
        }
    }
}

static JSBool
import_file(JSContext  *context,
            JSObject   *obj,
            const char *name,
            const char *full_path)
{
    char *script;
    gsize script_len;
    JSObject *module_obj;
    GError *error;
    jsval script_retval;
    JSBool retval = JS_FALSE;

    gjs_debug(GJS_DEBUG_IMPORTER,
              "Importing '%s'", full_path);

    module_obj = JS_ConstructObject(context, NULL, NULL, NULL);
    if (module_obj == NULL) {
        return JS_FALSE;
    }

    if (!define_import(context, obj, module_obj, name))
        return JS_FALSE;

    if (!define_meta_properties(context, module_obj, name, obj))
        goto out;

    script = NULL;
    script_len = 0;

    error = NULL;
    if (!g_file_get_contents(full_path, &script, &script_len, &error)) {
        gjs_throw(context, "Could not open %s: %s", full_path, error->message);
        g_error_free(error);
        goto out;
    }

    g_assert(script != NULL);

    if (!JS_EvaluateScript(context,
                           module_obj,
                           script,
                           script_len,
                           full_path,
                           1, /* line number */
                           &script_retval)) {
        g_free(script);

        /* If JSOPTION_DONT_REPORT_UNCAUGHT is set then the exception
         * would be left set after the evaluate and not go to the error
         * reporter function.
         */
        if (JS_IsExceptionPending(context)) {
            gjs_debug(GJS_DEBUG_IMPORTER,
                      "Module '%s' left an exception set",
                      name);
            gjs_log_and_keep_exception(context, NULL);
        } else {
            gjs_throw(context,
                         "JS_EvaluateScript() returned FALSE but did not set exception");
        }

        goto out;
    }

    g_free(script);

    if (!finish_import(context, name))
        goto out;

    if (!seal_import(context, obj, name))
        goto out;

    retval = JS_TRUE;

 out:
    if (!retval)
        cancel_import(context, obj, name);

    return retval;
}

static JSBool
do_import(JSContext  *context,
          JSObject   *obj,
          Importer   *priv,
          const char *name)
{
    char *filename;
    char *native_filename;
    char *full_path;
    char *dirname = NULL;
    jsval search_path_val;
    JSObject *search_path;
    JSObject *module_obj = NULL;
    jsuint search_path_len;
    jsuint i;
    JSBool result;
    GPtrArray *directories;

    if (strcmp(name, MODULE_INIT_PROPERTY) == 0) {
        return JS_FALSE;
    }

    if (!gjs_object_require_property(context, obj, "importer", "searchPath", &search_path_val)) {
        return JS_FALSE;
    }

    if (!JSVAL_IS_OBJECT(search_path_val)) {
        gjs_throw(context, "searchPath property on importer is not an object");
        return JS_FALSE;
    }

    search_path = JSVAL_TO_OBJECT(search_path_val);

    if (!JS_IsArrayObject(context, search_path)) {
        gjs_throw(context, "searchPath property on importer is not an array");
        return JS_FALSE;
    }

    if (!JS_GetArrayLength(context, search_path, &search_path_len)) {
        gjs_throw(context, "searchPath array has no length");
        return JS_FALSE;
    }

    result = JS_FALSE;

    filename = g_strdup_printf("%s.js", name);
    native_filename = g_strdup_printf("%s."G_MODULE_SUFFIX, name);
    full_path = NULL;
    directories = NULL;

    /* First try importing an internal module like byteArray */
    if (gjs_is_registered_native_module(context, obj, name) &&
        import_native_file(context, obj, name, NULL)) {
        gjs_debug(GJS_DEBUG_IMPORTER,
                  "successfully imported module '%s'", name);
        result = JS_TRUE;
        goto out;
    }

    for (i = 0; i < search_path_len; ++i) {
        jsval elem;

        elem = JSVAL_VOID;
        if (!JS_GetElement(context, search_path, i, &elem)) {
            /* this means there was an exception, while elem == JSVAL_VOID
             * means no element found
             */
            goto out;
        }

        if (JSVAL_IS_VOID(elem))
            continue;

        if (!JSVAL_IS_STRING(elem)) {
            gjs_throw(context, "importer searchPath contains non-string");
            goto out;
        }

        g_free(dirname);
        dirname = NULL;

        if (!gjs_string_to_utf8(context, elem, &dirname))
            goto out; /* Error message already set */

        /* Ignore empty path elements */
        if (dirname[0] == '\0')
            continue;

        /* Try importing __init__.js and loading the symbol from it */
        if (full_path)
            g_free(full_path);
        full_path = g_build_filename(dirname, MODULE_INIT_FILENAME,
                                     NULL);

        module_obj = load_module_init(context, obj, full_path);
        if (module_obj != NULL) {
            jsval obj_val;

            if (gjs_object_get_property(context,
                                        module_obj,
                                        name,
                                        &obj_val)) {
                if (!JSVAL_IS_VOID(obj_val) &&
                    JS_DefineProperty(context, obj,
                                      name, obj_val,
                                      NULL, NULL,
                                      GJS_MODULE_PROP_FLAGS & ~JSPROP_PERMANENT)) {
                    result = JS_TRUE;
                    goto out;
                }
            }
        }

        /* Second try importing a directory (a sub-importer) */
        if (full_path)
            g_free(full_path);
        full_path = g_build_filename(dirname, name,
                                     NULL);

        if (g_file_test(full_path, G_FILE_TEST_IS_DIR)) {
            gjs_debug(GJS_DEBUG_IMPORTER,
                      "Adding directory '%s' to child importer '%s'",
                      full_path, name);
            if (directories == NULL) {
                directories = g_ptr_array_new();
            }
            g_ptr_array_add(directories, full_path);
            /* don't free it twice - pass ownership to ptr array */
            full_path = NULL;
        }

        /* If we just added to directories, we know we don't need to
         * check for a file.  If we added to directories on an earlier
         * iteration, we want to ignore any files later in the
         * path. So, always skip the rest of the loop block if we have
         * directories.
         */
        if (directories != NULL) {
            continue;
        }

        /* Third, if it's not a directory, try importing a file */
        g_free(full_path);
        full_path = g_build_filename(dirname, filename,
                                     NULL);

        if (g_file_test(full_path, G_FILE_TEST_EXISTS)) {
            if (import_file(context, obj, name, full_path)) {
                gjs_debug(GJS_DEBUG_IMPORTER,
                          "successfully imported module '%s'", name);
                result = JS_TRUE;
            }

            /* Don't keep searching path if we fail to load the file for
             * reasons other than it doesn't exist... i.e. broken files
             * block searching for nonbroken ones
             */
            goto out;
        }

        /* Finally see if it's a native module */
        g_free(full_path);
        full_path = g_build_filename(dirname, native_filename,
                                     NULL);

        if (g_file_test(full_path, G_FILE_TEST_EXISTS)) {
            if (import_native_file(context, obj, name, full_path)) {
                gjs_debug(GJS_DEBUG_IMPORTER,
                          "successfully imported module '%s'", name);
                result = JS_TRUE;
            }

            /* Don't keep searching path if we fail to load the file for
             * reasons other than it doesn't exist... i.e. broken files
             * block searching for nonbroken ones
             */
            goto out;
        }

        gjs_debug(GJS_DEBUG_IMPORTER,
                  "JS import '%s' not found in %s",
                  name, dirname);
    }

    if (directories != NULL) {
        /* NULL-terminate the char** */
        g_ptr_array_add(directories, NULL);

        if (import_directory(context, obj, name,
                             (const char**) directories->pdata)) {
            gjs_debug(GJS_DEBUG_IMPORTER,
                      "successfully imported directory '%s'", name);
            result = JS_TRUE;
        }
    }

 out:
    if (directories != NULL) {
        char **str_array;

        /* NULL-terminate the char**
         * (maybe for a second time, but doesn't matter)
         */
        g_ptr_array_add(directories, NULL);

        str_array = (char**) directories->pdata;
        g_ptr_array_free(directories, FALSE);
        g_strfreev(str_array);
    }

    g_free(full_path);
    g_free(filename);
    g_free(native_filename);
    g_free(dirname);

    if (!result &&
        !JS_IsExceptionPending(context)) {
        /* If no exception occurred, the problem is just that we got to the
         * end of the path. Be sure an exception is set.
         */
        gjs_throw(context, "No JS module '%s' found in search path", name);
    }

    return result;
}

static ImporterIterator *
importer_iterator_new()
{
    ImporterIterator *iter;

    iter = g_slice_new0(ImporterIterator);

    iter->elements = g_ptr_array_new();
    iter->index = 0;

    return iter;
}

static void
importer_iterator_free(ImporterIterator *iter)
{
    g_ptr_array_foreach(iter->elements, (GFunc)g_free, NULL);
    g_ptr_array_free(iter->elements, TRUE);
    g_slice_free(ImporterIterator, iter);
}

/*
 * Like JSEnumerateOp, but enum provides contextual information as follows:
 *
 * JSENUMERATE_INIT: allocate private enum struct in state_p, return number
 * of elements in *id_p
 * JSENUMERATE_NEXT: return next property id in *id_p, and if no new property
 * free state_p and set to JSVAL_NULL
 * JSENUMERATE_DESTROY : destroy state_p
 *
 * Note that in a for ... in loop, this will be called first on the object,
 * then on its prototype.
 *
 */
static JSBool
importer_new_enumerate(JSContext  *context,
                       JSObject   *object,
                       JSIterateOp enum_op,
                       jsval      *state_p,
                       jsid       *id_p)
{
    ImporterIterator *iter;

    switch (enum_op) {
    case JSENUMERATE_INIT_ALL:
    case JSENUMERATE_INIT: {
        Importer *priv;
        JSObject *search_path;
        jsval search_path_val;
        jsuint search_path_len;
        jsuint i;

        if (state_p)
            *state_p = JSVAL_NULL;

        if (id_p)
            *id_p = INT_TO_JSID(0);

        priv = priv_from_js(context, object);
        if (!priv)
            /* we are enumerating the prototype properties */
            return JS_TRUE;

        if (!gjs_object_require_property(context, object, "importer", "searchPath", &search_path_val))
            return JS_FALSE;

        if (!JSVAL_IS_OBJECT(search_path_val)) {
            gjs_throw(context, "searchPath property on importer is not an object");
            return JS_FALSE;
        }

        search_path = JSVAL_TO_OBJECT(search_path_val);

        if (!JS_IsArrayObject(context, search_path)) {
            gjs_throw(context, "searchPath property on importer is not an array");
            return JS_FALSE;
        }

        if (!JS_GetArrayLength(context, search_path, &search_path_len)) {
            gjs_throw(context, "searchPath array has no length");
            return JS_FALSE;
        }

        iter = importer_iterator_new();

        for (i = 0; i < search_path_len; ++i) {
            char *dirname = NULL;
            char *init_path;
            const char *filename;
            jsval elem;
            GDir *dir = NULL;

            elem = JSVAL_VOID;
            if (!JS_GetElement(context, search_path, i, &elem)) {
                /* this means there was an exception, while elem == JSVAL_VOID
                 * means no element found
                 */
                importer_iterator_free(iter);
                return JS_FALSE;
            }

            if (JSVAL_IS_VOID(elem))
                continue;

            if (!JSVAL_IS_STRING(elem)) {
                gjs_throw(context, "importer searchPath contains non-string");
                importer_iterator_free(iter);
                return JS_FALSE;
            }

            if (!gjs_string_to_utf8(context, elem, &dirname)) {
                importer_iterator_free(iter);
                return JS_FALSE; /* Error message already set */
            }

            init_path = g_build_filename(dirname, MODULE_INIT_FILENAME,
                                         NULL);

            load_module_elements(context, object, iter, init_path);

            g_free(init_path);

            dir = g_dir_open(dirname, 0, NULL);

            if (!dir) {
                g_free(dirname);
                continue;
            }

            while ((filename = g_dir_read_name(dir))) {
                char *full_path;

                /* skip hidden files and directories (.svn, .git, ...) */
                if (filename[0] == '.')
                    continue;

                /* skip module init file */
                if (strcmp(filename, MODULE_INIT_FILENAME) == 0)
                    continue;

                full_path = g_build_filename(dirname, filename, NULL);

                if (g_file_test(full_path, G_FILE_TEST_IS_DIR)) {
                    g_ptr_array_add(iter->elements, g_strdup(filename));
                } else {
                    if (g_str_has_suffix(filename, "."G_MODULE_SUFFIX) ||
                        g_str_has_suffix(filename, ".js")) {
                        g_ptr_array_add(iter->elements,
                                        g_strndup(filename, strlen(filename) - 3));
                    }
                }

                g_free(full_path);
            }
            g_dir_close(dir);

            g_free(dirname);
        }

        if (state_p)
            *state_p = PRIVATE_TO_JSVAL(iter);

        if (id_p)
            *id_p = INT_TO_JSID(iter->elements->len);

        break;
    }

    case JSENUMERATE_NEXT: {
        jsval element_val;

        if (!state_p) {
            gjs_throw(context, "Enumerate with no iterator set?");
            return JS_FALSE;
        }

        if (JSVAL_IS_NULL(*state_p)) /* Iterating prototype */
            return JS_TRUE;

        iter = JSVAL_TO_PRIVATE(*state_p);

        if (iter->index < iter->elements->len) {
            if (!gjs_string_from_utf8(context,
                                         g_ptr_array_index(iter->elements,
                                                           iter->index++),
                                         -1,
                                         &element_val))
                return JS_FALSE;

            if (!JS_ValueToId(context, element_val, id_p))
                return JS_FALSE;

            break;
        }
        /* else fall through to destroying the iterator */
    }

    case JSENUMERATE_DESTROY: {
        if (state_p && !JSVAL_IS_NULL(*state_p)) {
            iter = JSVAL_TO_PRIVATE(*state_p);

            importer_iterator_free(iter);

            *state_p = JSVAL_NULL;
        }
    }
    }

    return JS_TRUE;
}

/*
 * Like JSResolveOp, but flags provide contextual information as follows:
 *
 *  JSRESOLVE_QUALIFIED   a qualified property id: obj.id or obj[id], not id
 *  JSRESOLVE_ASSIGNING   obj[id] is on the left-hand side of an assignment
 *  JSRESOLVE_DETECTING   'if (o.p)...' or similar detection opcode sequence
 *  JSRESOLVE_DECLARING   var, const, or function prolog declaration opcode
 *  JSRESOLVE_CLASSNAME   class name used when constructing
 *
 * The *objp out parameter, on success, should be null to indicate that id
 * was not resolved; and non-null, referring to obj or one of its prototypes,
 * if id was resolved.
 */
static JSBool
importer_new_resolve(JSContext *context,
                     JSObject  *obj,
                     jsid       id,
                     uintN      flags,
                     JSObject **objp)
{
    Importer *priv;
    char *name;
    JSBool ret = JS_TRUE;

    *objp = NULL;

    if (!gjs_get_string_id(context, id, &name))
        return JS_FALSE;

    /* let Object.prototype resolve these */
    if (strcmp(name, "valueOf") == 0 ||
        strcmp(name, "toString") == 0 ||
        strcmp(name, "__iterator__") == 0)
        goto out;

    priv = priv_from_js(context, obj);
    gjs_debug_jsprop(GJS_DEBUG_IMPORTER, "Resolve prop '%s' hook obj %p priv %p", name, obj, priv);

    if (priv == NULL) /* we are the prototype, or have the wrong class */
        goto out;

    JS_BeginRequest(context);
    if (do_import(context, obj, priv, name)) {
        *objp = obj;
    } else {
        ret = JS_FALSE;
    }
    JS_EndRequest(context);

 out:
    g_free(name);
    return ret;
}

/* If we set JSCLASS_CONSTRUCT_PROTOTYPE flag, then this is called on
 * the prototype in addition to on each instance. When called on the
 * prototype, "obj" is the prototype, and "retval" is the prototype
 * also, but can be replaced with another object to use instead as the
 * prototype. If we don't set JSCLASS_CONSTRUCT_PROTOTYPE we can
 * identify the prototype as an object of our class with NULL private
 * data.
 */
GJS_NATIVE_CONSTRUCTOR_DECLARE(importer)
{
    GJS_NATIVE_CONSTRUCTOR_VARIABLES(importer)
    Importer *priv;

    GJS_NATIVE_CONSTRUCTOR_PRELUDE(importer);

    priv = g_slice_new0(Importer);

    GJS_INC_COUNTER(importer);

    g_assert(priv_from_js(context, object) == NULL);
    JS_SetPrivate(context, object, priv);

    gjs_debug_lifecycle(GJS_DEBUG_IMPORTER,
                        "importer constructor, obj %p priv %p", object, priv);

    GJS_NATIVE_CONSTRUCTOR_FINISH(importer);

    return JS_TRUE;
}

static void
importer_finalize(JSContext *context,
                  JSObject  *obj)
{
    Importer *priv;

    priv = priv_from_js(context, obj);
    gjs_debug_lifecycle(GJS_DEBUG_IMPORTER,
                        "finalize, obj %p priv %p", obj, priv);
    if (priv == NULL)
        return; /* we are the prototype, not a real instance, so constructor never called */

    GJS_DEC_COUNTER(importer);
    g_slice_free(Importer, priv);
}

/* The bizarre thing about this vtable is that it applies to both
 * instances of the object, and to the prototype that instances of the
 * class have.
 *
 * Also, there's a constructor field in here, but as far as I can
 * tell, it would only be used if no constructor were provided to
 * JS_InitClass. The constructor from JS_InitClass is not applied to
 * the prototype unless JSCLASS_CONSTRUCT_PROTOTYPE is in flags.
 */
static struct JSClass gjs_importer_class = {
    "GjsFileImporter",
    JSCLASS_HAS_PRIVATE |
    JSCLASS_NEW_RESOLVE |
    JSCLASS_NEW_RESOLVE_GETS_START |
    JSCLASS_NEW_ENUMERATE,
    JS_PropertyStub,
    JS_PropertyStub,
    JS_PropertyStub,
    JS_StrictPropertyStub,
    (JSEnumerateOp) importer_new_enumerate, /* needs cast since it's the new enumerate signature */
    (JSResolveOp) importer_new_resolve, /* needs cast since it's the new resolve signature */
    JS_ConvertStub,
    importer_finalize,
    JSCLASS_NO_OPTIONAL_MEMBERS
};

static JSPropertySpec gjs_importer_proto_props[] = {
    { NULL }
};

static JSFunctionSpec gjs_importer_proto_funcs[] = {
    { NULL }
};

static JSObject*
importer_new(JSContext    *context)
{
    JSObject *importer;
    Importer *priv;
    JSObject *global;
    (void) priv;

    global = gjs_get_import_global(context);

    if (!gjs_object_has_property(context, global, gjs_importer_class.name)) {
        JSObject *prototype;
        prototype = JS_InitClass(context, global,
                                 /* parent prototype JSObject* for
                                  * prototype; NULL for
                                  * Object.prototype
                                  */
                                 NULL,
                                 &gjs_importer_class,
                                 /* constructor for instances (NULL for
                                  * none - just name the prototype like
                                  * Math - rarely correct)
                                  */
                                 gjs_importer_constructor,
                                 /* number of constructor args */
                                 0,
                                 /* props of prototype */
                                 &gjs_importer_proto_props[0],
                                 /* funcs of prototype */
                                 &gjs_importer_proto_funcs[0],
                                 /* props of constructor, MyConstructor.myprop */
                                 NULL,
                                 /* funcs of constructor, MyConstructor.myfunc() */
                                 NULL);
        if (prototype == NULL)
            gjs_fatal("Can't init class %s", gjs_importer_class.name);

        g_assert(gjs_object_has_property(context, global, gjs_importer_class.name));

        gjs_debug(GJS_DEBUG_IMPORTER, "Initialized class %s prototype %p",
                  gjs_importer_class.name, prototype);
    }

    importer = JS_ConstructObject(context, &gjs_importer_class, NULL, global);
    if (importer == NULL)
        gjs_fatal("No memory to create ns object");

    return importer;
}

static G_CONST_RETURN char * G_CONST_RETURN *
gjs_get_search_path(void)
{
    char **search_path;

    /* not thread safe */

    if (!gjs_search_path) {
        G_CONST_RETURN gchar* G_CONST_RETURN * system_data_dirs;
        const char *envstr;
        GPtrArray *path;
        gsize i;

        path = g_ptr_array_new();

        /* in order of priority */

        /* $GJS_PATH */
        envstr = g_getenv("GJS_PATH");
        if (envstr) {
            char **dirs, **d;
            dirs = g_strsplit(envstr, G_SEARCHPATH_SEPARATOR_S, 0);
            for (d = dirs; *d != NULL; d++)
                g_ptr_array_add(path, *d);
            /* we assume the array and strings are allocated separately */
            g_free(dirs);
        }

        /* $XDG_DATA_DIRS /gjs-1.0 */
        system_data_dirs = g_get_system_data_dirs();
        for (i = 0; system_data_dirs[i] != NULL; ++i) {
            char *s;
            s = g_build_filename(system_data_dirs[i], "gjs-1.0", NULL);
            g_ptr_array_add(path, s);
        }

        /* ${libdir}/gjs-1.0 */
        g_ptr_array_add(path, g_strdup(GJS_NATIVE_DIR));

        /* ${datadir}/share/gjs-1.0 */
        g_ptr_array_add(path, g_strdup(GJS_JS_DIR));

        g_ptr_array_add(path, NULL);

        search_path = (char**)g_ptr_array_free(path, FALSE);

        gjs_search_path = search_path;
    } else {
        search_path = gjs_search_path;
    }

    return (G_CONST_RETURN char * G_CONST_RETURN *)search_path;
}

JSObject*
gjs_define_importer(JSContext    *context,
                    JSObject     *in_object,
                    const char   *importer_name,
                    const char  **initial_search_path,
                    gboolean      add_standard_search_path)
{
    JSObject *importer;
    char **paths[2] = {0};
    char **search_path;

    paths[0] = (char**)initial_search_path;
    if (add_standard_search_path) {
        /* Stick the "standard" shared search path after the provided one. */
        paths[1] = (char**)gjs_get_search_path();
    }

    search_path = gjs_g_strv_concat(paths, 2);

    importer = importer_new(context);

    /* API users can replace this property from JS, is the idea */
    if (!gjs_define_string_array(context, importer,
                                    "searchPath", -1, (const char **)search_path,
                                    /* settable (no READONLY) but not deleteable (PERMANENT) */
                                    JSPROP_PERMANENT | JSPROP_ENUMERATE))
        gjs_fatal("no memory to define importer search path prop");

    g_strfreev(search_path);

    if (!define_meta_properties(context, importer, importer_name, in_object))
        gjs_fatal("failed to define meta properties on importer");

    if (!JS_DefineProperty(context, in_object,
                           importer_name, OBJECT_TO_JSVAL(importer),
                           NULL, NULL,
                           GJS_MODULE_PROP_FLAGS))
        gjs_fatal("no memory to define importer property");

    gjs_debug(GJS_DEBUG_IMPORTER,
              "Defined importer '%s' %p in %p", importer_name, importer, in_object);

    return importer;
}

/* If this were called twice for the same runtime with different args it
 * would basically be a bug, but checking for that is a lot of code so
 * we just ignore all calls after the first and hope the args are the same.
 */
JSBool
gjs_create_root_importer(JSContext   *context,
                         const char **initial_search_path,
                         gboolean     add_standard_search_path)
{
    JSObject *global;

    global = gjs_get_import_global(context);

    JS_BeginRequest(context);

    if (!gjs_object_has_property(context,
                                 global,
                                 "imports")) {
        if (gjs_define_importer(context, global,
                                "imports",
                                initial_search_path, add_standard_search_path) == NULL) {
            JS_EndRequest(context);
            return JS_FALSE;
        }
    } else {
        gjs_debug(GJS_DEBUG_IMPORTER,
                  "Someone else already created root importer, ignoring second request");
        JS_EndRequest(context);
        return JS_TRUE;
    }

    JS_EndRequest(context);
    return JS_TRUE;
}

JSBool
gjs_define_root_importer(JSContext   *context,
                         JSObject    *in_object,
                         const char  *importer_name)
{
    JSObject *global;
    jsval value;
    JSBool success;

    success = JS_FALSE;
    global = gjs_get_import_global(context);
    JS_BeginRequest(context);

    if (!gjs_object_require_property(context,
                                     global, "global object",
                                     "imports", &value) ||
        !JSVAL_IS_OBJECT(value)) {
        gjs_debug(GJS_DEBUG_IMPORTER, "Root importer did not exist, couldn't get from load context; must create it");
        goto fail;
    }

    if (!JS_DefineProperty(context, in_object,
                           importer_name, value,
                           NULL, NULL,
                           GJS_MODULE_PROP_FLAGS)) {
        gjs_debug(GJS_DEBUG_IMPORTER, "DefineProperty %s on %p failed",
                  importer_name, in_object);
        goto fail;
    }

    success = JS_TRUE;
 fail:
    JS_EndRequest(context);
    return success;
}
