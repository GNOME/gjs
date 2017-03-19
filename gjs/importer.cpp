/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
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
#include "jsapi-class.h"
#include "jsapi-wrapper.h"
#include "mem.h"
#include "module.h"
#include "native.h"

#include <gio/gio.h>

#ifdef G_OS_WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

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

extern const js::Class gjs_importer_real_class;

/* Bizarrely, the API for safely casting const js::Class * to const JSClass *
 * is called "js::Jsvalify" */
static const JSClass gjs_importer_class = *js::Jsvalify(&gjs_importer_real_class);

GJS_DEFINE_PRIV_FROM_JS(Importer, gjs_importer_class)

static JSObject *gjs_define_importer(JSContext *, JS::HandleObject,
    const char *, const char **, bool);

static bool
importer_to_string(JSContext *cx,
                   unsigned   argc,
                   JS::Value *vp)
{
    GJS_GET_THIS(cx, argc, vp, args, importer);
    const JSClass *klass = JS_GetClass(importer);

    JS::RootedValue module_path(cx);
    if (!gjs_object_get_property(cx, importer, GJS_STRING_MODULE_PATH,
                                 &module_path))
        return false;

    GjsAutoJSChar path(cx);
    GjsAutoChar output;

    if (module_path.isNull()) {
        output = g_strdup_printf("[%s root]", klass->name);
    } else {
        if (!gjs_string_to_utf8(cx, module_path, &path))
            return false;
        output = g_strdup_printf("[%s %s]", klass->name, path.get());
    }

    args.rval().setString(JS_NewStringCopyZ(cx, output));
    return true;
}

static bool
define_meta_properties(JSContext       *context,
                       JS::HandleObject module_obj,
                       const char      *full_path,
                       const char      *module_name,
                       JS::HandleObject parent)
{
    bool parent_is_module;

    /* For these meta-properties, don't set ENUMERATE since we wouldn't want to
     * copy these symbols to any other object for example. RESOLVING is used to
     * make sure we don't try to invoke a "resolve" operation, since this
     * function may be called from inside one. */
    unsigned attrs = JSPROP_READONLY | JSPROP_PERMANENT | JSPROP_RESOLVING;

    /* We define both __moduleName__ and __parentModule__ to null
     * on the root importer
     */
    parent_is_module = parent && JS_InstanceOf(context, parent,
                                               &gjs_importer_class, nullptr);

    gjs_debug(GJS_DEBUG_IMPORTER, "Defining parent %p of %p '%s' is mod %d",
              parent.get(), module_obj.get(),
              module_name ? module_name : "<root>", parent_is_module);

    if (full_path != NULL) {
        JS::RootedString file(context, JS_NewStringCopyZ(context, full_path));
        if (!JS_DefineProperty(context, module_obj, "__file__", file, attrs))
            return false;
    }

    /* Null is used instead of undefined for backwards compatibility with code
     * that explicitly checks for null. */
    JS::RootedValue module_name_val(context, JS::NullValue());
    JS::RootedValue parent_module_val(context, JS::NullValue());
    JS::RootedValue module_path(context, JS::NullValue());
    if (parent_is_module) {
        module_name_val.setString(JS_NewStringCopyZ(context, module_name));
        parent_module_val.setObject(*parent);

        JS::RootedValue parent_module_path(context);
        if (!gjs_object_get_property(context, parent,
                                     GJS_STRING_MODULE_PATH,
                                     &parent_module_path))
            return false;

        GjsAutoChar module_path_buf;
        if (parent_module_path.isNull()) {
            module_path_buf = g_strdup(module_name);
        } else {
            GjsAutoJSChar parent_path(context);
            if (!gjs_string_to_utf8(context, parent_module_path, &parent_path))
                return false;
            module_path_buf = g_strdup_printf("%s.%s", parent_path.get(), module_name);
        }
        module_path.setString(JS_NewStringCopyZ(context, module_path_buf));
    }

    if (!JS_DefineProperty(context, module_obj,
                           "__moduleName__", module_name_val, attrs))
        return false;

    if (!JS_DefineProperty(context, module_obj,
                           "__parentModule__", parent_module_val, attrs))
        return false;

    if (!JS_DefineProperty(context, module_obj, "__modulePath__", module_path,
                           attrs))
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

/* Make the property we set in gjs_module_import() permanent;
 * we do this after the import succesfully completes.
 */
static bool
seal_import(JSContext       *cx,
            JS::HandleObject obj,
            JS::HandleId     id,
            const char      *name)
{
    JS::Rooted<JSPropertyDescriptor> descr(cx);

    if (!JS_GetOwnPropertyDescriptorById(cx, obj, id, &descr) ||
        descr.object() == NULL) {
        gjs_debug(GJS_DEBUG_IMPORTER,
                  "Failed to get attributes to seal '%s' in importer",
                  name);
        return false;
    }

    descr.setConfigurable(false);
    if (!JS_DefinePropertyById(cx, descr.object(), id, descr)) {
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
module_to_string(JSContext *cx,
                 unsigned   argc,
                 JS::Value *vp)
{
    GJS_GET_THIS(cx, argc, vp, args, module);

    JS::RootedValue module_path(cx);
    if (!gjs_object_get_property(cx, module, GJS_STRING_MODULE_PATH,
                                 &module_path))
        return false;

    g_assert(!module_path.isNull());

    GjsAutoJSChar path(cx);
    if (!gjs_string_to_utf8(cx, module_path, &path))
        return false;
    GjsAutoChar output = g_strdup_printf("[GjsModule %s]", path.get());

    args.rval().setString(JS_NewStringCopyZ(cx, output));
    return true;
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

    if (!JS_DefineFunction(context, module_obj, "toString", module_to_string,
                           0, 0))
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

static bool
import_module_init(JSContext       *context,
                   GFile           *file,
                   JS::HandleObject module_obj)
{
    bool ret = false;
    char *script = NULL;
    char *full_path = NULL;
    gsize script_len = 0;
    GError *error = NULL;

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

    /* First we check if js module has already been loaded  */
    if (gjs_object_has_property(context, in_object, GJS_STRING_MODULE_INIT,
                                &found) && found) {
        JS::RootedValue module_obj_val(context);
        if (gjs_object_get_property(context, in_object,
                                    GJS_STRING_MODULE_INIT,
                                    &module_obj_val)) {
            return &module_obj_val.toObject();
        }
    }

    JS::RootedObject module_obj(context, JS_NewPlainObject(context));
    GjsAutoUnref<GFile> file = g_file_new_for_commandline_arg(full_path);
    if (!import_module_init(context, file, module_obj))
        return module_obj;

    gjs_object_define_property(context, in_object,
                               GJS_STRING_MODULE_INIT, module_obj,
                               GJS_MODULE_PROP_FLAGS & ~JSPROP_PERMANENT);

    return module_obj;
}

static void
load_module_elements(JSContext        *cx,
                     JS::HandleObject  in_object,
                     JS::AutoIdVector& prop_ids,
                     const char       *init_path)
{
    size_t ix, length;
    JS::RootedObject module_obj(cx, load_module_init(cx, in_object, init_path));

    if (module_obj == NULL)
        return;

    JS::Rooted<JS::IdVector> ids(cx, cx);
    if (!JS_Enumerate(cx, module_obj, &ids))
        return;

    for (ix = 0, length = ids.length(); ix < length; ix++)
        prop_ids.append(ids[ix]);
}

/* If error, returns false. If not found, returns true but does not touch
 * the value at *result. If found, returns true and sets *result = true.
 */
static bool
import_symbol_from_init_js(JSContext       *cx,
                           JS::HandleObject importer,
                           const char      *dirname,
                           const char      *name,
                           bool            *result)
{
    bool found;
    GjsAutoChar full_path = g_build_filename(dirname, MODULE_INIT_FILENAME,
                                             NULL);

    JS::RootedObject module_obj(cx, load_module_init(cx, importer, full_path));
    if (!module_obj || !JS_AlreadyHasOwnProperty(cx, module_obj, name, &found))
        return false;

    if (!found)
        return true;

    JS::RootedValue obj_val(cx);
    if (!JS_GetProperty(cx, module_obj, name, &obj_val))
        return false;

    if (obj_val.isUndefined())
        return true;

    if (!JS_DefineProperty(cx, importer, name, obj_val,
                           GJS_MODULE_PROP_FLAGS & ~JSPROP_PERMANENT))
        return false;

    *result = true;
    return true;
}

static bool
import_file_on_module(JSContext       *context,
                      JS::HandleObject obj,
                      JS::HandleId     id,
                      const char      *name,
                      GFile           *file)
{
    bool retval = false;
    char *full_path = NULL;

    JS::RootedObject module_obj(context,
        gjs_module_import(context, obj, id, name, file));
    if (!module_obj)
        goto out;

    full_path = g_file_get_parse_name (file);
    if (!define_meta_properties(context, module_obj, full_path, name, obj))
        goto out;

    if (!JS_DefineFunction(context, module_obj, "toString", module_to_string,
                           0, 0))
        goto out;

    if (!seal_import(context, obj, id, name))
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
          JS::HandleId     id,
          const char      *name)
{
    char *filename;
    char *full_path;
    JS::RootedObject search_path(context);
    guint32 search_path_len;
    guint32 i;
    bool result, exists, is_array;
    GPtrArray *directories;
    GFile *gfile;

    if (!gjs_object_require_property(context, obj, "importer",
                                     GJS_STRING_SEARCH_PATH, &search_path))
        return false;

    if (!JS_IsArrayObject(context, search_path, &is_array))
        return false;
    if (!is_array) {
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
        GjsAutoJSChar dirname(context);

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

        if (!gjs_string_to_utf8(context, elem, &dirname))
            goto out; /* Error message already set */

        /* Ignore empty path elements */
        if (dirname[0] == '\0')
            continue;

        /* Try importing __init__.js and loading the symbol from it */
        import_symbol_from_init_js(context, obj, dirname, name, &result);
        if (result)
            goto out;

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
                      name, dirname.get());

            g_object_unref(gfile);
            continue;
        }

        if (import_file_on_module(context, obj, id, name, gfile)) {
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

/* Note that in a for ... in loop, this will be called first on the object,
 * then on its prototype.
 */
static bool
importer_enumerate(JSContext        *context,
                   JS::HandleObject  object,
                   JS::AutoIdVector& properties,
                   bool              enumerable_only)
{
    Importer *priv;
    guint32 search_path_len;
    guint32 i;
    bool is_array;

    priv = priv_from_js(context, object);

    if (!priv)
        /* we are enumerating the prototype properties */
        return true;

    JS::RootedObject search_path(context);
    if (!gjs_object_require_property(context, object, "importer",
                                     GJS_STRING_SEARCH_PATH,
                                     &search_path))
        return false;

    if (!JS_IsArrayObject(context, search_path, &is_array))
        return false;
    if (!is_array) {
        gjs_throw(context, "searchPath property on importer is not an array");
        return false;
    }

    if (!JS_GetArrayLength(context, search_path, &search_path_len)) {
        gjs_throw(context, "searchPath array has no length");
        return false;
    }

    JS::RootedValue elem(context);
    for (i = 0; i < search_path_len; ++i) {
        GjsAutoJSChar dirname(context);
        char *init_path;

        elem.setUndefined();
        if (!JS_GetElement(context, search_path, i, &elem)) {
            /* this means there was an exception, while elem.isUndefined()
             * means no element found
             */
            return false;
        }

        if (elem.isUndefined())
            continue;

        if (!elem.isString()) {
            gjs_throw(context, "importer searchPath contains non-string");
            return false;
        }

        if (!gjs_string_to_utf8(context, elem, &dirname))
            return false; /* Error message already set */

        init_path = g_build_filename(dirname, MODULE_INIT_FILENAME, NULL);

        load_module_elements(context, object, properties, init_path);

        g_free(init_path);

        /* new_for_commandline_arg handles resource:/// paths */
        GjsAutoUnref<GFile> dir = g_file_new_for_commandline_arg(dirname);
        GjsAutoUnref<GFileEnumerator> direnum =
            g_file_enumerate_children(dir, G_FILE_ATTRIBUTE_STANDARD_TYPE,
                                      G_FILE_QUERY_INFO_NONE, NULL, NULL);

        while (true) {
            GFileInfo *info;
            GFile *file;
            if (!g_file_enumerator_iterate(direnum, &info, &file, NULL, NULL))
                break;
            if (info == NULL || file == NULL)
                break;

            GjsAutoChar filename = g_file_get_basename(file);

            /* skip hidden files and directories (.svn, .git, ...) */
            if (filename[0] == '.')
                continue;

            /* skip module init file */
            if (strcmp(filename, MODULE_INIT_FILENAME) == 0)
                continue;

            if (g_file_info_get_file_type(info) == G_FILE_TYPE_DIRECTORY) {
                properties.append(gjs_intern_string_to_id(context, filename));
            } else if (g_str_has_suffix(filename, "." G_MODULE_SUFFIX) ||
                       g_str_has_suffix(filename, ".js")) {
                GjsAutoChar filename_noext =
                    g_strndup(filename, strlen(filename) - 3);
                properties.append(gjs_intern_string_to_id(context, filename_noext));
            }
        }
    }
    return true;
}

/*
 * The *objp out parameter, on success, should be null to indicate that id
 * was not resolved; and non-null, referring to obj or one of its prototypes,
 * if id was resolved.
 */
static bool
importer_resolve(JSContext        *context,
                 JS::HandleObject  obj,
                 JS::HandleId      id,
                 bool             *resolved)
{
    Importer *priv;
    jsid module_init_name;
    GjsAutoJSChar name(context);

    module_init_name = gjs_context_get_const_string(context, GJS_STRING_MODULE_INIT);
    if (id == module_init_name) {
        *resolved = false;
        return true;
    }

    if (!gjs_get_string_id(context, id, &name))
        return false;

    /* let Object.prototype resolve these */
    if (strcmp(name, "valueOf") == 0 ||
        strcmp(name, "toString") == 0 ||
        strcmp(name, "__iterator__") == 0) {
        *resolved = false;
        return true;
    }
    priv = priv_from_js(context, obj);

    gjs_debug_jsprop(GJS_DEBUG_IMPORTER,
                     "Resolve prop '%s' hook obj %p priv %p",
                     name.get(), obj.get(), priv);
    if (priv == NULL) {
        /* we are the prototype, or have the wrong class */
        *resolved = false;
        return true;
    }

    JSAutoRequest ar(context);
    if (!do_import(context, obj, priv, id, name))
        return false;

    *resolved = true;
    return true;
}

GJS_NATIVE_CONSTRUCTOR_DEFINE_ABSTRACT(importer)

static void
importer_finalize(js::FreeOp *fop,
                  JSObject   *obj)
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
const js::Class gjs_importer_real_class = {
    "GjsFileImporter",
    JSCLASS_HAS_PRIVATE,
    NULL,  /* addProperty */
    NULL,  /* deleteProperty */
    NULL,  /* getProperty */
    NULL,  /* setProperty */
    NULL,  /* enumerate (see below) */
    importer_resolve,
    NULL,  /* convert */
    importer_finalize,
    NULL,  /* call */
    NULL,  /* hasInstance */
    NULL,  /* construct */
    NULL,  /* trace */
    JS_NULL_CLASS_SPEC,
    JS_NULL_CLASS_EXT,
    {
        NULL,  /* lookupProperty */
        NULL,  /* defineProperty */
        NULL,  /* hasProperty */
        NULL,  /* getProperty */
        NULL,  /* setProperty */
        NULL,  /* getOwnPropertyDescriptor */
        NULL,  /* deleteProperty */
        NULL,  /* watch */
        NULL,  /* unwatch */
        NULL,  /* getElements */
        importer_enumerate,
        NULL,  /* thisObject */
    }
};

static JSPropertySpec *gjs_importer_proto_props = nullptr;
static JSFunctionSpec *gjs_importer_static_funcs = nullptr;

JSFunctionSpec gjs_importer_proto_funcs[] = {
    JS_FS("toString", importer_to_string, 0, 0),
    JS_FS_END
};

GJS_DEFINE_PROTO_FUNCS(importer)

static JSObject*
importer_new(JSContext *context,
             bool       is_root)
{
    Importer *priv;

    JS::RootedObject proto(context);
    if (!gjs_importer_define_proto(context, nullptr, &proto))
        g_error("Error creating importer prototype");

    JS::RootedObject importer(context,
        JS_NewObjectWithGivenProto(context, &gjs_importer_class, proto));
    if (importer == NULL)
        g_error("No memory to create importer");

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
#ifdef G_OS_WIN32
        extern HMODULE gjs_dll;
        char *basedir = g_win32_get_package_installation_directory_of_module (gjs_dll);
        char *gjs_data_dir = g_build_filename (basedir, "share", "gjs-1.0", NULL);
        g_ptr_array_add(path, g_strdup(gjs_data_dir));
        g_free (gjs_data_dir);
        g_free (basedir);
#else
        g_ptr_array_add(path, g_strdup(GJS_JS_DIR));
#endif

        g_ptr_array_add(path, NULL);

        search_path = (char**)g_ptr_array_free(path, false);

        gjs_search_path = search_path;
    } else {
        search_path = gjs_search_path;
    }

    return (G_CONST_RETURN char * G_CONST_RETURN *)search_path;
}

static JSObject*
gjs_create_importer(JSContext          *context,
                    const char         *importer_name,
                    const char * const *initial_search_path,
                    bool                add_standard_search_path,
                    bool                is_root,
                    JS::HandleObject    in_object)
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
                                 JSPROP_PERMANENT | JSPROP_RESOLVING))
        g_error("no memory to define importer search path prop");

    g_strfreev(search_path);

    if (!define_meta_properties(context, importer, NULL, importer_name, in_object))
        g_error("failed to define meta properties on importer");

    return importer;
}

static JSObject *
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

JSObject *
gjs_create_root_importer(JSContext          *cx,
                         const char * const *search_path)
{
    return gjs_create_importer(cx, "imports", search_path, true, true, nullptr);
}
