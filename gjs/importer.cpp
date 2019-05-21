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

#include <string.h>  // for size_t, strcmp, strlen

#ifdef _WIN32
#    define WIN32_LEAN_AND_MEAN
#    include <windows.h>
#endif

#include <memory>   // for allocator_traits<>::value_type
#include <utility>  // for move
#include <vector>   // for vector

#include <gio/gio.h>
#include <glib-object.h>
#include <glib.h>

#include "gjs/jsapi-wrapper.h"
#include "mozilla/UniquePtr.h"

#include "gjs/atoms.h"
#include "gjs/context-private.h"
#include "gjs/importer.h"
#include "gjs/jsapi-class.h"
#include "gjs/jsapi-util.h"
#include "gjs/mem-private.h"
#include "gjs/module.h"
#include "gjs/native.h"
#include "util/log.h"
#include "util/misc.h"

#define MODULE_INIT_FILENAME "__init__.js"

static char **gjs_search_path = NULL;

typedef struct {
    bool is_root;
} Importer;

typedef struct {
    GPtrArray *elements;
    unsigned int index;
} ImporterIterator;

extern const JSClass gjs_importer_class;

GJS_DEFINE_PRIV_FROM_JS(Importer, gjs_importer_class)

GJS_JSAPI_RETURN_CONVENTION
static JSObject *gjs_define_importer(JSContext *, JS::HandleObject,
    const char *, const char * const *, bool);

GJS_JSAPI_RETURN_CONVENTION
static bool
importer_to_string(JSContext *cx,
                   unsigned   argc,
                   JS::Value *vp)
{
    GJS_GET_THIS(cx, argc, vp, args, importer);
    const JSClass *klass = JS_GetClass(importer);

    const GjsAtoms& atoms = GjsContextPrivate::atoms(cx);
    JS::RootedValue module_path(cx);
    if (!JS_GetPropertyById(cx, importer, atoms.module_path(), &module_path))
        return false;

    GjsAutoChar output;

    if (module_path.isNull()) {
        output = g_strdup_printf("[%s root]", klass->name);
    } else {
        JS::UniqueChars path = gjs_string_to_utf8(cx, module_path);
        if (!path)
            return false;
        output = g_strdup_printf("[%s %s]", klass->name, path.get());
    }

    args.rval().setString(JS_NewStringCopyZ(cx, output));
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool
define_meta_properties(JSContext       *context,
                       JS::HandleObject module_obj,
                       const char      *parse_name,
                       const char      *module_name,
                       JS::HandleObject parent)
{
    bool parent_is_module;
    const GjsAtoms& atoms = GjsContextPrivate::atoms(context);

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

    if (parse_name != nullptr) {
        JS::RootedValue file(context);
        if (!gjs_string_from_utf8(context, parse_name, &file))
            return false;
        if (!JS_DefinePropertyById(context, module_obj, atoms.file(), file,
                                   attrs))
            return false;
    }

    /* Null is used instead of undefined for backwards compatibility with code
     * that explicitly checks for null. */
    JS::RootedValue module_name_val(context, JS::NullValue());
    JS::RootedValue parent_module_val(context, JS::NullValue());
    JS::RootedValue module_path(context, JS::NullValue());
    JS::RootedValue to_string_tag(context);
    if (parent_is_module) {
        if (!gjs_string_from_utf8(context, module_name, &module_name_val))
            return false;
        parent_module_val.setObject(*parent);

        JS::RootedValue parent_module_path(context);
        if (!JS_GetPropertyById(context, parent, atoms.module_path(),
                                &parent_module_path))
            return false;

        GjsAutoChar module_path_buf;
        if (parent_module_path.isNull()) {
            module_path_buf = g_strdup(module_name);
        } else {
            JS::UniqueChars parent_path =
                gjs_string_to_utf8(context, parent_module_path);
            if (!parent_path)
                return false;
            module_path_buf = g_strdup_printf("%s.%s", parent_path.get(), module_name);
        }
        if (!gjs_string_from_utf8(context, module_path_buf, &module_path))
            return false;

        GjsAutoChar to_string_tag_buf = g_strdup_printf("GjsModule %s",
                                                        module_path_buf.get());
        if (!gjs_string_from_utf8(context, to_string_tag_buf, &to_string_tag))
            return false;
    } else {
        to_string_tag.setString(JS_AtomizeString(context, "GjsModule"));
    }

    if (!JS_DefinePropertyById(context, module_obj, atoms.module_name(),
                               module_name_val, attrs))
        return false;

    if (!JS_DefinePropertyById(context, module_obj, atoms.parent_module(),
                               parent_module_val, attrs))
        return false;

    if (!JS_DefinePropertyById(context, module_obj, atoms.module_path(),
                               module_path, attrs))
        return false;

    JS::RootedId to_string_tag_name(context,
        SYMBOL_TO_JSID(JS::GetWellKnownSymbol(context,
                                              JS::SymbolCode::toStringTag)));
    return JS_DefinePropertyById(context, module_obj, to_string_tag_name,
                                 to_string_tag, attrs);
}

GJS_JSAPI_RETURN_CONVENTION
static bool
import_directory(JSContext          *context,
                 JS::HandleObject    obj,
                 const char         *name,
                 const char * const *full_paths)
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
    return importer != nullptr;
}

/* Make the property we set in gjs_module_import() permanent;
 * we do this after the import succesfully completes.
 */
GJS_JSAPI_RETURN_CONVENTION
static bool
seal_import(JSContext       *cx,
            JS::HandleObject obj,
            JS::HandleId     id,
            const char      *name)
{
    JS::Rooted<JS::PropertyDescriptor> descr(cx);

    if (!JS_GetOwnPropertyDescriptorById(cx, obj, id, &descr) || !descr.object()) {
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

/*
 * gjs_import_native_module:
 * @cx: the #JSContext
 * @importer: the root importer
 * @parse_name: Name under which the module was registered with
 *  gjs_register_native_module(), should be in the format as returned by
 *  g_file_get_parse_name()
 *
 * Imports a builtin native-code module so that it is available to JS code as
 * `imports[parse_name]`.
 *
 * Returns: true on success, false if an exception was thrown.
 */
bool
gjs_import_native_module(JSContext       *cx,
                         JS::HandleObject importer,
                         const char      *parse_name)
{
    gjs_debug(GJS_DEBUG_IMPORTER, "Importing '%s'", parse_name);

    JS::RootedObject module(cx);
    return gjs_load_native_module(cx, parse_name, &module) &&
           define_meta_properties(cx, module, nullptr, parse_name, importer) &&
           JS_DefineProperty(cx, importer, parse_name, module, GJS_MODULE_PROP_FLAGS);
}

GJS_JSAPI_RETURN_CONVENTION
static bool
import_module_init(JSContext       *context,
                   GFile           *file,
                   JS::HandleObject module_obj)
{
    gsize script_len = 0;
    GError *error = NULL;

    GjsContextPrivate* gjs = GjsContextPrivate::from_cx(context);
    JS::RootedValue ignored(context);

    char* script_unowned;
    if (!(g_file_load_contents(file, nullptr, &script_unowned, &script_len,
                               nullptr, &error))) {
        if (!g_error_matches(error, G_IO_ERROR, G_IO_ERROR_IS_DIRECTORY) &&
            !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_NOT_DIRECTORY) &&
            !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
            gjs_throw_gerror_message(context, error);
        else
            g_error_free(error);

        return false;
    }

    GjsAutoChar script = script_unowned;
    g_assert(script != NULL);

    GjsAutoChar full_path = g_file_get_parse_name(file);

    return gjs->eval_with_scope(module_obj, script, script_len, full_path,
                                &ignored);
}

GJS_JSAPI_RETURN_CONVENTION
static JSObject* load_module_init(JSContext* cx, JS::HandleObject in_object,
                                  const char* full_path) {
    bool found;
    const GjsAtoms& atoms = GjsContextPrivate::atoms(cx);

    /* First we check if js module has already been loaded  */
    if (!JS_HasPropertyById(cx, in_object, atoms.module_init(), &found))
        return nullptr;
    if (found) {
        JS::RootedValue v_module(cx);
        if (!JS_GetPropertyById(cx, in_object, atoms.module_init(),
                                &v_module))
            return nullptr;
        if (v_module.isObject())
            return &v_module.toObject();

        gjs_throw(cx, "Unexpected non-object module __init__ imported from %s",
                  full_path);
        return nullptr;
    }

    JS::RootedObject module_obj(cx, JS_NewPlainObject(cx));
    if (!module_obj)
        return nullptr;

    GjsAutoUnref<GFile> file = g_file_new_for_commandline_arg(full_path);
    if (!import_module_init(cx, file, module_obj)) {
        JS_ClearPendingException(cx);
        return module_obj;
    }

    if (!JS_DefinePropertyById(cx, in_object, atoms.module_init(), module_obj,
                               GJS_MODULE_PROP_FLAGS & ~JSPROP_PERMANENT))
        return nullptr;

    return module_obj;
}

GJS_JSAPI_RETURN_CONVENTION
static bool load_module_elements(JSContext* cx, JS::HandleObject in_object,
                                 JS::MutableHandleIdVector prop_ids,
                                 const char* init_path) {
    size_t ix, length;
    JS::RootedObject module_obj(cx, load_module_init(cx, in_object, init_path));
    if (!module_obj)
        return false;

    JS::Rooted<JS::IdVector> ids(cx, cx);
    if (!JS_Enumerate(cx, module_obj, &ids))
        return false;

    for (ix = 0, length = ids.length(); ix < length; ix++)
        if (!prop_ids.append(ids[ix])) {
            JS_ReportOutOfMemory(cx);
            return false;
        }

    return true;
}

/* If error, returns false. If not found, returns true but does not touch
 * the value at *result. If found, returns true and sets *result = true.
 */
GJS_JSAPI_RETURN_CONVENTION
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

GJS_JSAPI_RETURN_CONVENTION
static bool attempt_import(JSContext* cx, JS::HandleObject obj,
                           JS::HandleId module_id, const char* module_name,
                           GFile* file) {
    JS::RootedObject module_obj(
        cx, gjs_module_import(cx, obj, module_id, module_name, file));
    if (!module_obj)
        return false;

    GjsAutoChar full_path = g_file_get_parse_name(file);

    return define_meta_properties(cx, module_obj, full_path, module_name,
                                  obj) &&
           seal_import(cx, obj, module_id, module_name);
}

GJS_JSAPI_RETURN_CONVENTION
static bool
import_file_on_module(JSContext       *context,
                      JS::HandleObject obj,
                      JS::HandleId     id,
                      const char      *name,
                      GFile           *file)
{
    if (!attempt_import(context, obj, id, name, file)) {
        cancel_import(context, obj, name);
        return false;
    }

    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool do_import(JSContext* context, JS::HandleObject obj, Importer* priv,
                      JS::HandleId id) {
    JS::RootedObject search_path(context);
    guint32 search_path_len;
    guint32 i;
    bool exists, is_array;
    const GjsAtoms& atoms = GjsContextPrivate::atoms(context);

    if (!gjs_object_require_property(context, obj, "importer",
                                     atoms.search_path(), &search_path))
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

    JS::UniqueChars name;
    if (!gjs_get_string_id(context, id, &name))
        return false;
    if (!name) {
        gjs_throw(context, "Importing invalid module name");
        return false;
    }

    /* First try importing an internal module like gi */
    if (priv->is_root && gjs_is_registered_native_module(name.get())) {
        if (!gjs_import_native_module(context, obj, name.get()))
            return false;

        gjs_debug(GJS_DEBUG_IMPORTER, "successfully imported module '%s'",
                  name.get());
        return true;
    }

    GjsAutoChar filename = g_strdup_printf("%s.js", name.get());
    std::vector<GjsAutoChar> directories;
    JS::RootedValue elem(context);
    JS::RootedString str(context);

    for (i = 0; i < search_path_len; ++i) {
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

        str = elem.toString();
        JS::UniqueChars dirname(JS_EncodeStringToUTF8(context, str));
        if (!dirname)
            return false;

        /* Ignore empty path elements */
        if (dirname[0] == '\0')
            continue;

        /* Try importing __init__.js and loading the symbol from it */
        bool found = false;
        if (!import_symbol_from_init_js(context, obj, dirname.get(), name.get(),
                                        &found))
            return false;
        if (found)
            return true;

        /* Second try importing a directory (a sub-importer) */
        GjsAutoChar full_path =
            g_build_filename(dirname.get(), name.get(), nullptr);
        GjsAutoUnref<GFile> gfile = g_file_new_for_commandline_arg(full_path);

        if (g_file_query_file_type(gfile, (GFileQueryInfoFlags) 0, NULL) == G_FILE_TYPE_DIRECTORY) {
            gjs_debug(GJS_DEBUG_IMPORTER,
                      "Adding directory '%s' to child importer '%s'",
                      full_path.get(), name.get());
            directories.push_back(std::move(full_path));
        }

        /* If we just added to directories, we know we don't need to
         * check for a file.  If we added to directories on an earlier
         * iteration, we want to ignore any files later in the
         * path. So, always skip the rest of the loop block if we have
         * directories.
         */
        if (!directories.empty())
            continue;

        /* Third, if it's not a directory, try importing a file */
        full_path = g_build_filename(dirname.get(), filename.get(), nullptr);
        gfile = g_file_new_for_commandline_arg(full_path);
        exists = g_file_query_exists(gfile, NULL);

        if (!exists) {
            gjs_debug(GJS_DEBUG_IMPORTER, "JS import '%s' not found in %s",
                      name.get(), dirname.get());
            continue;
        }

        if (import_file_on_module(context, obj, id, name.get(), gfile)) {
            gjs_debug(GJS_DEBUG_IMPORTER, "successfully imported module '%s'",
                      name.get());
            return true;
        }

        /* Don't keep searching path if we fail to load the file for
         * reasons other than it doesn't exist... i.e. broken files
         * block searching for nonbroken ones
         */
        return false;
    }

    if (!directories.empty()) {
        /* NULL-terminate the char** */
        const char **full_paths = g_new0(const char *, directories.size() + 1);
        for (size_t ix = 0; ix < directories.size(); ix++)
            full_paths[ix] = directories[ix].get();

        bool result = import_directory(context, obj, name.get(), full_paths);
        g_free(full_paths);
        if (!result)
            return false;

        gjs_debug(GJS_DEBUG_IMPORTER, "successfully imported directory '%s'",
                  name.get());
        return true;
    }

    /* If no exception occurred, the problem is just that we got to the
     * end of the path. Be sure an exception is set. */
    g_assert(!JS_IsExceptionPending(context));
    gjs_throw_custom(context, JSProto_Error, "ImportError",
                     "No JS module '%s' found in search path", name.get());
    return false;
}

/* Note that in a for ... in loop, this will be called first on the object,
 * then on its prototype.
 */
GJS_JSAPI_RETURN_CONVENTION
static bool importer_new_enumerate(JSContext* context, JS::HandleObject object,
                                   JS::MutableHandleIdVector properties,
                                   bool enumerable_only G_GNUC_UNUSED) {
    Importer *priv;
    guint32 search_path_len;
    guint32 i;
    bool is_array;
    const GjsAtoms& atoms = GjsContextPrivate::atoms(context);

    priv = priv_from_js(context, object);

    if (!priv)
        /* we are enumerating the prototype properties */
        return true;

    JS::RootedObject search_path(context);
    if (!gjs_object_require_property(context, object, "importer",
                                     atoms.search_path(), &search_path))
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
    JS::RootedString str(context);
    for (i = 0; i < search_path_len; ++i) {
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

        str = elem.toString();
        JS::UniqueChars dirname(JS_EncodeStringToUTF8(context, str));
        if (!dirname)
            return false;

        init_path =
            g_build_filename(dirname.get(), MODULE_INIT_FILENAME, nullptr);

        if (!load_module_elements(context, object, properties, init_path))
            return false;

        g_free(init_path);

        /* new_for_commandline_arg handles resource:/// paths */
        GjsAutoUnref<GFile> dir = g_file_new_for_commandline_arg(dirname.get());
        GjsAutoUnref<GFileEnumerator> direnum =
            g_file_enumerate_children(dir, "standard::name,standard::type",
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
            if (filename.get()[0] == '.')
                continue;

            /* skip module init file */
            if (strcmp(filename, MODULE_INIT_FILENAME) == 0)
                continue;

            if (g_file_info_get_file_type(info) == G_FILE_TYPE_DIRECTORY) {
                jsid id = gjs_intern_string_to_id(context, filename);
                if (id == JSID_VOID)
                    return false;
                if (!properties.append(id)) {
                    JS_ReportOutOfMemory(context);
                    return false;
                }
            } else if (g_str_has_suffix(filename, "." G_MODULE_SUFFIX) ||
                       g_str_has_suffix(filename, ".js")) {
                GjsAutoChar filename_noext =
                    g_strndup(filename, strlen(filename) - 3);
                jsid id = gjs_intern_string_to_id(context, filename_noext);
                if (id == JSID_VOID)
                    return false;
                if (!properties.append(id)) {
                    JS_ReportOutOfMemory(context);
                    return false;
                }
            }
        }
    }
    return true;
}

/* The *resolved out parameter, on success, should be false to indicate that id
 * was not resolved; and true if id was resolved. */
GJS_JSAPI_RETURN_CONVENTION
static bool
importer_resolve(JSContext        *context,
                 JS::HandleObject  obj,
                 JS::HandleId      id,
                 bool             *resolved)
{
    Importer *priv;

    if (!JSID_IS_STRING(id)) {
        *resolved = false;
        return true;
    }

    const GjsAtoms& atoms = GjsContextPrivate::atoms(context);
    if (id == atoms.module_init() || id == atoms.to_string() ||
        id == atoms.value_of()) {
        *resolved = false;
        return true;
    }

    priv = priv_from_js(context, obj);

    gjs_debug_jsprop(GJS_DEBUG_IMPORTER,
                     "Resolve prop '%s' hook, obj %s, priv %p",
                     gjs_debug_id(id).c_str(), gjs_debug_object(obj).c_str(), priv);
    if (!priv) {
        /* we are the prototype, or have the wrong class */
        *resolved = false;
        return true;
    }

    if (!JSID_IS_STRING(id)) {
        *resolved = false;
        return true;
    }

    if (!do_import(context, obj, priv, id))
        return false;

    *resolved = true;
    return true;
}

GJS_NATIVE_CONSTRUCTOR_DEFINE_ABSTRACT(importer)

static void importer_finalize(JSFreeOp*, JSObject* obj) {
    Importer *priv;

    priv = (Importer*) JS_GetPrivate(obj);
    gjs_debug_lifecycle(GJS_DEBUG_IMPORTER,
                        "finalize, obj %p priv %p", obj, priv);
    if (!priv)
        return; /* we are the prototype, not a real instance */

    GJS_DEC_COUNTER(importer);
    g_slice_free(Importer, priv);
}

/* The bizarre thing about this vtable is that it applies to both
 * instances of the object, and to the prototype that instances of the
 * class have.
 */
static const JSClassOps gjs_importer_class_ops = {
    nullptr,  // addProperty
    nullptr,  // deleteProperty
    nullptr,  // enumerate
    importer_new_enumerate,
    importer_resolve,
    nullptr,  // mayResolve
    importer_finalize
};

const JSClass gjs_importer_class = {
    "GjsFileImporter",
    JSCLASS_HAS_PRIVATE | JSCLASS_FOREGROUND_FINALIZE,
    &gjs_importer_class_ops,
};

static JSPropertySpec *gjs_importer_proto_props = nullptr;
static JSFunctionSpec *gjs_importer_static_funcs = nullptr;

JSFunctionSpec gjs_importer_proto_funcs[] = {
    JS_FN("toString", importer_to_string, 0, 0),
    JS_FS_END};

GJS_DEFINE_PROTO_FUNCS(importer)

GJS_JSAPI_RETURN_CONVENTION
static JSObject*
importer_new(JSContext *context,
             bool       is_root)
{
    Importer *priv;

    JS::RootedObject proto(context);
    if (!gjs_importer_define_proto(context, nullptr, &proto))
        return nullptr;

    JS::RootedObject importer(context,
        JS_NewObjectWithGivenProto(context, &gjs_importer_class, proto));
    if (!importer)
        return nullptr;

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

GJS_USE
static const char* const* gjs_get_search_path(void) {
    char **search_path;

    /* not thread safe */

    if (!gjs_search_path) {
        const char* const* system_data_dirs;
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

    return const_cast<const char* const*>(search_path);
}

GJS_JSAPI_RETURN_CONVENTION
static JSObject*
gjs_create_importer(JSContext          *context,
                    const char         *importer_name,
                    const char * const *initial_search_path,
                    bool                add_standard_search_path,
                    bool                is_root,
                    JS::HandleObject    in_object)
{
    char **paths[2] = {0};

    paths[0] = (char**)initial_search_path;
    if (add_standard_search_path) {
        /* Stick the "standard" shared search path after the provided one. */
        paths[1] = (char**)gjs_get_search_path();
    }

    GjsAutoStrv search_path = gjs_g_strv_concat(paths, 2);

    JS::RootedObject importer(context, importer_new(context, is_root));

    /* API users can replace this property from JS, is the idea */
    if (!gjs_define_string_array(context, importer,
                                 "searchPath", -1, search_path.as<const char *>(),
                                 /* settable (no READONLY) but not deleteable (PERMANENT) */
                                 JSPROP_PERMANENT | JSPROP_RESOLVING))
        return nullptr;

    if (!define_meta_properties(context, importer, NULL, importer_name, in_object))
        return nullptr;

    return importer;
}

GJS_JSAPI_RETURN_CONVENTION
static JSObject *
gjs_define_importer(JSContext          *context,
                    JS::HandleObject    in_object,
                    const char         *importer_name,
                    const char * const *initial_search_path,
                    bool                add_standard_search_path)

{
    JS::RootedObject importer(context,
        gjs_create_importer(context, importer_name, initial_search_path,
                            add_standard_search_path, false, in_object));

    if (!JS_DefineProperty(context, in_object, importer_name, importer,
                           GJS_MODULE_PROP_FLAGS))
        return nullptr;

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
