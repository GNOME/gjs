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

#include "importer.h"
#include "jsapi-wrapper.h"
#include "mem.h"
#include "native.h"

#include <gio/gio.h>

#include <string.h>

#define MODULE_INIT_FILENAME "__init__.js"

static char **gjs_search_path = NULL;

typedef struct {
    bool is_root;
} Importer;

typedef struct {
    GPtrArray *elements;
    unsigned int index;
} ImporterIterator;

extern struct JSClass gjs_importer_class;

GJS_DEFINE_PRIV_FROM_JS(Importer, gjs_importer_class)

static bool
define_meta_properties(JSContext       *context,
                       JS::HandleObject module_obj,
                       const char      *full_path,
                       const char      *module_name,
                       JS::HandleObject parent)
{
    bool parent_is_module;

    /* We define both __moduleName__ and __parentModule__ to null
     * on the root importer
     */
    parent_is_module = parent && JS_InstanceOf(context, parent, &gjs_importer_class, NULL);

    gjs_debug(GJS_DEBUG_IMPORTER, "Defining parent %p of %p '%s' is mod %d",
              parent.get(), module_obj.get(),
              module_name ? module_name : "<root>", parent_is_module);

    if (full_path != NULL) {
        JS::RootedString file(context, JS_NewStringCopyZ(context, full_path));
        if (!JS_DefineProperty(context, module_obj, "__file__", file,
                               /* don't set ENUMERATE since we wouldn't want to copy
                                * this symbol to any other object for example.
                                */
                               JSPROP_READONLY | JSPROP_PERMANENT))
            return false;
    }

    JS::RootedValue module_name_val(context, JS::NullValue());
    JS::RootedValue parent_module_val(context, JS::NullValue());
    if (parent_is_module) {
        module_name_val.setString(JS_NewStringCopyZ(context, module_name));
        parent_module_val.setObject(*parent);
    }

    if (!JS_DefineProperty(context, module_obj,
                           "__moduleName__", module_name_val,
                           /* don't set ENUMERATE since we wouldn't want to copy
                            * this symbol to any other object for example.
                            */
                           JSPROP_READONLY | JSPROP_PERMANENT))
        return false;

    if (!JS_DefineProperty(context, module_obj,
                           "__parentModule__", parent_module_val,
                           /* don't set ENUMERATE since we wouldn't want to copy
                            * this symbol to any other object for example.
                            */
                           JSPROP_READONLY | JSPROP_PERMANENT))
        return false;

    return true;
}

static bool
import_directory(JSContext       *context,
                 JS::HandleObject obj,
                 const char      *name,
                 const char     **full_paths)
{
    JSObject *importer;

    gjs_debug(GJS_DEBUG_IMPORTER,
              "Importing directory '%s'",
              name);

    /* We define a sub-importer that has only the given directories on
     * its search path. gjs_define_importer() exits if it fails, so
     * this always succeeds.
     */
    importer = gjs_define_importer(context, obj, name, full_paths, false);
    return importer != NULL;
}

static bool
define_import(JSContext       *context,
              JS::HandleObject obj,
              JS::HandleObject module_obj,
              const char      *name)
{
    if (!JS_DefineProperty(context, obj, name, module_obj,
                           GJS_MODULE_PROP_FLAGS & ~JSPROP_PERMANENT)) {
        gjs_debug(GJS_DEBUG_IMPORTER,
                  "Failed to define '%s' in importer",
                  name);
        return false;
    }

    return true;
}

/* Make the property we set in define_import permament;
 * we do this after the import succesfully completes.
 */
static bool
seal_import(JSContext       *cx,
            JS::HandleObject obj,
            const char      *name)
{
    JS::Rooted<JSPropertyDescriptor> descr(cx);

    if (!JS_GetOwnPropertyDescriptor(cx, obj, name, &descr) ||
        descr.object() == NULL) {
        gjs_debug(GJS_DEBUG_IMPORTER,
                  "Failed to get attributes to seal '%s' in importer",
                  name);
        return false;
    }

    /* COMPAT: in mozjs45 use .setConfigurable(false) and the form of
     * JS_DefineProperty that takes the JSPropertyDescriptor directly */

    if (!JS_DefineProperty(cx, descr.object(), name, descr.value(),
                           descr.attributes() | JSPROP_PERMANENT,
                           descr.getter(), descr.setter())) {
        gjs_debug(GJS_DEBUG_IMPORTER,
                  "Failed to redefine attributes to seal '%s' in importer",
                  name);
        return false;
    }

    return true;
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
cancel_import(JSContext       *context,
              JS::HandleObject obj,
              const char      *name)
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

static bool
import_native_file(JSContext       *context,
                   JS::HandleObject obj,
                   const char      *name)
{
    JS::RootedObject module_obj(context);

    gjs_debug(GJS_DEBUG_IMPORTER, "Importing '%s'", name);

    if (!gjs_import_native_module(context, name, &module_obj))
        return false;

    if (!define_meta_properties(context, module_obj, NULL, name, obj))
        return false;

    if (JS_IsExceptionPending(context)) {
        /* I am not sure whether this can happen, but if it does we want to trap it.
         */
        gjs_debug(GJS_DEBUG_IMPORTER,
                  "Module '%s' reported an exception but gjs_import_native_module() returned true",
                  name);
        return false;
    }

    JS::RootedValue v_module(context, JS::ObjectValue(*module_obj));
    return JS_DefineProperty(context, obj, name, v_module,
                             GJS_MODULE_PROP_FLAGS);
}

static JSObject *
create_module_object(JSContext *context)
{
    return JS_NewObject(context, NULL, JS::NullPtr(), JS::NullPtr());
}

static bool
import_file(JSContext       *context,
            const char      *name,
            GFile           *file,
            JS::HandleObject module_obj)
{
    bool ret = false;
    char *script = NULL;
    char *full_path = NULL;
    gsize script_len = 0;
    GError *error = NULL;

    JS::CompileOptions options(context);
    JS::RootedValue ignored(context);

    if (!(g_file_load_contents(file, NULL, &script, &script_len, NULL, &error))) {
        if (!g_error_matches(error, G_IO_ERROR, G_IO_ERROR_IS_DIRECTORY) &&
            !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_NOT_DIRECTORY) &&
            !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
            gjs_throw_g_error(context, error);
        else
            g_error_free(error);

        goto out;
    }

    g_assert(script != NULL);

    full_path = g_file_get_parse_name (file);

    if (!gjs_eval_with_scope(context, module_obj, script, script_len,
                             full_path, &ignored))
        goto out;

    ret = true;

 out:
    g_free(script);
    g_free(full_path);
    return ret;
}

static JSObject *
load_module_init(JSContext       *context,
                 JS::HandleObject in_object,
                 const char      *full_path)
{
    bool found;
    GFile *file;

    /* First we check if js module has already been loaded  */
    JS::RootedId module_init_name(context,
        gjs_context_get_const_string(context, GJS_STRING_MODULE_INIT));
    if (JS_HasPropertyById(context, in_object, module_init_name, &found) && found) {
        JS::RootedValue module_obj_val(context);
        if (JS_GetPropertyById(context, in_object,
                               module_init_name, &module_obj_val)) {
            return &module_obj_val.toObject();
        }
    }

    JS::RootedObject module_obj(context, create_module_object(context));
    file = g_file_new_for_commandline_arg(full_path);
    if (!import_file (context, "__init__", file, module_obj))
        goto out;

    if (!JS_DefinePropertyById(context, in_object,
                               module_init_name, JS::ObjectValue(*module_obj),
                               NULL, NULL,
                               GJS_MODULE_PROP_FLAGS & ~JSPROP_PERMANENT))
        goto out;

 out:
    g_object_unref (file);
    return module_obj;
}

static void
load_module_elements(JSContext        *context,
                     JS::HandleObject  in_object,
                     ImporterIterator *iter,
                     const char       *init_path)
{
    JS::RootedObject jsiter(context),
        module_obj(context, load_module_init(context, in_object, init_path));

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

static bool
import_file_on_module(JSContext       *context,
                      JS::HandleObject obj,
                      const char      *name,
                      GFile           *file)
{
    bool retval = false;
    char *full_path = NULL;

    JS::RootedObject module_obj(context, create_module_object(context));

    if (!define_import(context, obj, module_obj, name))
        goto out;

    if (!import_file(context, name, file, module_obj))
        goto out;

    full_path = g_file_get_parse_name (file);
    if (!define_meta_properties(context, module_obj, full_path, name, obj))
        goto out;

    if (!seal_import(context, obj, name))
        goto out;

    retval = true;

 out:
    if (!retval)
        cancel_import(context, obj, name);

    g_free (full_path);
    return retval;
}

static bool
do_import(JSContext       *context,
          JS::HandleObject obj,
          Importer        *priv,
          const char      *name)
{
    char *filename;
    char *full_path;
    char *dirname = NULL;
    JS::RootedObject search_path(context);
    guint32 search_path_len;
    guint32 i;
    bool result;
    GPtrArray *directories;
    GFile *gfile;
    bool exists;

    JS::RootedId search_path_name(context,
        gjs_context_get_const_string(context, GJS_STRING_SEARCH_PATH));

    if (!gjs_object_require_property_value(context, obj, "importer",
                                           search_path_name, &search_path)) {
        return false;
    }

    if (!JS_IsArrayObject(context, search_path)) {
        gjs_throw(context, "searchPath property on importer is not an array");
        return false;
    }

    if (!JS_GetArrayLength(context, search_path, &search_path_len)) {
        gjs_throw(context, "searchPath array has no length");
        return false;
    }

    result = false;

    filename = g_strdup_printf("%s.js", name);
    full_path = NULL;
    directories = NULL;

    JS::RootedValue elem(context);
    JS::RootedObject module_obj(context);

    /* First try importing an internal module like byteArray */
    if (priv->is_root &&
        gjs_is_registered_native_module(context, obj, name) &&
        import_native_file(context, obj, name)) {
        gjs_debug(GJS_DEBUG_IMPORTER,
                  "successfully imported module '%s'", name);
        result = true;
        goto out;
    }

    for (i = 0; i < search_path_len; ++i) {
        elem.setUndefined();
        if (!JS_GetElement(context, search_path, i, &elem)) {
            /* this means there was an exception, while elem.isUndefined()
             * means no element found
             */
            goto out;
        }

        if (elem.isUndefined())
            continue;

        if (!elem.isString()) {
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

        module_obj.set(load_module_init(context, obj, full_path));
        if (module_obj != NULL) {
            JS::RootedValue obj_val(context);
            if (JS_GetProperty(context, module_obj, name, &obj_val)) {
                if (!obj_val.isUndefined() &&
                    JS_DefineProperty(context, obj, name, obj_val,
                                      GJS_MODULE_PROP_FLAGS & ~JSPROP_PERMANENT)) {
                    result = true;
                    goto out;
                }
            }
        }

        /* Second try importing a directory (a sub-importer) */
        if (full_path)
            g_free(full_path);
        full_path = g_build_filename(dirname, name,
                                     NULL);
        gfile = g_file_new_for_commandline_arg(full_path);

        if (g_file_query_file_type(gfile, (GFileQueryInfoFlags) 0, NULL) == G_FILE_TYPE_DIRECTORY) {
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

        g_object_unref(gfile);

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
        gfile = g_file_new_for_commandline_arg(full_path);
        exists = g_file_query_exists(gfile, NULL);

        if (!exists) {
            gjs_debug(GJS_DEBUG_IMPORTER,
                      "JS import '%s' not found in %s",
                      name, dirname);

            g_object_unref(gfile);
            continue;
        }

        if (import_file_on_module (context, obj, name, gfile)) {
            gjs_debug(GJS_DEBUG_IMPORTER,
                      "successfully imported module '%s'", name);
            result = true;
        }

        g_object_unref(gfile);

        /* Don't keep searching path if we fail to load the file for
         * reasons other than it doesn't exist... i.e. broken files
         * block searching for nonbroken ones
         */
        goto out;
    }

    if (directories != NULL) {
        /* NULL-terminate the char** */
        g_ptr_array_add(directories, NULL);

        if (import_directory(context, obj, name,
                             (const char**) directories->pdata)) {
            gjs_debug(GJS_DEBUG_IMPORTER,
                      "successfully imported directory '%s'", name);
            result = true;
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
        g_ptr_array_free(directories, false);
        g_strfreev(str_array);
    }

    g_free(full_path);
    g_free(filename);
    g_free(dirname);

    if (!result &&
        !JS_IsExceptionPending(context)) {
        /* If no exception occurred, the problem is just that we got to the
         * end of the path. Be sure an exception is set.
         */
        gjs_throw_custom(context, "Error", "ImportError",
                         "No JS module '%s' found in search path", name);
    }

    return result;
}

static ImporterIterator *
importer_iterator_new(void)
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
    g_ptr_array_free(iter->elements, true);
    g_slice_free(ImporterIterator, iter);
}

/*
 * Like JSEnumerateOp, but enum provides contextual information as follows:
 *
 * JSENUMERATE_INIT: allocate private enum struct in state_p, return number
 * of elements in *id_p
 * JSENUMERATE_NEXT: return next property id in *id_p, and if no new property
 * free state_p and set to JS::NullValue()
 * JSENUMERATE_DESTROY : destroy state_p
 *
 * Note that in a for ... in loop, this will be called first on the object,
 * then on its prototype.
 *
 */
static bool
importer_new_enumerate(JSContext  *context,
                       JS::HandleObject object,
                       JSIterateOp enum_op,
                       JS::MutableHandleValue statep,
                       JS::MutableHandleId idp)
{
    ImporterIterator *iter;

    switch (enum_op) {
    case JSENUMERATE_INIT_ALL:
    case JSENUMERATE_INIT: {
        Importer *priv;
        JS::RootedObject search_path(context);
        guint32 search_path_len;
        guint32 i;

        statep.setNull();

        idp.set(INT_TO_JSID(0));

        priv = priv_from_js(context, object);

        if (!priv)
            /* we are enumerating the prototype properties */
            return true;

        JS::RootedId search_path_name(context,
            gjs_context_get_const_string(context, GJS_STRING_SEARCH_PATH));
        if (!gjs_object_require_property_value(context, object, "importer",
                                               search_path_name, &search_path))
            return false;

        if (!JS_IsArrayObject(context, search_path)) {
            gjs_throw(context, "searchPath property on importer is not an array");
            return false;
        }

        if (!JS_GetArrayLength(context, search_path, &search_path_len)) {
            gjs_throw(context, "searchPath array has no length");
            return false;
        }

        iter = importer_iterator_new();

        JS::RootedValue elem(context);
        for (i = 0; i < search_path_len; ++i) {
            char *dirname = NULL;
            char *init_path;
            const char *filename;
            GDir *dir = NULL;

            elem = JS::UndefinedValue();
            if (!JS_GetElement(context, search_path, i, &elem)) {
                /* this means there was an exception, while elem.isUndefined()
                 * means no element found
                 */
                importer_iterator_free(iter);
                return false;
            }

            if (elem.isUndefined())
                continue;

            if (!elem.isString()) {
                gjs_throw(context, "importer searchPath contains non-string");
                importer_iterator_free(iter);
                return false;
            }

            if (!gjs_string_to_utf8(context, elem, &dirname)) {
                importer_iterator_free(iter);
                return false; /* Error message already set */
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
                    if (g_str_has_suffix(filename, "." G_MODULE_SUFFIX) ||
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

        statep.set(JS::PrivateValue(context));

        idp.set(INT_TO_JSID(iter->elements->len));

        break;
    }

    case JSENUMERATE_NEXT: {
        if (statep.isNull()) /* Iterating prototype */
            return true;

        iter = (ImporterIterator*) statep.get().toPrivate();

        if (iter->index < iter->elements->len) {
            JS::RootedValue element_val(context);
            if (!gjs_string_from_utf8(context,
                                         (const char*) g_ptr_array_index(iter->elements,
                                                           iter->index++),
                                         -1,
                                         &element_val))
                return false;

            if (!JS_ValueToId(context, element_val, idp))
                return false;

            break;
        }
        /* else fall through to destroying the iterator */
    }

    case JSENUMERATE_DESTROY: {
        if (!statep.isNull()) {
            iter = (ImporterIterator*) statep.get().toPrivate();

            importer_iterator_free(iter);

            statep.setNull();
        }
    }

    default:
        ;
    }

    return true;
}

/*
 * The *objp out parameter, on success, should be null to indicate that id
 * was not resolved; and non-null, referring to obj or one of its prototypes,
 * if id was resolved.
 */
static bool
importer_new_resolve(JSContext *context,
                     JS::HandleObject obj,
                     JS::HandleId id,
                     JS::MutableHandleObject objp)
{
    Importer *priv;
    char *name;
    bool ret = true;
    jsid module_init_name;

    module_init_name = gjs_context_get_const_string(context, GJS_STRING_MODULE_INIT);
    if (id == module_init_name)
        return true;

    if (!gjs_get_string_id(context, id, &name))
        return false;

    /* let Object.prototype resolve these */
    if (strcmp(name, "valueOf") == 0 ||
        strcmp(name, "toString") == 0 ||
        strcmp(name, "__iterator__") == 0)
        goto out;
    priv = priv_from_js(context, obj);

    gjs_debug_jsprop(GJS_DEBUG_IMPORTER,
                     "Resolve prop '%s' hook obj %p priv %p",
                     name, obj.get(), priv);
    if (priv == NULL) /* we are the prototype, or have the wrong class */
        goto out;
    JS_BeginRequest(context);
    if (do_import(context, obj, priv, name)) {
        objp.set(obj);
    } else {
        ret = false;
    }
    JS_EndRequest(context);

 out:
    g_free(name);
    return ret;
}

GJS_NATIVE_CONSTRUCTOR_DEFINE_ABSTRACT(importer)

static void
importer_finalize(JSFreeOp *fop,
                  JSObject *obj)
{
    Importer *priv;

    priv = (Importer*) JS_GetPrivate(obj);
    gjs_debug_lifecycle(GJS_DEBUG_IMPORTER,
                        "finalize, obj %p priv %p", obj, priv);
    if (priv == NULL)
        return; /* we are the prototype, not a real instance */

    GJS_DEC_COUNTER(importer);
    g_slice_free(Importer, priv);
}

/* The bizarre thing about this vtable is that it applies to both
 * instances of the object, and to the prototype that instances of the
 * class have.
 */
struct JSClass gjs_importer_class = {
    "GjsFileImporter",
    JSCLASS_HAS_PRIVATE |
    JSCLASS_NEW_RESOLVE |
    JSCLASS_NEW_ENUMERATE,
    JS_PropertyStub,
    JS_DeletePropertyStub,
    JS_PropertyStub,
    JS_StrictPropertyStub,
    (JSEnumerateOp) importer_new_enumerate, /* needs cast since it's the new enumerate signature */
    (JSResolveOp) importer_new_resolve, /* needs cast since it's the new resolve signature */
    JS_ConvertStub,
    importer_finalize
};

JSPropertySpec gjs_importer_proto_props[] = {
    JS_PS_END
};

JSFunctionSpec gjs_importer_proto_funcs[] = {
    JS_FS_END
};

static JSObject*
importer_new(JSContext *context,
             bool       is_root)
{
    Importer *priv;
    bool found;

    JS::RootedObject global(context, gjs_get_import_global(context));

    if (!JS_HasProperty(context, global, gjs_importer_class.name, &found))
        g_error("HasProperty call failed creating importer class");

    if (!found) {
        JSObject *prototype;
        prototype = JS_InitClass(context, global,
                                 /* parent prototype JSObject* for
                                  * prototype; NULL for
                                  * Object.prototype
                                  */
                                 JS::NullPtr(),
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
            g_error("Can't init class %s", gjs_importer_class.name);

        gjs_debug(GJS_DEBUG_IMPORTER, "Initialized class %s prototype %p",
                  gjs_importer_class.name, prototype);
    }

    JS::RootedObject importer(context,
        JS_NewObject(context, &gjs_importer_class, JS::NullPtr(), global));
    if (importer == NULL)
        g_error("No memory to create importer importer");

    priv = g_slice_new0(Importer);
    priv->is_root = is_root;

    GJS_INC_COUNTER(importer);

    g_assert(priv_from_js(context, importer) == NULL);
    JS_SetPrivate(importer, priv);

    gjs_debug_lifecycle(GJS_DEBUG_IMPORTER,
                        "importer constructor, obj %p priv %p", importer.get(),
                        priv);

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

        g_ptr_array_add(path, g_strdup("resource:///org/gnome/gjs/modules/"));

        /* $XDG_DATA_DIRS /gjs-1.0 */
        system_data_dirs = g_get_system_data_dirs();
        for (i = 0; system_data_dirs[i] != NULL; ++i) {
            char *s;
            s = g_build_filename(system_data_dirs[i], "gjs-1.0", NULL);
            g_ptr_array_add(path, s);
        }

        /* ${datadir}/share/gjs-1.0 */
        g_ptr_array_add(path, g_strdup(GJS_JS_DIR));

        g_ptr_array_add(path, NULL);

        search_path = (char**)g_ptr_array_free(path, false);

        gjs_search_path = search_path;
    } else {
        search_path = gjs_search_path;
    }

    return (G_CONST_RETURN char * G_CONST_RETURN *)search_path;
}

static JSObject*
gjs_create_importer(JSContext       *context,
                    const char      *importer_name,
                    const char     **initial_search_path,
                    bool             add_standard_search_path,
                    bool             is_root,
                    JS::HandleObject in_object)
{
    char **paths[2] = {0};
    char **search_path;

    paths[0] = (char**)initial_search_path;
    if (add_standard_search_path) {
        /* Stick the "standard" shared search path after the provided one. */
        paths[1] = (char**)gjs_get_search_path();
    }

    search_path = gjs_g_strv_concat(paths, 2);

    JS::RootedObject importer(context, importer_new(context, is_root));

    /* API users can replace this property from JS, is the idea */
    if (!gjs_define_string_array(context, importer,
                                 "searchPath", -1, (const char **)search_path,
                                 /* settable (no READONLY) but not deleteable (PERMANENT) */
                                 JSPROP_PERMANENT | JSPROP_ENUMERATE))
        g_error("no memory to define importer search path prop");

    g_strfreev(search_path);

    if (!define_meta_properties(context, importer, NULL, importer_name, in_object))
        g_error("failed to define meta properties on importer");

    return importer;
}

JSObject*
gjs_define_importer(JSContext       *context,
                    JS::HandleObject in_object,
                    const char      *importer_name,
                    const char     **initial_search_path,
                    bool             add_standard_search_path)

{
    JS::RootedObject importer(context,
        gjs_create_importer(context, importer_name, initial_search_path,
                            add_standard_search_path, false, in_object));

    if (!JS_DefineProperty(context, in_object, importer_name, importer,
                           GJS_MODULE_PROP_FLAGS))
        g_error("no memory to define importer property");

    gjs_debug(GJS_DEBUG_IMPORTER,
              "Defined importer '%s' %p in %p", importer_name, importer.get(),
              in_object.get());

    return importer;
}

/* If this were called twice for the same runtime with different args it
 * would basically be a bug, but checking for that is a lot of code so
 * we just ignore all calls after the first and hope the args are the same.
 */
bool
gjs_create_root_importer(JSContext   *context,
                         const char **initial_search_path,
                         bool         add_standard_search_path)
{
    JS::Value importer;

    JS_BeginRequest(context);

    importer = gjs_get_global_slot(context, GJS_GLOBAL_SLOT_IMPORTS);

    if (G_UNLIKELY (!importer.isUndefined())) {
        gjs_debug(GJS_DEBUG_IMPORTER,
                  "Someone else already created root importer, ignoring second request");

        JS_EndRequest(context);
        return true;
    }

    importer = JS::ObjectValue(*gjs_create_importer(context, "imports",
                                                    initial_search_path,
                                                    add_standard_search_path,
                                                    true, JS::NullPtr()));
    gjs_set_global_slot(context, GJS_GLOBAL_SLOT_IMPORTS, importer);

    JS_EndRequest(context);
    return true;
}

bool
gjs_define_root_importer_object(JSContext        *context,
                                JS::HandleObject  in_object,
                                JS::HandleObject  root_importer)
{
    bool success;
    jsid imports_name;

    success = false;
    JS_BeginRequest(context);

    JS::RootedValue importer (JS_GetRuntime(context),
                              JS::ObjectValue(*root_importer));
    imports_name = gjs_context_get_const_string(context, GJS_STRING_IMPORTS);
    if (!JS_DefinePropertyById(context, in_object,
                               imports_name, importer,
                               NULL, NULL,
                               GJS_MODULE_PROP_FLAGS)) {
        gjs_debug(GJS_DEBUG_IMPORTER, "DefineProperty imports on %p failed",
                  in_object.get());
        goto fail;
    }

    success = true;
 fail:
    JS_EndRequest(context);
    return success;
}

bool
gjs_define_root_importer(JSContext   *context,
                         JSObject    *in_object)
{
    JS::RootedValue importer(JS_GetRuntime(context),
                             gjs_get_global_slot(context, GJS_GLOBAL_SLOT_IMPORTS));
    JS::RootedObject rooted_in_object(JS_GetRuntime(context),
                                      in_object);
    JS::RootedObject rooted_importer(JS_GetRuntime(context),
                                     &importer.toObject());
    return gjs_define_root_importer_object(context,
                                           rooted_in_object,
                                           rooted_importer);
}
