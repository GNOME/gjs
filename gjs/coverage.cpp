/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * Copyright © 2014 Endless Mobile, Inc.
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
 *
 * Authored By: Sam Spilsbury <sam@endlessm.com>
 */

#include <sys/stat.h>
#include <gio/gio.h>

#include <gjs/context.h>

#include "coverage.h"
#include "global.h"
#include "importer.h"
#include "jsapi-util-args.h"
#include "util/error.h"

struct _GjsCoverage {
    GObject parent;
};

typedef struct {
    gchar **prefixes;
    GjsContext *context;
    JS::Heap<JSObject *> compartment;

    GFile *output_dir;
} GjsCoveragePrivate;

#if __GNUC__ >= 8
_Pragma("GCC diagnostic push")
_Pragma("GCC diagnostic ignored \"-Wcast-function-type\"")
#endif
G_DEFINE_TYPE_WITH_PRIVATE(GjsCoverage,
                           gjs_coverage,
                           G_TYPE_OBJECT)
#if __GNUC__ >= 8
_Pragma("GCC diagnostic pop")
#endif

enum {
    PROP_0,
    PROP_PREFIXES,
    PROP_CONTEXT,
    PROP_CACHE,
    PROP_OUTPUT_DIRECTORY,
    PROP_N
};

static GParamSpec *properties[PROP_N] = { NULL, };

static char *
get_file_identifier(GFile *source_file) {
    char *path = g_file_get_path(source_file);
    if (!path)
        path = g_file_get_uri(source_file);
    return path;
}

static bool
write_source_file_header(GOutputStream *stream,
                         GFile         *source_file,
                         GError       **error)
{
    GjsAutoChar path = get_file_identifier(source_file);
    return g_output_stream_printf(stream, NULL, NULL, error, "SF:%s\n", path.get());
}

static bool
copy_source_file_to_coverage_output(GFile   *source_file,
                                    GFile   *destination_file,
                                    GError **error)
{
    /* We need to recursively make the directory we
     * want to copy to, as g_file_copy doesn't do that */
    GjsAutoUnref<GFile> destination_dir = g_file_get_parent(destination_file);
    if (!g_file_make_directory_with_parents(destination_dir, NULL, error)) {
        if (!g_error_matches(*error, G_IO_ERROR, G_IO_ERROR_EXISTS))
            return false;
        g_clear_error(error);
    }

    return g_file_copy(source_file, destination_file, G_FILE_COPY_OVERWRITE,
                       nullptr, nullptr, nullptr, error);
}

/* This function will strip a URI scheme and return
 * the string with the URI scheme stripped or NULL
 * if the path was not a valid URI
 */
static char *
strip_uri_scheme(const char *potential_uri)
{
    char *uri_header = g_uri_parse_scheme(potential_uri);

    if (uri_header) {
        gsize offset = strlen(uri_header);
        g_free(uri_header);

        /* g_uri_parse_scheme only parses the name
         * of the scheme, we also need to strip the
         * characters ':///' */
        return g_strdup(potential_uri + offset + 4);
    }

    return NULL;
}

/* This function will return a string of pathname
 * components from the first directory indicating
 * where two directories diverge. For instance:
 *
 * child: /a/b/c/d/e
 * parent: /a/b/d/
 *
 * Will return: c/d/e
 *
 * If the directories are not at all similar then
 * the full dirname of the child_path effectively
 * be returned.
 *
 * As a special case, child paths that are a URI
 * automatically return the full URI path with
 * the URI scheme and leading slash stripped out.
 */
static char *
find_diverging_child_components(GFile *child,
                                GFile *parent)
{
    g_object_ref(parent);
    GFile *ancestor = parent;
    while (ancestor != NULL) {
        char *relpath = g_file_get_relative_path(ancestor, child);
        if (relpath) {
            g_object_unref(ancestor);
            return relpath;
        }
        GFile *new_ancestor = g_file_get_parent(ancestor);
        g_object_unref(ancestor);
        ancestor = new_ancestor;
    }

    /* This is a special case of getting the URI below. The difference is that
     * this gives you a regular path name; getting it through the URI would
     * give a URI-encoded path (%20 for spaces, etc.) */
    GFile *root = g_file_new_for_path("/");
    char *child_path = g_file_get_relative_path(root, child);
    g_object_unref(root);
    if (child_path)
        return child_path;

    char *child_uri = g_file_get_uri(child);
    char *stripped_uri = strip_uri_scheme(child_uri);
    g_free(child_uri);
    return stripped_uri;
}

static bool
filename_has_coverage_prefixes(GjsCoverage *self, const char *filename)
{
    auto priv = static_cast<GjsCoveragePrivate *>(gjs_coverage_get_instance_private(self));

    for (const char * const *prefix = priv->prefixes; *prefix; prefix++) {
        if (g_str_has_prefix(filename, *prefix))
            return true;
    }
    return false;
}

static inline bool
write_line(GOutputStream *out,
           const char    *line,
           GError       **error)
{
    return g_output_stream_printf(out, nullptr, nullptr, error, "%s\n", line);
}

static GjsAutoUnref<GFile>
write_statistics_internal(GjsCoverage *coverage,
                          JSContext   *cx,
                          GError     **error)
{
    GjsCoveragePrivate *priv = (GjsCoveragePrivate *) gjs_coverage_get_instance_private(coverage);

    /* Create output directory if it doesn't exist */
    if (!g_file_make_directory_with_parents(priv->output_dir, nullptr, error)) {
        if (!g_error_matches(*error, G_IO_ERROR, G_IO_ERROR_EXISTS))
            return nullptr;
        g_clear_error(error);
    }

    GFile *output_file = g_file_get_child(priv->output_dir, "coverage.lcov");

    size_t lcov_length;
    GjsAutoPointer<char, void, free> lcov(
        js::GetCodeCoverageSummary(cx, &lcov_length));

    GjsAutoUnref<GOutputStream> ostream =
        G_OUTPUT_STREAM(g_file_append_to(output_file,
                                         G_FILE_CREATE_NONE,
                                         NULL,
                                         error));
    if (!ostream)
        return nullptr;

    GjsAutoStrv lcov_lines = g_strsplit(lcov, "\n", -1);
    const char* test_name = NULL;
    bool ignoring_file = false;

    for (const char * const *iter = lcov_lines.get(); *iter; iter++) {
        if (ignoring_file) {
            if (strcmp(*iter, "end_of_record") == 0)
                ignoring_file = false;
            continue;
        }

        if (g_str_has_prefix(*iter, "TN:")) {
            /* Don't write the test name if the next line shows we are
             * ignoring the source file */
            test_name = *iter;
            continue;
        } else if (g_str_has_prefix(*iter, "SF:")) {
            const char *filename = *iter + 3;
            if (!filename_has_coverage_prefixes(coverage, filename)) {
                ignoring_file = true;
                continue;
            }

            /* Now we can write the test name before writing the source file */
            if (!write_line(ostream, test_name, error))
                return nullptr;

            /* The source file could be a resource, so we must use
             * g_file_new_for_commandline_arg() to disambiguate between URIs and
             * filesystem paths. */
            GjsAutoUnref<GFile> source_file = g_file_new_for_commandline_arg(filename);
            GjsAutoChar diverged_paths =
                find_diverging_child_components(source_file, priv->output_dir);
            GjsAutoUnref<GFile> destination_file =
                g_file_resolve_relative_path(priv->output_dir, diverged_paths);
            if (!copy_source_file_to_coverage_output(source_file, destination_file, error))
                return nullptr;

            /* Rewrite the source file path to be relative to the output
             * dir so that genhtml will find it */
            if (!write_source_file_header(ostream, destination_file, error))
                return nullptr;
            continue;
        }

        if (!write_line(ostream, *iter, error))
            return nullptr;
    }

    return output_file;
}

/**
 * gjs_coverage_write_statistics:
 * @coverage: A #GjsCoverage
 * @output_directory: A directory to write coverage information to.
 *
 * Scripts which were provided as part of the #GjsCoverage:prefixes
 * construction property will be written out to @output_directory, in the same
 * directory structure relative to the source dir where the tests were run.
 *
 * This function takes all available statistics and writes them out to either
 * the file provided or to files of the pattern (filename).info in the same
 * directory as the scanned files. It will provide coverage data for all files
 * ending with ".js" in the coverage directories.
 */
void
gjs_coverage_write_statistics(GjsCoverage *coverage)
{
    auto priv = static_cast<GjsCoveragePrivate *>(gjs_coverage_get_instance_private(coverage));
    GError *error = nullptr;

    auto cx = static_cast<JSContext *>(gjs_context_get_native_context(priv->context));
    JSAutoCompartment ac(cx, gjs_get_import_global(cx));
    JSAutoRequest ar(cx);

    GjsAutoUnref<GFile> output_file = write_statistics_internal(coverage, cx, &error);
    if (!output_file) {
        g_critical("Error writing coverage data: %s", error->message);
        g_error_free(error);
        return;
    }

    GjsAutoChar output_file_path = g_file_get_path(output_file);
    g_message("Wrote coverage statistics to %s", output_file_path.get());
}

static void
gjs_coverage_init(GjsCoverage *self)
{
}

static void
coverage_tracer(JSTracer *trc, void *data)
{
    GjsCoverage *coverage = (GjsCoverage *) data;
    GjsCoveragePrivate *priv = (GjsCoveragePrivate *) gjs_coverage_get_instance_private(coverage);

    JS::TraceEdge<JSObject *>(trc, &priv->compartment, "Coverage compartment");
}

static bool
bootstrap_coverage(GjsCoverage *coverage)
{
    GjsCoveragePrivate *priv = (GjsCoveragePrivate *) gjs_coverage_get_instance_private(coverage);

    JSContext *context = (JSContext *) gjs_context_get_native_context(priv->context);
    JSAutoRequest ar(context);

    JSObject *debuggee = gjs_get_import_global(context);
    JS::RootedObject debugger_compartment(context,
                                          gjs_create_global_object(context));
    {
        JSAutoCompartment compartment(context, debugger_compartment);
        JS::RootedObject debuggeeWrapper(context, debuggee);
        if (!JS_WrapObject(context, &debuggeeWrapper))
            return false;

        JS::RootedValue debuggeeWrapperValue(context, JS::ObjectValue(*debuggeeWrapper));
        if (!JS_SetProperty(context, debugger_compartment, "debuggee",
                            debuggeeWrapperValue) ||
            !gjs_define_global_properties(context, debugger_compartment,
                                          "coverage"))
            return false;

        /* Add a tracer, as suggested by jdm on #jsapi */
        JS_AddExtraGCRootsTracer(context, coverage_tracer, coverage);

        priv->compartment = debugger_compartment;
    }

    return true;
}

static void
gjs_coverage_constructed(GObject *object)
{
    G_OBJECT_CLASS(gjs_coverage_parent_class)->constructed(object);

    GjsCoverage *coverage = GJS_COVERAGE(object);
    GjsCoveragePrivate *priv = (GjsCoveragePrivate *) gjs_coverage_get_instance_private(coverage);
    new (&priv->compartment) JS::Heap<JSObject *>();

    if (!bootstrap_coverage(coverage)) {
        JSContext *context = static_cast<JSContext *>(gjs_context_get_native_context(priv->context));
        JSAutoCompartment compartment(context, gjs_get_import_global(context));
        gjs_log_exception(context);
    }
}

static void
gjs_coverage_set_property(GObject      *object,
                          unsigned int  prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
    GjsCoverage *coverage = GJS_COVERAGE(object);
    GjsCoveragePrivate *priv = (GjsCoveragePrivate *) gjs_coverage_get_instance_private(coverage);
    switch (prop_id) {
    case PROP_PREFIXES:
        g_assert(priv->prefixes == NULL);
        priv->prefixes = (char **) g_value_dup_boxed (value);
        break;
    case PROP_CONTEXT:
        priv->context = GJS_CONTEXT(g_value_dup_object(value));
        break;
    case PROP_CACHE:
        break;
    case PROP_OUTPUT_DIRECTORY:
        priv->output_dir = G_FILE(g_value_dup_object(value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gjs_coverage_dispose(GObject *object)
{
    GjsCoverage *coverage = GJS_COVERAGE(object);
    GjsCoveragePrivate *priv = (GjsCoveragePrivate *) gjs_coverage_get_instance_private(coverage);

    /* Decomission objects inside of the JSContext before
     * disposing of the context */
    auto cx = static_cast<JSContext *>(gjs_context_get_native_context(priv->context));
    JS_RemoveExtraGCRootsTracer(cx, coverage_tracer, coverage);
    priv->compartment = nullptr;

    g_clear_object(&priv->context);

    G_OBJECT_CLASS(gjs_coverage_parent_class)->dispose(object);
}

static void
gjs_coverage_finalize (GObject *object)
{
    GjsCoverage *coverage = GJS_COVERAGE(object);
    GjsCoveragePrivate *priv = (GjsCoveragePrivate *) gjs_coverage_get_instance_private(coverage);

    g_strfreev(priv->prefixes);
    g_clear_object(&priv->output_dir);
    priv->compartment.~Heap();

    G_OBJECT_CLASS(gjs_coverage_parent_class)->finalize(object);
}

static void
gjs_coverage_class_init (GjsCoverageClass *klass)
{
    GObjectClass *object_class = (GObjectClass *) klass;

    object_class->constructed = gjs_coverage_constructed;
    object_class->dispose = gjs_coverage_dispose;
    object_class->finalize = gjs_coverage_finalize;
    object_class->set_property = gjs_coverage_set_property;

    properties[PROP_PREFIXES] = g_param_spec_boxed("prefixes",
                                                   "Prefixes",
                                                   "Prefixes of files on which to perform coverage analysis",
                                                   G_TYPE_STRV,
                                                   (GParamFlags) (G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE));
    properties[PROP_CONTEXT] = g_param_spec_object("context",
                                                   "Context",
                                                   "A context to gather coverage stats for",
                                                   GJS_TYPE_CONTEXT,
                                                   (GParamFlags) (G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE));
    properties[PROP_CACHE] = g_param_spec_object("cache",
                                                 "Deprecated property",
                                                 "Has no effect",
                                                 G_TYPE_FILE,
                                                 (GParamFlags) (G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE | G_PARAM_DEPRECATED));
    properties[PROP_OUTPUT_DIRECTORY] =
        g_param_spec_object("output-directory", "Output directory",
                            "Directory handle at which to output coverage statistics",
                            G_TYPE_FILE,
                            (GParamFlags) (G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_properties(object_class,
                                      PROP_N,
                                      properties);
}

/**
 * gjs_coverage_new:
 * @prefixes: A null-terminated strv of prefixes of files on which to record
 * code coverage
 * @context: A #GjsContext object
 * @output_dir: A #GFile handle to a directory in which to write coverage
 * information
 *
 * Creates a new #GjsCoverage object that collects coverage information for
 * any scripts run in @context.
 *
 * Scripts which were provided as part of @prefixes will be written out to
 * @output_dir, in the same directory structure relative to the source dir where
 * the tests were run.
 *
 * Returns: A #GjsCoverage object
 */
GjsCoverage *
gjs_coverage_new (const char * const *prefixes,
                  GjsContext         *context,
                  GFile              *output_dir)
{
    GjsCoverage *coverage =
        GJS_COVERAGE(g_object_new(GJS_TYPE_COVERAGE,
                                  "prefixes", prefixes,
                                  "context", context,
                                  "output-directory", output_dir,
                                  NULL));

    return coverage;
}
