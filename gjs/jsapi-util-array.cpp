/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * Copyright (c) 2008  litl, LLC
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

#include "jsapi-util.h"
#include "compat.h"

/* Maximum number of elements allowed in a GArray of rooted JS::Values.
 * We pre-alloc that amount and then never allow the array to grow,
 * or we'd have invalid memory rooted if the internals of GArray decide
 * to move the contents to a new memory area
 */
#define ARRAY_MAX_LEN 32

/**
 * gjs_rooted_array_new:
 *
 * Creates an opaque data type that holds JS::Values and keeps
 * their location (NOT their value) GC-rooted.
 *
 * Returns: an opaque object prepared to hold GC root locations.
 **/
GjsRootedArray*
gjs_rooted_array_new()
{
    GArray *array;

    /* we prealloc ARRAY_MAX_LEN to avoid realloc */
    array = g_array_sized_new(false,             /* zero-terminated */
                              false,             /* clear */
                              sizeof(JS::Value), /* element size */
                              ARRAY_MAX_LEN);    /* reserved size */

    return (GjsRootedArray*) array;
}

/* typesafe wrapper */
static void
add_root_jsval(JSContext *context,
               JS::Value *value_p)
{
    JS_BeginRequest(context);
    JS_AddValueRoot(context, value_p);
    JS_EndRequest(context);
}

/* typesafe wrapper */
static void
remove_root_jsval(JSContext *context,
                  JS::Value *value_p)
{
    JS_BeginRequest(context);
    JS_RemoveValueRoot(context, value_p);
    JS_EndRequest(context);
}

/**
 * gjs_rooted_array_append:
 * @context: a #JSContext
 * @array: a #GjsRootedArray created by gjs_rooted_array_new()
 * @value: a JS::Value
 *
 * Appends @value to @array, calling JS_AddValueRoot on the location where it's
 * stored.
 *
 **/
void
gjs_rooted_array_append(JSContext        *context,
                        GjsRootedArray *array,
                        JS::Value       value)
{
    GArray *garray;

    g_return_if_fail(context != NULL);
    g_return_if_fail(array != NULL);

    garray = (GArray*) array;

    if (garray->len >= ARRAY_MAX_LEN) {
        gjs_throw(context, "Maximum number of values (%d)",
                     ARRAY_MAX_LEN);
        return;
    }

    g_array_append_val(garray, value);
    add_root_jsval(context, & g_array_index(garray, JS::Value, garray->len - 1));
}

/**
 * gjs_rooted_array_get:
 * @context: a #JSContext
 * @array: an array
 * @i: element to return
 * Returns: value of an element
 */
JS::Value
gjs_rooted_array_get(JSContext        *context,
                     GjsRootedArray *array,
                     int               i)
{
    GArray *garray;

    g_return_val_if_fail(context != NULL, JS::UndefinedValue());
    g_return_val_if_fail(array != NULL, JS::UndefinedValue());

    garray = (GArray*) array;

    if (i < 0 || i >= (int) garray->len) {
        gjs_throw(context, "Index %d is out of range", i);
        return JS::UndefinedValue();
    }

    return g_array_index(garray, JS::Value, i);
}

/**
 * gjs_rooted_array_get_data:
 *
 * @context: a #JSContext
 * @array: an array
 * Returns: the rooted JS::Value locations in the array
 */
JS::Value *
gjs_rooted_array_get_data(JSContext      *context,
                          GjsRootedArray *array)
{
    GArray *garray;

    g_return_val_if_fail(context != NULL, NULL);
    g_return_val_if_fail(array != NULL, NULL);

    garray = (GArray*) array;

    return (JS::Value *) garray->data;
}

/**
 * gjs_rooted_array_get_length:
 *
 * @context: a #JSContext
 * @array: an array
 * Returns: number of JS::Value in the rooted array
 */
int
gjs_rooted_array_get_length (JSContext        *context,
                             GjsRootedArray *array)
{
    GArray *garray;

    g_return_val_if_fail(context != NULL, 0);
    g_return_val_if_fail(array != NULL, 0);

    garray = (GArray*) array;

    return garray->len;
}

/**
 * gjs_root_value_locations:
 * @context: a #JSContext
 * @locations: contiguous locations in memory that store JS::Values (must be
 *   initialized)
 * @n_locations: the number of locations to root
 *
 * Calls JS_AddValueRoot() on each address in @locations.
 *
 **/
void
gjs_root_value_locations(JSContext        *context,
                         JS::Value        *locations,
                         int               n_locations)
{
    int i;

    g_return_if_fail(context != NULL);
    g_return_if_fail(locations != NULL);
    g_return_if_fail(n_locations >= 0);

    JS_BeginRequest(context);
    for (i = 0; i < n_locations; i++) {
        add_root_jsval(context, ((JS::Value *)locations) + i);
    }
    JS_EndRequest(context);
}

/**
 * gjs_unroot_value_locations:
 * @context: a #JSContext
 * @locations: contiguous locations in memory that store JS::Values and have
 *   been added as GC roots
 * @n_locations: the number of locations to unroot
 *
 * Calls JS_RemoveValueRoot() on each address in @locations.
 *
 **/
void
gjs_unroot_value_locations(JSContext *context,
                           JS::Value *locations,
                           int        n_locations)
{
    int i;

    g_return_if_fail(context != NULL);
    g_return_if_fail(locations != NULL);
    g_return_if_fail(n_locations >= 0);

    JS_BeginRequest(context);
    for (i = 0; i < n_locations; i++) {
        remove_root_jsval(context, ((JS::Value *)locations) + i);
    }
    JS_EndRequest(context);
}

/**
 * gjs_set_values:
 * @context: a #JSContext
 * @locations: array of JS::Value
 * @n_locations: the number of elements to set
 * @initializer: what to set each element to
 *
 * Assigns initializer to each member of the given array.
 *
 **/
void
gjs_set_values(JSContext        *context,
               JS::Value        *locations,
               int               n_locations,
               JS::Value         initializer)
{
    int i;

    g_return_if_fail(context != NULL);
    g_return_if_fail(locations != NULL);
    g_return_if_fail(n_locations >= 0);

    for (i = 0; i < n_locations; i++) {
        locations[i] = initializer;
    }
}

/**
 * gjs_rooted_array_free:
 * @context: a #JSContext
 * @array: a #GjsRootedArray created with gjs_rooted_array_new()
 * @free_segment: whether or not to free and unroot the internal JS::Value array
 *
 * Frees the memory allocated for the #GjsRootedArray. If @free_segment is
 * true the internal memory block allocated for the JS::Value array will
 * be freed and unrooted also.
 *
 * Returns: the JS::Value array if it was not freed
 **/
JS::Value *
gjs_rooted_array_free(JSContext        *context,
                      GjsRootedArray *array,
                      bool            free_segment)
{
    GArray *garray;

    g_return_val_if_fail(context != NULL, NULL);
    g_return_val_if_fail(array != NULL, NULL);

    garray = (GArray*) array;

    if (free_segment)
        gjs_unroot_value_locations(context, (JS::Value *) garray->data, garray->len);

    return (JS::Value *) g_array_free(garray, free_segment);
}
