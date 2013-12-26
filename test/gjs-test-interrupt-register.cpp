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

#include <errno.h>
#include <unistd.h>
#include <sys/types.h>

#include <jsapi.h>
#include <jsdbgapi.h>
#include <gjs/gjs.h>
#include <gjs/jsapi-util.h>
#include <gjs/interrupt-register.h>
#include <gjs/debug-interrupt-register.h>
#include <gjs/executable-linesutil.h>
#include <gjs/debug-connection.h>

typedef struct _GjsDebugInterruptRegisterFixture
{
    GjsContext                *context;
    GjsDebugInterruptRegister *interrupt_register;
    gchar                     *temporary_js_script_filename;
    gint                      temporary_js_script_open_handle;
} GjsDebugInterruptRegisterFixture;

static void
gjs_debug_interrupt_register_fixture_set_up (gpointer      fixture_data,
                                             gconstpointer user_data)
{
    GjsDebugInterruptRegisterFixture *fixture = (GjsDebugInterruptRegisterFixture *) fixture_data;
    const gchar                      *js_script = "function f () { return 1; }\n";
    fixture->context = gjs_context_new();
    fixture->interrupt_register = gjs_debug_interrupt_register_new(fixture->context);
    fixture->temporary_js_script_open_handle = g_file_open_tmp("mock-js-XXXXXXX.js",
                                                               &fixture->temporary_js_script_filename,
                                                               NULL);

    if (write(fixture->temporary_js_script_open_handle, js_script, strlen(js_script) * sizeof (gchar)) == -1)
        g_print("Error writing to temporary file: %s", strerror(errno));
}

static void
gjs_debug_interrupt_register_fixture_tear_down(gpointer      fixture_data,
                                               gconstpointer user_data)
{
    GjsDebugInterruptRegisterFixture *fixture = (GjsDebugInterruptRegisterFixture *) fixture_data;
    unlink(fixture->temporary_js_script_filename);
    g_free(fixture->temporary_js_script_filename);
    close(fixture->temporary_js_script_open_handle);

    g_object_unref(fixture->interrupt_register);
    g_object_unref(fixture->context);
}

typedef GjsDebugConnection *
(*GjsDebugInterruptRegisterConnectionFunction)(GjsDebugInterruptRegister *,
                                               const gchar               *,
                                               guint                      ,
                                               GCallback                  ,
                                               gpointer                   ,
                                               GError                     *);

static void
dummy_callback_for_connector(GjsInterruptRegister *reg,
                             GjsContext           *context,
                             gpointer             data,
                             gpointer             user_data)
{
}

static GjsDebugConnection *
add_dummy_connection_from_function(GjsDebugInterruptRegisterFixture            *fixture,
                                   GjsDebugInterruptRegisterConnectionFunction connector)
{
    return (*connector)(fixture->interrupt_register,
                        fixture->temporary_js_script_filename,
                        0,
                        (GCallback) dummy_callback_for_connector,
                        NULL,
                        NULL);
}

static void
test_debug_mode_on_while_there_are_active_connections(gpointer      fixture_data,
                                                      gconstpointer user_data)
{
    GjsDebugInterruptRegisterFixture *fixture = (GjsDebugInterruptRegisterFixture *) fixture_data;
    GjsDebugInterruptRegisterConnectionFunction connector = (GjsDebugInterruptRegisterConnectionFunction) user_data;
    GjsDebugConnection *connection =
        add_dummy_connection_from_function(fixture, connector);
    JSContext *js_context = (JSContext *) gjs_context_get_native_context(fixture->context);
    JSAutoCompartment ac(js_context,
                         JS_GetGlobalObject(js_context));

    g_assert(JS_GetDebugMode(js_context) == JS_TRUE);
    g_object_unref(connection);
}

static void
test_debug_mode_off_when_active_connections_are_released(gpointer      fixture_data,
                                                         gconstpointer user_data)
{
    GjsDebugInterruptRegisterFixture *fixture = (GjsDebugInterruptRegisterFixture *) fixture_data;
    GjsDebugInterruptRegisterConnectionFunction connector = (GjsDebugInterruptRegisterConnectionFunction) user_data;
    GjsDebugConnection *connection =
        add_dummy_connection_from_function(fixture, connector);
    g_object_unref(connection);
    JSContext *js_context = (JSContext *) gjs_context_get_native_context(fixture->context);
    JSAutoCompartment ac(js_context,
                         JS_GetGlobalObject (js_context));

    g_assert (JS_GetDebugMode (js_context) == JS_FALSE);
}

static void
single_step_mock_interrupt_callback(GjsInterruptRegister *reg,
                                    GjsContext           *context,
                                    GjsInterruptInfo     *info,
                                    gpointer             user_data)
{
    guint *hit_count = (guint *) user_data;
    ++(*hit_count);
}

static void
test_interrupts_are_recieved_in_single_step_mode (gpointer      fixture_data,
                                                  gconstpointer user_data)
{
    GjsDebugInterruptRegisterFixture *fixture = (GjsDebugInterruptRegisterFixture *) fixture_data;
    guint hit_count = 0;
    GjsDebugConnection *connection =
        gjs_interrupt_register_start_singlestep(GJS_INTERRUPT_REGISTER_INTERFACE (fixture->interrupt_register),
                                                single_step_mock_interrupt_callback,
                                                &hit_count);
    gjs_context_eval_file(fixture->context,
                          fixture->temporary_js_script_filename,
                          NULL,
                          NULL);
    g_object_unref(connection);
    g_assert(hit_count > 0);
}

static void
test_interrupts_are_not_recieved_after_single_step_mode_unlocked (gpointer      fixture_data,
                                                                  gconstpointer user_data)
{
    GjsDebugInterruptRegisterFixture *fixture = (GjsDebugInterruptRegisterFixture *) fixture_data;
    guint hit_count = 0;
    GjsDebugConnection *connection =
        gjs_interrupt_register_start_singlestep(GJS_INTERRUPT_REGISTER_INTERFACE (fixture->interrupt_register),
                                                single_step_mock_interrupt_callback,
                                                &hit_count);
    g_object_unref (connection);
    gjs_context_eval_file(fixture->context,
                          fixture->temporary_js_script_filename,
                          NULL,
                          NULL);
    g_assert(hit_count == 0);
}

static gboolean
guint_in_guint_array(guint *array,
                     guint array_len,
                     guint n)
{
    guint i = 0;
    for (; i < array_len; ++i)
        if (array[i] == n)
            return TRUE;

    return FALSE;
}

static void
single_step_mock_interrupt_callback_tracking_lines (GjsInterruptRegister *reg,
                                                    GjsContext           *context,
                                                    GjsInterruptInfo     *info,
                                                    gpointer             user_data)
{
    GArray *line_tracker = (GArray *) user_data;
    guint adjusted_line = info->line;

    if (!guint_in_guint_array((guint *) line_tracker->data,
                              line_tracker->len,
                              adjusted_line))
        g_array_append_val(line_tracker, adjusted_line);
}

static gboolean
known_executable_lines_are_subset_of_executed_lines(const GArray *executed_lines,
                                                    const guint  *executable_lines,
                                                    gsize        executable_lines_len)
{
    guint i = 0, j = 0;
    for (; i < executable_lines_len; ++i)
    {
        gboolean found_executable_line_in_executed_lines = FALSE;
        for (j = 0; j < executed_lines->len; ++j)
        {
            if (g_array_index (executed_lines, guint, j) == executable_lines[i])
                found_executable_line_in_executed_lines = TRUE;
        }

        if (!found_executable_line_in_executed_lines)
            return FALSE;
    }

    return TRUE;
}

static void
write_content_to_file_at_beginning(gint        handle,
                                   const gchar *content)
{
    if (ftruncate(handle, 0) == -1)
        g_error ("Failed to erase mock file: %s", strerror(errno));
    lseek (handle, 0, SEEK_SET);
    if (write(handle, (gconstpointer) content, strlen(content) * sizeof (gchar)) == -1)
        g_error ("Failed to write to mock file: %s", strerror(errno));
}

static void
test_interrupts_are_received_on_all_executable_lines_in_single_step_mode (gpointer      fixture_data,
                                                                          gconstpointer user_data)
{
    GArray *line_tracker = g_array_new (FALSE, TRUE, sizeof (guint));
    GjsDebugInterruptRegisterFixture *fixture = (GjsDebugInterruptRegisterFixture *) fixture_data;
    GjsDebugConnection *connection =
        gjs_interrupt_register_start_singlestep(GJS_INTERRUPT_REGISTER_INTERFACE(fixture->interrupt_register),
                                                single_step_mock_interrupt_callback_tracking_lines,
                                                line_tracker);
    const gchar mock_script[] =
        "let a = 1;\n" \
        "let b = 2;\n" \
        "\n" \
        "function func (a, b) {\n" \
        "    let result = a + b;\n" \
        "    return result;\n" \
        "}\n" \
        "\n" \
        "let c = func (a, b);\n"
        "\n";

    write_content_to_file_at_beginning(fixture->temporary_js_script_open_handle,
                                       mock_script);

    guint n_executable_lines = 0;
    guint *executable_lines =
        gjs_context_get_executable_lines_for_filename(fixture->context,
                                                      fixture->temporary_js_script_filename,
                                                      0,
                                                      &n_executable_lines);

    gjs_context_eval_file(fixture->context,
                          fixture->temporary_js_script_filename,
                          NULL,
                          NULL);

    g_assert(known_executable_lines_are_subset_of_executed_lines(line_tracker,
                                                                 executable_lines,
                                                                 n_executable_lines) == TRUE);

    g_free(executable_lines);
    g_array_free(line_tracker, TRUE);
    g_object_unref(connection);
}

static void
mock_breakpoint_callback(GjsInterruptRegister *reg,
                         GjsContext           *context,
                         GjsInterruptInfo     *info,
                         gpointer             user_data)
{
    guint *line_hit = (guint *) user_data;
    *line_hit = info->line;
}

static void
test_breakpoint_is_hit_when_adding_before_script_run(gpointer      fixture_data,
                                                     gconstpointer user_data)
{
    GjsDebugInterruptRegisterFixture *fixture = (GjsDebugInterruptRegisterFixture *) fixture_data;
    const gchar mock_script[] =
        "let a = 1;\n"
        "let expected_breakpoint_line = 1;\n"
        "\n";

    write_content_to_file_at_beginning(fixture->temporary_js_script_open_handle,
                                       mock_script);

    guint line_hit = 0;
    GjsDebugConnection *connection =
        gjs_interrupt_register_add_breakpoint(GJS_INTERRUPT_REGISTER_INTERFACE (fixture->interrupt_register),
                                              fixture->temporary_js_script_filename,
                                              1,
                                              mock_breakpoint_callback,
                                              &line_hit);

    gjs_context_eval_file(fixture->context,
                          fixture->temporary_js_script_filename,
                          NULL,
                          NULL);

    g_assert(line_hit == 1);

    g_object_unref(connection);
}

static void
test_breakpoint_is_not_hit_when_later_removed (gpointer      fixture_data,
                                               gconstpointer user_data)
{
    GjsDebugInterruptRegisterFixture *fixture = (GjsDebugInterruptRegisterFixture *) fixture_data;
    const gchar mock_script[] =
        "let a = 1;\n"
        "let expected_breakpoint_line = 1;\n"
        "\n";

    write_content_to_file_at_beginning(fixture->temporary_js_script_open_handle,
                                       mock_script);

    guint line_hit = 0;
    GjsDebugConnection *connection =
        gjs_interrupt_register_add_breakpoint(GJS_INTERRUPT_REGISTER_INTERFACE (fixture->interrupt_register),
                                              fixture->temporary_js_script_filename,
                                              1,
                                              mock_breakpoint_callback,
                                              &line_hit);
    g_object_unref(connection);

    gjs_context_eval_file(fixture->context,
                          fixture->temporary_js_script_filename,
                          NULL,
                          NULL);

    g_assert(line_hit == 0);
}

static void
mock_function_calls_and_execution_interrupt_handler(GjsInterruptRegister *reg,
                                                    GjsContext           *context,
                                                    GjsFrameInfo         *info,
                                                    gpointer             user_data)
{
    gboolean *interrupts_received = (gboolean *) user_data;
    *interrupts_received = TRUE;
}

static void
test_interrupts_received_when_connected_to_function_calls_and_execution(gpointer      fixture_data,
                                                                        gconstpointer user_data)
{
    GjsDebugInterruptRegisterFixture *fixture = (GjsDebugInterruptRegisterFixture *) fixture_data;
    const gchar mock_script[] =
        "let a = 1;\n"
        "\n";

    write_content_to_file_at_beginning(fixture->temporary_js_script_open_handle,
                                       mock_script);

    gboolean interrupts_received = FALSE;

    GjsDebugConnection *connection =
        gjs_interrupt_register_connect_to_function_calls_and_execution(GJS_INTERRUPT_REGISTER_INTERFACE (fixture->interrupt_register),
                                                                       mock_function_calls_and_execution_interrupt_handler,
                                                                       &interrupts_received);

    gjs_context_eval_file(fixture->context,
                          fixture->temporary_js_script_filename,
                          NULL,
                          NULL);

    g_assert(interrupts_received == TRUE);

    g_object_unref(connection);
}

static void
mock_function_calls_and_execution_interrupt_handler_recording_functions (GjsInterruptRegister *reg,
                                                                         GjsContext           *context,
                                                                         GjsFrameInfo         *info,
                                                                         gpointer             user_data)
{
    GList **function_names_hit = (GList **) user_data;

    *function_names_hit = g_list_append (*function_names_hit,
                                         g_strdup(info->interrupt.functionName));
}

static gboolean
check_if_string_elements_are_in_list (GList       *list,
                                      const gchar **elements,
                                      gsize       n_elements)
{
    if (elements && !list)
        return FALSE;

    guint i = 0;
    for (; i < n_elements; ++i)
    {
        GList *iter = list;
        gboolean found = FALSE;

        while (iter)
        {
            if (g_strcmp0 ((const gchar *) iter->data, elements[i]) == 0)
                found = TRUE;

            iter = g_list_next  (iter);
        }

        if (!found)
            return FALSE;
    }

    return TRUE;
}

static void
test_expected_function_names_hit_when_connected_to_calls_and_execution_handler (gpointer      fixture_data,
                                                                                gconstpointer user_data)
{
  GjsDebugInterruptRegisterFixture *fixture = (GjsDebugInterruptRegisterFixture *) fixture_data;
  const gchar mock_script[] =
      "let a = 1;\n"
      "function foo (a) {\n"
      "    return a;\n"
      "}\n"
      "let b = foo (a);\n"
      "\n";

  write_content_to_file_at_beginning(fixture->temporary_js_script_open_handle,
                                     mock_script);

  GList *function_names_hit = NULL;
  GjsDebugConnection *connection =
      gjs_interrupt_register_connect_to_function_calls_and_execution(GJS_INTERRUPT_REGISTER_INTERFACE (fixture->interrupt_register),
                                                                     mock_function_calls_and_execution_interrupt_handler_recording_functions,
                                                                     &function_names_hit);

  gjs_context_eval_file(fixture->context,
                        fixture->temporary_js_script_filename,
                        NULL,
                        NULL);

  const gchar *expected_function_names_hit[] =
  {
    "foo"
  };
  const gsize expected_function_names_hit_len =
      sizeof (expected_function_names_hit) / sizeof (expected_function_names_hit[0]);

  g_assert(check_if_string_elements_are_in_list(function_names_hit,
                                                expected_function_names_hit,
                                                expected_function_names_hit_len));

  if (function_names_hit)
    g_list_free_full(function_names_hit, g_free);

  g_object_unref(connection);
}

static void
test_nothing_hit_when_function_calls_and_toplevel_execution_handler_removed (gpointer      fixture_data,
                                                                             gconstpointer user_data)
{
    GjsDebugInterruptRegisterFixture *fixture = (GjsDebugInterruptRegisterFixture *) fixture_data;
    const gchar mock_script[] =
        "let a = 1;\n"
        "function foo (a) {\n"
        "    return a;\n"
        "}\n"
        "let b = foo (a);\n"
        "\n";

    write_content_to_file_at_beginning(fixture->temporary_js_script_open_handle,
                                       mock_script);

    GList *function_names_hit = NULL;
    GjsDebugConnection *connection =
        gjs_interrupt_register_connect_to_function_calls_and_execution(GJS_INTERRUPT_REGISTER_INTERFACE(fixture->interrupt_register),
                                                                       mock_function_calls_and_execution_interrupt_handler_recording_functions,
                                                                       &function_names_hit);
    g_object_unref(connection);

    gjs_context_eval_file(fixture->context,
                          fixture->temporary_js_script_filename,
                          NULL,
                          NULL);

    g_assert(function_names_hit == NULL);
}

static void
replace_string(gchar       **string_pointer,
               const gchar *new_string)
{
    if (*string_pointer)
        g_free (*string_pointer);

    *string_pointer = g_strdup (new_string);
}

static void
mock_new_script_hook(GjsInterruptRegister *reg,
                     GjsContext           *context,
                     GjsDebugScriptInfo   *info,
                     gpointer             user_data)
{
    gchar **last_loaded_script = (gchar **) user_data;

    replace_string(last_loaded_script,
                   info->filename);
}

static void
test_script_load_notification_sent_on_new_script(gpointer      fixture_data,
                                                 gconstpointer user_data)
{
    GjsDebugInterruptRegisterFixture *fixture = (GjsDebugInterruptRegisterFixture *) fixture_data;
    const gchar loadable_script[] = "let a = 1;\n\n";

    write_content_to_file_at_beginning(fixture->temporary_js_script_open_handle,
                                       loadable_script);

    gchar *last_loaded_script = NULL;
    GjsDebugConnection *connection =
        gjs_interrupt_register_connect_to_script_load(GJS_INTERRUPT_REGISTER_INTERFACE (fixture->interrupt_register),
                                                      mock_new_script_hook,
                                                      &last_loaded_script);

    gjs_context_eval_file(fixture->context,
                          fixture->temporary_js_script_filename,
                          NULL,
                          NULL);

    g_assert(last_loaded_script != NULL &&
             g_strcmp0(last_loaded_script,
                       fixture->temporary_js_script_filename) == 0);

    g_object_unref(connection);
}

static void
test_script_load_notification_not_sent_on_connection_removed(gpointer      fixture_data,
                                                             gconstpointer user_data)
{
    GjsDebugInterruptRegisterFixture *fixture = (GjsDebugInterruptRegisterFixture *) fixture_data;
    const gchar loadable_script[] = "let a = 1;\n\n";

    write_content_to_file_at_beginning(fixture->temporary_js_script_open_handle,
                                       loadable_script);

    gchar *last_loaded_script = NULL;
    GjsDebugConnection *connection =
        gjs_interrupt_register_connect_to_script_load(GJS_INTERRUPT_REGISTER_INTERFACE (fixture->interrupt_register),
                                                      mock_new_script_hook,
                                                      &last_loaded_script);

    g_object_unref(connection);

    gjs_context_eval_file(fixture->context,
                          fixture->temporary_js_script_filename,
                          NULL,
                          NULL);

    g_assert(last_loaded_script == NULL);
}

typedef void (*TestDataFunc)(gpointer      data,
                             gconstpointer user_data);

static void
for_each_in_table_driven_test_data(gconstpointer test_data,
                                   gsize         element_size,
                                   gsize         n_elements,
                                   TestDataFunc  func,
                                   gconstpointer user_data)
{
    const gchar *test_data_iterator = (const gchar *) test_data;
    gsize i = 0;
    for (; i < n_elements; ++i, test_data_iterator += element_size)
        (*func)(const_cast <gchar *> (test_data_iterator), user_data);
}

typedef struct _FixturedTest
{
    gsize            fixture_size;
    GTestFixtureFunc set_up;
    GTestFixtureFunc tear_down;
} FixturedTest;

static void
add_test_for_fixture(const gchar      *name,
                     FixturedTest     *fixture,
                     GTestFixtureFunc test_func,
                     gconstpointer    user_data)
{
    g_test_add_vtable(name,
                      fixture->fixture_size,
                      user_data,
                      fixture->set_up,
                      test_func,
                      fixture->tear_down);
}

typedef struct _FixturedTableDrivenTestData
{
    const gchar      *test_name;
    gsize            fixture_size;
    GTestFixtureFunc set_up;
    GTestFixtureFunc test_func;
    GTestFixtureFunc tear_down;
} FixturedTableDrivenTestData;

static void
add_test_for_fixture_size_and_funcs(gpointer      data,
                                    gconstpointer user_data)
{
    const FixturedTableDrivenTestData *fixtured_table_driven_test = (const FixturedTableDrivenTestData *) data;
    FixturedTest fixtured_test =
    {
        fixtured_table_driven_test->fixture_size,
        fixtured_table_driven_test->set_up,
        fixtured_table_driven_test->tear_down
    };
    add_test_for_fixture(fixtured_table_driven_test->test_name,
                         &fixtured_test,
                         fixtured_table_driven_test->test_func,
                         user_data);

}

typedef struct _GjsDebugInterruptRegisterContextStateData
{
    const gchar                                 *test_name;
    GjsDebugInterruptRegisterConnectionFunction connector;
} GjsDebugInterruptRegisterContextStateData;

typedef struct _GjsDebugInterruptRegisterTableDrivenTest
{
    const gchar      *prefix;
    GTestFixtureFunc test_function;
} GjsDebugInterruptRegisterTableDrivenTest;

static void
add_gjs_debug_interrupt_register_context_state_data_test(gpointer      data,
                                                         gconstpointer user_data)
{
    const GjsDebugInterruptRegisterTableDrivenTest  *test = (const GjsDebugInterruptRegisterTableDrivenTest *) user_data;
    GjsDebugInterruptRegisterContextStateData       *table_data = (GjsDebugInterruptRegisterContextStateData *) data;

    gchar *test_name = g_strconcat(test->prefix, "/", table_data->test_name, NULL);

    FixturedTableDrivenTestData fixtured_data =
    {
        test_name,
        sizeof(GjsDebugInterruptRegisterFixture),
        gjs_debug_interrupt_register_fixture_set_up,
        test->test_function,
        gjs_debug_interrupt_register_fixture_tear_down
    };

    add_test_for_fixture_size_and_funcs(&fixtured_data,
                                        (gconstpointer) table_data->connector);

    g_free (test_name);
}

void add_tests_for_debug_register()
{
    const GjsDebugInterruptRegisterContextStateData context_state_data[] =
    {
        { "add_breakpoint", (GjsDebugInterruptRegisterConnectionFunction) gjs_interrupt_register_add_breakpoint },
        { "start_singlestep", (GjsDebugInterruptRegisterConnectionFunction) gjs_interrupt_register_start_singlestep },
        { "connect_to_script_load", (GjsDebugInterruptRegisterConnectionFunction) gjs_interrupt_register_connect_to_script_load },
        { "connect_to_function_calls_and_execution", (GjsDebugInterruptRegisterConnectionFunction) gjs_interrupt_register_connect_to_function_calls_and_execution }
    };
    const gsize context_state_data_len =
        sizeof (context_state_data) / sizeof (context_state_data[0]);

    const GjsDebugInterruptRegisterTableDrivenTest interrupt_register_tests_info[] =
    {
        {
            "/gjs/debug/interrupt_register/debug_mode_is_on_when_connection_from",
            test_debug_mode_on_while_there_are_active_connections
        },
        {
            "/gjs/debug/interrupt_register/debug_mode_off_when_connection_released",
            test_debug_mode_off_when_active_connections_are_released
        }
    };
    const gsize interrupt_register_tests_info_size =
        sizeof (interrupt_register_tests_info) / sizeof (interrupt_register_tests_info[0]);

    gsize i = 0;
    for (; i < interrupt_register_tests_info_size; ++i)
        for_each_in_table_driven_test_data(&context_state_data,
                                           sizeof (GjsDebugInterruptRegisterContextStateData),
                                           context_state_data_len,
                                           add_gjs_debug_interrupt_register_context_state_data_test,
                                           (gconstpointer) &interrupt_register_tests_info[i]);

    FixturedTest debug_interrupt_register_fixture =
    {
        sizeof (GjsDebugInterruptRegisterFixture),
        gjs_debug_interrupt_register_fixture_set_up,
        gjs_debug_interrupt_register_fixture_tear_down
    };

    add_test_for_fixture("/gjs/debug/interrupt_register/interrupts_recieved_when_in_single_step_mode",
                         &debug_interrupt_register_fixture,
                         test_interrupts_are_recieved_in_single_step_mode,
                         NULL);
    add_test_for_fixture("/gjs/debug/interrupt_register/interrupts_not_received_after_single_step_mode_unlocked",
                         &debug_interrupt_register_fixture,
                         test_interrupts_are_not_recieved_after_single_step_mode_unlocked,
                         NULL);
    add_test_for_fixture("/gjs/debug/interrupt_register/interrupts_received_on_expected_lines_of_script",
                         &debug_interrupt_register_fixture,
                         test_interrupts_are_received_on_all_executable_lines_in_single_step_mode,
                         NULL);
    add_test_for_fixture("/gjs/debug/interrupt_register/breakpoint_hit_when_added_before_script_run",
                         &debug_interrupt_register_fixture,
                         test_breakpoint_is_hit_when_adding_before_script_run,
                         NULL);
    add_test_for_fixture("/gjs/debug/interrupt_register/interrupts_received_when_connected_to_function_calls_and_execution",
                         &debug_interrupt_register_fixture,
                         test_interrupts_received_when_connected_to_function_calls_and_execution,
                         NULL);
    add_test_for_fixture("/gjs/debug/interrupt_register/interrupts_received_for_expected_functions_when_connected_to_function_calls_and_execution",
                         &debug_interrupt_register_fixture,
                         test_expected_function_names_hit_when_connected_to_calls_and_execution_handler,
                         NULL);
    add_test_for_fixture("/gjs/debug/interrupt_register/interrupts_not_received_when_function_calls_and_execution_hook_is_removed",
                         &debug_interrupt_register_fixture,
                         test_nothing_hit_when_function_calls_and_toplevel_execution_handler_removed,
                         NULL);
    add_test_for_fixture("/gjs/debug/interrupt_register/new_script_notification_sent_when_listener_installed",
                         &debug_interrupt_register_fixture,
                         test_script_load_notification_sent_on_new_script,
                         NULL);
    add_test_for_fixture("/gjs/debug/interrupt_register/new_script_notification_not_sent_when_listener_uninstalled",
                         &debug_interrupt_register_fixture,
                         test_script_load_notification_not_sent_on_connection_removed,
                         NULL);
}
