/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008-2010 litl, LLC

#include <config.h>

#include <string.h>  // for size_t, strcmp, strlen

#ifdef _WIN32
#    include <windows.h>
#endif

#include <string>
#include <vector>   // for vector

#include <gio/gio.h>
#include <glib-object.h>
#include <glib.h>

#include <js/Array.h>
#include <js/CallArgs.h>
#include <js/CharacterEncoding.h>
#include <js/Class.h>
#include <js/ComparisonOperators.h>
#include <js/ErrorReport.h>  // for JS_ReportOutOfMemory, JSEXN_ERR
#include <js/GCVector.h>      // for StackGCVector
#include <js/GlobalObject.h>  // for CurrentGlobalOrNull
#include <js/Id.h>            // for PropertyKey
#include <js/Object.h>        // for GetClass
#include <js/PropertyAndElement.h>
#include <js/PropertyDescriptor.h>
#include <js/PropertySpec.h>
#include <js/RootingAPI.h>
#include <js/String.h>
#include <js/Symbol.h>
#include <js/TypeDecls.h>
#include <js/Utility.h>  // for UniqueChars
#include <js/Value.h>
#include <jsapi.h>  // for JS_NewPlainObject, IdVector, JS_...
#include <mozilla/Maybe.h>

#ifndef G_DISABLE_ASSERT
#    include <js/Exception.h>  // for JS_IsExceptionPending
#endif

#include "gjs/atoms.h"
#include "gjs/auto.h"
#include "gjs/context-private.h"
#include "gjs/gerror-result.h"
#include "gjs/global.h"
#include "gjs/importer.h"
#include "gjs/jsapi-util.h"
#include "gjs/macros.h"
#include "gjs/module.h"
#include "gjs/native.h"
#include "util/log.h"

#define MODULE_INIT_FILENAME "__init__.js"

extern const JSClass gjs_importer_class;

GJS_JSAPI_RETURN_CONVENTION
static JSObject* gjs_define_importer(JSContext*, JS::HandleObject, const char*,
                                     const std::vector<std::string>&, bool);

GJS_JSAPI_RETURN_CONVENTION
static bool
importer_to_string(JSContext *cx,
                   unsigned   argc,
                   JS::Value *vp)
{
    GJS_GET_THIS(cx, argc, vp, args, importer);

    Gjs::AutoChar output;

    const JSClass* klass = JS::GetClass(importer);
    const GjsAtoms& atoms = GjsContextPrivate::atoms(cx);
    JS::RootedValue module_path(cx);
    if (!JS_GetPropertyById(cx, importer, atoms.module_path(), &module_path))
        return false;

    if (module_path.isNull()) {
        output = g_strdup_printf("[%s root]", klass->name);
    } else {
        g_assert(module_path.isString() && "Bad importer.__modulePath__");
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

        Gjs::AutoChar module_path_buf;
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

        Gjs::AutoChar to_string_tag_buf{
            g_strdup_printf("GjsModule %s", module_path_buf.get())};
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

    JS::RootedId to_string_tag_name(
        context, JS::PropertyKey::Symbol(JS::GetWellKnownSymbol(
                     context, JS::SymbolCode::toStringTag)));
    return JS_DefinePropertyById(context, module_obj, to_string_tag_name,
                                 to_string_tag, attrs);
}

GJS_JSAPI_RETURN_CONVENTION
static bool import_directory(JSContext* context, JS::HandleObject obj,
                             const char* name,
                             const std::vector<std::string>& full_paths) {
    gjs_debug(GJS_DEBUG_IMPORTER,
              "Importing directory '%s'",
              name);

    // We define a sub-importer that has only the given directories on its
    // search path.
    return !!gjs_define_importer(context, obj, name, full_paths, false);
}

/* Make the property we set in gjs_module_import() permanent;
 * we do this after the import successfully completes.
 */
GJS_JSAPI_RETURN_CONVENTION
static bool
seal_import(JSContext       *cx,
            JS::HandleObject obj,
            JS::HandleId     id,
            const char      *name)
{
    JS::Rooted<mozilla::Maybe<JS::PropertyDescriptor>> maybe_descr(cx);

    if (!JS_GetOwnPropertyDescriptorById(cx, obj, id, &maybe_descr) ||
        maybe_descr.isNothing()) {
        gjs_debug(GJS_DEBUG_IMPORTER,
                  "Failed to get attributes to seal '%s' in importer",
                  name);
        return false;
    }

    JS::Rooted<JS::PropertyDescriptor> descr(cx, maybe_descr.value());

    descr.setConfigurable(false);

    if (!JS_DefinePropertyById(cx, obj, id, descr)) {
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
 * @id_str: Name under which the module was registered with add()
 *
 * Imports a builtin native-code module so that it is available to JS code as
 * `imports[id_str]`.
 *
 * Returns: true on success, false if an exception was thrown.
 */
bool gjs_import_native_module(JSContext* cx, JS::HandleObject importer,
                              const char* id_str) {
    gjs_debug(GJS_DEBUG_IMPORTER, "Importing '%s'", id_str);

    JS::RootedObject native_registry(
        cx, gjs_get_native_registry(JS::CurrentGlobalOrNull(cx)));

    JS::RootedId id(cx, gjs_intern_string_to_id(cx, id_str));
    if (id.isVoid())
        return false;

    JS::RootedObject module(cx);
    if (!gjs_global_registry_get(cx, native_registry, id, &module))
        return false;

    if (!module &&
        (!Gjs::NativeModuleDefineFuncs::get().define(cx, id_str, &module) ||
         !gjs_global_registry_set(cx, native_registry, id, module)))
        return false;

    return define_meta_properties(cx, module, nullptr, id_str, importer) &&
           JS_DefineProperty(cx, importer, id_str, module,
                             GJS_MODULE_PROP_FLAGS);
}

GJS_JSAPI_RETURN_CONVENTION
static bool
import_module_init(JSContext       *context,
                   GFile           *file,
                   JS::HandleObject module_obj)
{
    gsize script_len = 0;
    Gjs::AutoError error;

    GjsContextPrivate* gjs = GjsContextPrivate::from_cx(context);
    JS::RootedValue ignored(context);

    Gjs::AutoChar script;
    if (!g_file_load_contents(file, nullptr, script.out(), &script_len, nullptr,
                              &error)) {
        if (!g_error_matches(error, G_IO_ERROR, G_IO_ERROR_IS_DIRECTORY) &&
            !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_NOT_DIRECTORY) &&
            !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND)) {
            gjs_throw_gerror_message(context, error);
            return false;
        }

        return true;
    }
    g_assert(script);

    Gjs::AutoChar full_path{g_file_get_parse_name(file)};

    return gjs->eval_with_scope(module_obj, script, script_len, full_path,
                                &ignored);
}

GJS_JSAPI_RETURN_CONVENTION
static JSObject* load_module_init(JSContext* cx, JS::HandleObject in_object,
                                  GFile* file) {
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

        Gjs::AutoChar full_path{g_file_get_parse_name(file)};
        gjs_throw(cx, "Unexpected non-object module __init__ imported from %s",
                  full_path.get());
        return nullptr;
    }

    JS::RootedObject module_obj(cx, JS_NewPlainObject(cx));
    if (!module_obj)
        return nullptr;

    if (!import_module_init(cx, file, module_obj))
        return nullptr;

    if (!JS_DefinePropertyById(cx, in_object, atoms.module_init(), module_obj,
                               GJS_MODULE_PROP_FLAGS & ~JSPROP_PERMANENT))
        return nullptr;

    return module_obj;
}

GJS_JSAPI_RETURN_CONVENTION
static bool load_module_elements(JSContext* cx, JS::HandleObject in_object,
                                 JS::MutableHandleIdVector prop_ids,
                                 GFile* file) {
    JS::RootedObject module_obj(cx, load_module_init(cx, in_object, file));
    if (!module_obj)
        return false;

    JS::Rooted<JS::IdVector> ids(cx, cx);
    if (!JS_Enumerate(cx, module_obj, &ids))
        return false;

    if (!prop_ids.appendAll(ids)) {
        JS_ReportOutOfMemory(cx);
        return false;
    }

    return true;
}

/* If error, returns false. If not found, returns true but does not touch
 * the value at *result. If found, returns true and sets *result = true.
 */
GJS_JSAPI_RETURN_CONVENTION
static bool import_symbol_from_init_js(JSContext* cx, JS::HandleObject importer,
                                       GFile* directory, const char* name,
                                       bool* result) {
    bool found;
    Gjs::AutoUnref<GFile> file{
        g_file_get_child(directory, MODULE_INIT_FILENAME)};

    JS::RootedObject module_obj(cx, load_module_init(cx, importer, file));
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

    Gjs::AutoChar full_path{g_file_get_parse_name(file)};

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
static bool do_import(JSContext* context, JS::HandleObject obj,
                      JS::HandleId id) {
    JS::RootedObject search_path(context);
    guint32 search_path_len;
    guint32 i;
    bool exists, is_array;
    const GjsAtoms& atoms = GjsContextPrivate::atoms(context);

    if (!gjs_object_require_property(context, obj, "importer",
                                     atoms.search_path(), &search_path))
        return false;

    if (!JS::IsArrayObject(context, search_path, &is_array))
        return false;
    if (!is_array) {
        gjs_throw(context, "searchPath property on importer is not an array");
        return false;
    }

    if (!JS::GetArrayLength(context, search_path, &search_path_len)) {
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

    // null if this is the root importer
    JS::RootedValue parent(context);
    if (!JS_GetPropertyById(context, obj, atoms.parent_module(), &parent))
        return false;

    /* First try importing an internal module like gi */
    if (parent.isNull() &&
        Gjs::NativeModuleDefineFuncs::get().is_registered(name.get())) {
        if (!gjs_import_native_module(context, obj, name.get()))
            return false;

        gjs_debug(GJS_DEBUG_IMPORTER, "successfully imported module '%s'",
                  name.get());
        return true;
    }

    Gjs::AutoChar filename{g_strdup_printf("%s.js", name.get())};
    std::vector<std::string> directories;
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

        Gjs::AutoUnref<GFile> directory{
            g_file_new_for_commandline_arg(dirname.get())};

        /* Try importing __init__.js and loading the symbol from it */
        bool found = false;
        if (!import_symbol_from_init_js(context, obj, directory, name.get(),
                                        &found))
            return false;
        if (found)
            return true;

        /* Second try importing a directory (a sub-importer) */
        Gjs::AutoUnref<GFile> file{g_file_get_child(directory, name.get())};

        if (g_file_query_file_type(file, GFileQueryInfoFlags(0), nullptr) ==
            G_FILE_TYPE_DIRECTORY) {
            Gjs::AutoChar full_path{g_file_get_parse_name(file)};
            gjs_debug(GJS_DEBUG_IMPORTER,
                      "Adding directory '%s' to child importer '%s'",
                      full_path.get(), name.get());
            directories.push_back(full_path.get());
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
        file = g_file_get_child(directory, filename.get());
        exists = g_file_query_exists(file, nullptr);

        if (!exists) {
            Gjs::AutoChar full_path{g_file_get_parse_name(file)};
            gjs_debug(GJS_DEBUG_IMPORTER,
                      "JS import '%s' not found in %s at %s", name.get(),
                      dirname.get(), full_path.get());
            continue;
        }

        if (import_file_on_module(context, obj, id, name.get(), file)) {
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
        if (!import_directory(context, obj, name.get(), directories))
            return false;

        gjs_debug(GJS_DEBUG_IMPORTER, "successfully imported directory '%s'",
                  name.get());
        return true;
    }

    /* If no exception occurred, the problem is just that we got to the
     * end of the path. Be sure an exception is set. */
    g_assert(!JS_IsExceptionPending(context));
    gjs_throw_custom(context, JSEXN_ERR, "ImportError",
                     "No JS module '%s' found in search path", name.get());
    return false;
}

GJS_JSAPI_RETURN_CONVENTION
static bool importer_new_enumerate(JSContext* context, JS::HandleObject object,
                                   JS::MutableHandleIdVector properties,
                                   bool enumerable_only [[maybe_unused]]) {
    guint32 search_path_len;
    guint32 i;
    bool is_array;
    const GjsAtoms& atoms = GjsContextPrivate::atoms(context);

    JS::RootedObject search_path(context);
    if (!gjs_object_require_property(context, object, "importer",
                                     atoms.search_path(), &search_path))
        return false;

    if (!JS::IsArrayObject(context, search_path, &is_array))
        return false;
    if (!is_array) {
        gjs_throw(context, "searchPath property on importer is not an array");
        return false;
    }

    if (!JS::GetArrayLength(context, search_path, &search_path_len)) {
        gjs_throw(context, "searchPath array has no length");
        return false;
    }

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

        Gjs::AutoUnref<GFile> directory{
            g_file_new_for_commandline_arg(dirname.get())};
        Gjs::AutoUnref<GFile> file{
            g_file_get_child(directory, MODULE_INIT_FILENAME)};

        if (!load_module_elements(context, object, properties, file))
            return false;

        /* new_for_commandline_arg handles resource:/// paths */
        Gjs::AutoUnref<GFileEnumerator> direnum{g_file_enumerate_children(
            directory, "standard::name,standard::type", G_FILE_QUERY_INFO_NONE,
            nullptr, nullptr)};

        while (true) {
            GFileInfo *info;
            GFile *file;
            if (!direnum ||
                !g_file_enumerator_iterate(direnum, &info, &file, NULL, NULL))
                break;
            if (info == NULL || file == NULL)
                break;

            Gjs::AutoChar filename{g_file_get_basename(file)};

            /* skip hidden files and directories (.svn, .git, ...) */
            if (filename.get()[0] == '.')
                continue;

            /* skip module init file */
            if (strcmp(filename, MODULE_INIT_FILENAME) == 0)
                continue;

            if (g_file_info_get_file_type(info) == G_FILE_TYPE_DIRECTORY) {
                jsid id = gjs_intern_string_to_id(context, filename);
                if (id.isVoid())
                    return false;
                if (!properties.append(id)) {
                    JS_ReportOutOfMemory(context);
                    return false;
                }
            } else if (g_str_has_suffix(filename, ".js")) {
                Gjs::AutoChar filename_noext{
                    g_strndup(filename, strlen(filename) - 3)};
                jsid id = gjs_intern_string_to_id(context, filename_noext);
                if (id.isVoid())
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
    if (!id.isString()) {
        *resolved = false;
        return true;
    }

    const GjsAtoms& atoms = GjsContextPrivate::atoms(context);
    if (id == atoms.module_init() || id == atoms.to_string() ||
        id == atoms.value_of()) {
        *resolved = false;
        return true;
    }

    gjs_debug_jsprop(GJS_DEBUG_IMPORTER, "Resolve prop '%s' hook, obj %s",
                     gjs_debug_id(id).c_str(), gjs_debug_object(obj).c_str());

    if (!id.isString()) {
        *resolved = false;
        return true;
    }

    if (!do_import(context, obj, id))
        return false;

    *resolved = true;
    return true;
}

static const JSClassOps gjs_importer_class_ops = {
    nullptr,  // addProperty
    nullptr,  // deleteProperty
    nullptr,  // enumerate
    importer_new_enumerate,
    importer_resolve,
};

const JSClass gjs_importer_class = {
    "GjsFileImporter",
    0,
    &gjs_importer_class_ops,
};

static const JSPropertySpec gjs_importer_proto_props[] = {
    JS_STRING_SYM_PS(toStringTag, "GjsFileImporter", JSPROP_READONLY),
    JS_PS_END};

JSFunctionSpec gjs_importer_proto_funcs[] = {
    JS_FN("toString", importer_to_string, 0, 0),
    JS_FS_END};

[[nodiscard]] static const std::vector<std::string>& gjs_get_search_path() {
    static std::vector<std::string> gjs_search_path;
    static bool search_path_initialized = false;

    /* not thread safe */

    if (!search_path_initialized) {
        const char* const* system_data_dirs;
        const char *envstr;
        gsize i;

        /* in order of priority */

        /* $GJS_PATH */
        envstr = g_getenv("GJS_PATH");
        if (envstr) {
            char **dirs, **d;
            dirs = g_strsplit(envstr, G_SEARCHPATH_SEPARATOR_S, 0);
            for (d = dirs; *d != NULL; d++)
                gjs_search_path.push_back(*d);
            /* we assume the array and strings are allocated separately */
            g_free(dirs);
        }

        gjs_search_path.push_back("resource:///org/gnome/gjs/modules/script/");
        gjs_search_path.push_back("resource:///org/gnome/gjs/modules/core/");

        /* $XDG_DATA_DIRS /gjs-1.0 */
        system_data_dirs = g_get_system_data_dirs();
        for (i = 0; system_data_dirs[i] != NULL; ++i) {
            Gjs::AutoChar s{
                g_build_filename(system_data_dirs[i], "gjs-1.0", nullptr)};
            gjs_search_path.push_back(s.get());
        }

        /* ${datadir}/share/gjs-1.0 */
#ifdef G_OS_WIN32
        extern HMODULE gjs_dll;
        char *basedir = g_win32_get_package_installation_directory_of_module (gjs_dll);
        Gjs::AutoChar gjs_data_dir{
            g_build_filename(basedir, "share", "gjs-1.0", nullptr)};
        gjs_search_path.push_back(gjs_data_dir.get());
        g_free (basedir);
#else
        gjs_search_path.push_back(GJS_JS_DIR);
#endif

        search_path_initialized = true;
    }

    return gjs_search_path;
}

GJS_JSAPI_RETURN_CONVENTION
static bool no_construct(JSContext* cx, unsigned argc, JS::Value* vp) {
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
    gjs_throw_abstract_constructor_error(cx, args);
    return false;
}

GJS_JSAPI_RETURN_CONVENTION
static JSObject* gjs_importer_define_proto(JSContext* cx) {
    JS::RootedObject global(cx, JS::CurrentGlobalOrNull(cx));
    g_assert(global && "Must enter a realm before defining importer");

    // If we've been here more than once, we already have the proto
    JS::Value v_proto =
        gjs_get_global_slot(global, GjsGlobalSlot::PROTOTYPE_importer);
    if (!v_proto.isUndefined()) {
        g_assert(v_proto.isObject() &&
                 "Someone stored some weird value in a global slot");
        return &v_proto.toObject();
    }

    JS::RootedObject proto(cx, JS_NewPlainObject(cx));
    if (!proto || !JS_DefineFunctions(cx, proto, gjs_importer_proto_funcs) ||
        !JS_DefineProperties(cx, proto, gjs_importer_proto_props))
        return nullptr;
    gjs_set_global_slot(global, GjsGlobalSlot::PROTOTYPE_importer,
                        JS::ObjectValue(*proto));

    // For backwards compatibility
    JSFunction* constructor = JS_NewFunction(
        cx, no_construct, 0, JSFUN_CONSTRUCTOR, "GjsFileImporter");
    JS::RootedObject ctor_obj(cx, JS_GetFunctionObject(constructor));
    if (!JS_LinkConstructorAndPrototype(cx, ctor_obj, proto) ||
        !JS_DefineProperty(cx, global, "GjsFileImporter", ctor_obj, 0))
        return nullptr;

    gjs_debug(GJS_DEBUG_CONTEXT, "Initialized class %s prototype %p",
              gjs_importer_class.name, proto.get());
    return proto;
}

GJS_JSAPI_RETURN_CONVENTION
static JSObject* gjs_create_importer(
    JSContext* context, const char* importer_name,
    const std::vector<std::string>& initial_search_path,
    bool add_standard_search_path, JS::HandleObject in_object) {
    std::vector<std::string> search_paths = initial_search_path;
    if (add_standard_search_path) {
        /* Stick the "standard" shared search path after the provided one. */
        const std::vector<std::string>& gjs_search_path = gjs_get_search_path();
        search_paths.insert(search_paths.end(), gjs_search_path.begin(),
                            gjs_search_path.end());
    }

    JS::RootedObject proto(context, gjs_importer_define_proto(context));
    if (!proto)
        return nullptr;

    JS::RootedObject importer(
        context,
        JS_NewObjectWithGivenProto(context, &gjs_importer_class, proto));
    if (!importer)
        return nullptr;

    gjs_debug_lifecycle(GJS_DEBUG_IMPORTER, "importer constructor, obj %p",
                        importer.get());

    /* API users can replace this property from JS, is the idea */
    if (!gjs_define_string_array(
            context, importer, "searchPath", search_paths,
            // settable (no READONLY) but not deletable (PERMANENT)
            JSPROP_PERMANENT | JSPROP_RESOLVING))
        return nullptr;

    if (!define_meta_properties(context, importer, NULL, importer_name, in_object))
        return nullptr;

    return importer;
}

GJS_JSAPI_RETURN_CONVENTION
static JSObject* gjs_define_importer(
    JSContext* context, JS::HandleObject in_object, const char* importer_name,
    const std::vector<std::string>& initial_search_path,
    bool add_standard_search_path) {
    JS::RootedObject importer(
        context,
        gjs_create_importer(context, importer_name, initial_search_path,
                            add_standard_search_path, in_object));

    if (!JS_DefineProperty(context, in_object, importer_name, importer,
                           GJS_MODULE_PROP_FLAGS))
        return nullptr;

    gjs_debug(GJS_DEBUG_IMPORTER,
              "Defined importer '%s' %p in %p", importer_name, importer.get(),
              in_object.get());

    return importer;
}

JSObject* gjs_create_root_importer(
    JSContext* cx, const std::vector<std::string>& search_path) {
    return gjs_create_importer(cx, "imports", search_path, true, nullptr);
}
