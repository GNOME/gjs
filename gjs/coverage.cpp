/*
 * Copyright © 2013 Endless Mobile, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Authored By: Sam Spilsbury <sam@endlessm.com>
 */
#include <stdio.h>
#include <string.h>
#include <gio/gio.h>
#include <gjs/gjs.h>
#include <gjs/interrupt-register.h>
#include <gjs/executable-linesutil.h>
#include <gjs/coverage.h>

struct _GjsDebugCoveragePrivate
{
    GHashTable           *file_statistics;
    GjsInterruptRegister *interrupt_register;
    GjsContext           *context;
    gchar                **covered_paths;
    GjsDebugConnection   *new_scripts_connection;
    GjsDebugConnection   *single_step_connection;
};

G_DEFINE_TYPE_WITH_PRIVATE(GjsDebugCoverage,
                           gjs_debug_coverage,
                           G_TYPE_OBJECT)

static void
gjs_debug_coverage_single_step_interrupt_hook(GjsInterruptRegister *reg,
                                              GjsContext           *context,
                                              GjsInterruptInfo     *info,
                                              gpointer             user_data)
{
    const gchar *filename = info->filename;
    GArray *statistics = (GArray *) g_hash_table_lookup((GHashTable *) user_data,
                                                        filename);
    /* This shouldn't really happen, but if it does just return early */
    if (!statistics)
      return;

    guint line_no = info->line;

    g_assert(line_no <= statistics->len);

    /* If this happens it is not a huge problem - we only try to
     * filter out lines which we think are not executable so
     * that they don't cause execess noise in coverage reports */
    gint *statistics_line_count = &(g_array_index(statistics, gint, line_no));

    if (*statistics_line_count == -1)
        *statistics_line_count = 0;

    ++(*statistics_line_count);
}

typedef void(*LineForeachFunc)(const gchar *str,
                               gpointer    user_data);

static void
for_each_line_in_string(const gchar *data,
                        gpointer    user_data,
                        LineForeachFunc func)
{
    const gchar *str = data;

    while (str)
    {
        (*func)(str + 1, user_data);
        str = (gchar *) (strstr (str + 1, "\n"));
    }
}

static void
increment_line_counter(const gchar *str,
                       gpointer    user_data)
{
    guint *line_count = (guint *) user_data;
    ++(*line_count);
}

static guint
count_lines_in_string(const gchar *data)
{
    guint lineCount = 0;

    for_each_line_in_string(data, &lineCount, increment_line_counter);

    return lineCount;
}

static GArray *
create_statistics_for_filename(const gchar *filename)
{
    gchar *lines = NULL;
    gsize length = 0;

    if (!g_file_get_contents(filename,
                             &lines,
                             &length,
                             NULL))
    return NULL;

    guint lineCount = count_lines_in_string(lines);

    GArray *statistics = g_array_new(TRUE, FALSE, sizeof(gint));
    g_array_set_size(statistics, lineCount);
    memset(statistics->data, -1, sizeof(gint) * statistics->len);

    g_free(lines);

    return statistics;
}

static void
mark_executable_lines(GArray *statistics,
                      guint  *executable_lines,
                      guint  n_executable_lines)
{
    guint i = 0;
    for (; i < n_executable_lines; ++i)
        g_array_index(statistics, gint, executable_lines[i]) = 0;
}

static void
gjs_debug_coverage_new_script_available_hook(GjsInterruptRegister *reg,
                                             GjsContext           *context,
                                             GjsDebugScriptInfo   *info,
                                             gpointer             user_data)
{
    GHashTable *file_statistics = (GHashTable *) user_data;
    if (g_hash_table_contains(file_statistics,
                              info->filename))
    {
        GArray *statistics = (GArray *) g_hash_table_lookup(file_statistics,
                                                            info->filename);

        /* No current value exists, open the file and create statistics for
         * it now that we have the number of executable lines for this file */
        if (!statistics)
        {
            statistics = create_statistics_for_filename(info->filename);
            g_hash_table_insert(file_statistics,
                                g_strdup(info->filename),
                                statistics);
        }

        /* This might be a new part of an existing script, so mark any
         * executable lines that were otherwise unmarked */
        mark_executable_lines(statistics,
                              info->executable_lines,
                              info->n_executable_lines);
    }
}

static void
write_to_stream(GOutputStream *ostream,
                const gchar   *msg)
{
    g_output_stream_write(ostream, msg, strlen(msg), NULL, NULL);
}

static GFile *
delete_file_and_open_anew(GFile *file)
{
    g_file_delete(file, NULL, NULL);
    g_file_create(file, G_FILE_CREATE_REPLACE_DESTINATION, NULL, NULL);
    return file;
}

static GFile *
delete_file_at_path_and_open_anew(const gchar *path)
{
    GFile *file = g_file_new_for_path(path);
    return delete_file_and_open_anew(file);
}

static GFile *
create_tracefile_for_script_name(const gchar *script_name)
{
    gsize tracefile_name_buffer_size = strlen((const gchar *) script_name) + 8;
    gchar tracefile_name_buffer[tracefile_name_buffer_size];
    snprintf(tracefile_name_buffer,
             tracefile_name_buffer_size,
             "%s.info",
             (const gchar *) script_name);

    return delete_file_at_path_and_open_anew(tracefile_name_buffer);
}

static GFile *
open_tracefile(GFile       *specified_tracefile,
               const gchar *script_name)
{
    if (specified_tracefile)
        return (GFile *) g_object_ref(specified_tracefile);

    return create_tracefile_for_script_name(script_name);
}

static GFileIOStream *
get_io_stream_at_end_position_for_tracefile(GFile *file)
{
    GError *error = NULL;
    GFileIOStream *iostream = g_file_open_readwrite(file, NULL, &error);

    if (!iostream)
    {
        g_error("Error occurred opening tracefile %s\n", error->message);
        return NULL;
    }

    if (!g_seekable_seek(G_SEEKABLE(iostream), 0, (GSeekType) SEEK_END, NULL, &error))
    {
        g_error("Error occurred in seeking output stream: %s", error->message);
        return NULL;
    }

    return iostream;
}

typedef struct _StatisticsPrintUserData
{
    GjsContext *context;
    GFile      *specified_file;
} StatisticsPrintUserData;

static void
print_statistics_for_files(gpointer key,
                           gpointer value,
                           gpointer user_data)
{
    StatisticsPrintUserData *statistics_print_data = (StatisticsPrintUserData *) user_data;
    const gchar             *filename = (const gchar *) key;
    GFile *tracefile = open_tracefile(statistics_print_data->specified_file,
                                      filename);
    GFileIOStream *iostream = get_io_stream_at_end_position_for_tracefile(tracefile);
    GOutputStream *ostream = g_io_stream_get_output_stream(G_IO_STREAM(iostream));

    write_to_stream(ostream, "SF:");
    write_to_stream(ostream, (const gchar *) key);
    write_to_stream(ostream, "\n");
    write_to_stream(ostream, "FNF:0\n");
    write_to_stream(ostream, "FNH:0\n");
    write_to_stream(ostream, "BRF:0\n");
    write_to_stream(ostream, "BRH:0\n");

    GArray *stats = (GArray *) value;

    /* If there is no statistics for this file, then we should
     * compile the script and print statistics for it now */
    if (!stats)
    {
        guint n_executable_lines = 0;
        guint *executable_lines =
            gjs_context_get_executable_lines_for_filename(statistics_print_data->context,
                                                          filename,
                                                          0,
                                                          &n_executable_lines);

        stats = create_statistics_for_filename(filename);
        mark_executable_lines(stats,
                              executable_lines,
                              n_executable_lines);

        g_free(executable_lines);
    }

    guint i = 0;
    guint lines_hit_count = 0;
    guint executable_lines_count = 0;
    for (i = 0; i < stats->len; ++i)
    {
        gchar hit_count_buffer[64];
        gint hit_count_for_line = g_array_index(stats, gint, i);

        if (hit_count_for_line == -1)
            continue;

        write_to_stream(ostream, "DA:");

        snprintf(hit_count_buffer, 64, "%i,%i\n", i, g_array_index(stats, gint, i));
        write_to_stream(ostream, hit_count_buffer);

        if (g_array_index(stats, guint, i))
          ++lines_hit_count;

        ++executable_lines_count;
    }

    gchar lines_hit_buffer[64];
    write_to_stream(ostream, "LH:");
    snprintf(lines_hit_buffer, 64, "%i\n", lines_hit_count);
    write_to_stream(ostream, lines_hit_buffer);
    write_to_stream(ostream, "LF:");
    snprintf(lines_hit_buffer, 64, "%i\n", executable_lines_count);
    write_to_stream(ostream, lines_hit_buffer);
    write_to_stream(ostream, "end_of_record\n");

    g_object_unref(iostream);
    g_object_unref(tracefile);
}

void
gjs_debug_coverage_write_statistics(GjsDebugCoverage *coverage,
                                    GFile            *output_file)
{
    if (output_file)
        output_file = delete_file_and_open_anew(output_file);

    StatisticsPrintUserData data =
    {
        coverage->priv->context,
        output_file
    };

    g_hash_table_foreach(coverage->priv->file_statistics,
                         print_statistics_for_files,
                         &data);
}

static void
g_array_free_segment(gpointer array)
{
    if (array)
        g_array_free((GArray *) array, TRUE);
}

static void
gjs_debug_coverage_init(GjsDebugCoverage *self)
{
    self->priv = (GjsDebugCoveragePrivate *) gjs_debug_coverage_get_instance_private(self);
    self->priv->file_statistics = g_hash_table_new_full(g_str_hash,
                                                        g_str_equal,
                                                        g_free,
                                                        g_array_free_segment);
}

typedef enum _GjsDebugCoverageProperties
{
    PROP_0,
    PROP_INTERRUPT_REGISTER,
    PROP_CONTEXT,
    PROP_COVERAGE_PATHS,
    PROP_N
} GjsDebugCoverageProperties;

typedef void (*PathForeachFunc)(const gchar *path,
                                gpointer    user_data);

static void
for_each_in_strv(gchar           **strv,
                 PathForeachFunc func,
                 gpointer        user_data)

{
    gchar **iterator = strv;

    if (!iterator)
        return;

    if (*iterator)
    {
        do
        {
            (*func)(*iterator, user_data);
        }
        while (*(++iterator));
    }
}

/* This function just adds a key with no value to the
 * filename statistics. We'll create a proper source file
 * map once we get a new script callback (to avoid lots
 * of recompiling) and also create a source map on
 * coverage data generation if we didn't already have one */
static void
add_filename_key_to_statistics(GFile      *file,
                               GHashTable *statistics)
{
    gchar *path = g_file_get_path(file);
    g_hash_table_insert(statistics, path, NULL);
}

static void
recursive_scan_for_potential_js_files(GFile    *node,
                                      gpointer user_data)
{
    GFileEnumerator *enumerator =
        g_file_enumerate_children(node,
                                  "standard::*",
                                  G_FILE_QUERY_INFO_NONE,
                                  NULL,
                                  NULL);

    GFileInfo *current_file = g_file_enumerator_next_file(enumerator, NULL, NULL);

    while (current_file)
    {
        GFile *child = g_file_enumerator_get_child(enumerator, current_file);
        if (g_file_info_get_file_type(current_file) == G_FILE_TYPE_DIRECTORY)
            recursive_scan_for_potential_js_files(child, user_data);
        else if (g_file_info_get_file_type(current_file) == G_FILE_TYPE_REGULAR)
        {
            const gchar *filename = g_file_info_get_name(current_file);
            gsize       filename_len = strlen(filename);

            if (g_strcmp0(&filename[filename_len - 3], ".js") == 0)
                add_filename_key_to_statistics(child, (GHashTable *) user_data);
        }

        g_object_unref(child);
        g_object_unref(current_file);
        current_file = g_file_enumerator_next_file(enumerator, NULL, NULL);
    }

    g_object_unref(enumerator);
}

static void
begin_recursive_scan_for_potential_js_files(const gchar *toplevel_path,
                                            gpointer    user_data)
{
    GFile *toplevel_file = g_file_new_for_path(toplevel_path);
    recursive_scan_for_potential_js_files(toplevel_file, user_data);
    g_object_unref(toplevel_file);
}

static void
gjs_debug_coverage_constructed(GObject *object)
{
    G_OBJECT_CLASS(gjs_debug_coverage_parent_class)->constructed(object);

    GjsDebugCoverage *coverage = GJS_DEBUG_COVERAGE(object);

    /* Recursively scan the directories provided to us for files ending
    * with .js and add them to the coverage data hashtable */
    for_each_in_strv(coverage->priv->covered_paths,
                     begin_recursive_scan_for_potential_js_files,
                     coverage->priv->file_statistics);

    /* Add hook for new scripts and singlestep execution */
    coverage->priv->new_scripts_connection =
        gjs_interrupt_register_connect_to_script_load(coverage->priv->interrupt_register,
                                                      gjs_debug_coverage_new_script_available_hook,
                                                      coverage->priv->file_statistics);

    coverage->priv->single_step_connection =
        gjs_interrupt_register_start_singlestep(coverage->priv->interrupt_register,
                                                gjs_debug_coverage_single_step_interrupt_hook,
                                                coverage->priv->file_statistics);
}

static void
gjs_debug_coverage_set_property(GObject      *object,
                                guint        prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
    GjsDebugCoverage *coverage = GJS_DEBUG_COVERAGE(object);
    switch (prop_id)
    {
    case PROP_INTERRUPT_REGISTER:
        coverage->priv->interrupt_register = GJS_INTERRUPT_REGISTER_INTERFACE(g_value_dup_object(value));
        break;
    case PROP_CONTEXT:
        coverage->priv->context = GJS_CONTEXT (g_value_get_object (value));
        break;
    case PROP_COVERAGE_PATHS:
        if (coverage->priv->covered_paths)
            g_strfreev(coverage->priv->covered_paths);

        coverage->priv->covered_paths = (gchar **) g_value_dup_boxed (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gjs_debug_coverage_dispose(GObject *object)
{
    GjsDebugCoverage *coverage = GJS_DEBUG_COVERAGE (object);

    g_object_unref(coverage->priv->new_scripts_connection);
    g_object_unref(coverage->priv->single_step_connection);
    g_object_unref(coverage->priv->interrupt_register);
}

static void
gjs_debug_coverage_finalize (GObject *object)
{
    GjsDebugCoverage *coverage = GJS_DEBUG_COVERAGE(object);

    g_hash_table_unref(coverage->priv->file_statistics);
    g_strfreev(coverage->priv->covered_paths);
}

static void
gjs_debug_coverage_class_init (GjsDebugCoverageClass *klass)
{
    GObjectClass *object_class = (GObjectClass *) klass;

    object_class->constructed = gjs_debug_coverage_constructed;
    object_class->dispose = gjs_debug_coverage_dispose;
    object_class->finalize = gjs_debug_coverage_finalize;
    object_class->set_property = gjs_debug_coverage_set_property;

    GParamSpec *properties[] =
    {
        NULL,
        g_param_spec_object("interrupt-register",
                            "Interrupt Register",
                            "Interrupt Register",
                            GJS_TYPE_INTERRUPT_REGISTER_INTERFACE,
                            (GParamFlags) (G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE)),
        g_param_spec_object("context",
                            "Context",
                            "Running Context",
                            GJS_TYPE_CONTEXT,
                            (GParamFlags) (G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE)),
        g_param_spec_boxed("coverage-paths",
                           "Coverage Paths",
                           "Paths (and included subdirectories) of which to perform coverage analysis",
                           G_TYPE_STRV,
                           (GParamFlags) (G_PARAM_CONSTRUCT | G_PARAM_WRITABLE))
    };

    g_object_class_install_properties(object_class,
                                      PROP_N,
                                      properties);
}

/**
 * gjs_debug_coverage_new:
 * @interrupt_register: (transfer full): A #GjsDebugInterruptRegister to register callbacks on
 * @context: (transfer full): A #GjsContext
 * @covered_directories: (transfer none): A null-terminated strv of directories to generate
 * coverage_data for
 *
 * Returns: A #GjsDebugCoverage
 */
GjsDebugCoverage *
gjs_debug_coverage_new (GjsInterruptRegister *interrupt_register,
                        GjsContext           *context,
                        const gchar          **coverage_paths)
{
    if (!coverage_paths)
        return NULL;

    GjsDebugCoverage *coverage =
        GJS_DEBUG_COVERAGE(g_object_new(GJS_TYPE_DEBUG_COVERAGE,
                                        "interrupt-register", interrupt_register,
                                        "context", context,
                                        "coverage-paths", coverage_paths,
                                        NULL));

    return coverage;
}
