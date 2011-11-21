/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/* Copyright 2008 litl, LLC.
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

#include "dbus-values.h"

#include <gjs/gjs-module.h>
#include <gjs/compat.h>

#include <gjs-dbus/dbus.h>
#include <util/log.h>

#include <string.h>

static JSBool
_gjs_js_one_value_from_dbus_array_dict_entry(JSContext        *context,
                                             DBusMessageIter  *iter,
                                             jsval            *value_p)
{
    /* Create a dictionary object */
    JSObject *obj;
    DBusMessageIter array_iter;
    jsval key_value, entry_value;
    JSString *key_str;
    char *key;
    JSBool retval = JS_FALSE;

    obj = JS_ConstructObject(context, NULL, NULL, NULL);
    if (obj == NULL)
        return JS_FALSE;

    key = NULL;
    key_value = JSVAL_VOID;
    entry_value = JSVAL_VOID;
    key_str = NULL;

    JS_AddObjectRoot(context, &obj);
    JS_AddValueRoot(context, &key_value);
    JS_AddValueRoot(context, &entry_value);
    JS_AddStringRoot(context, &key_str);

    dbus_message_iter_recurse(iter, &array_iter);

    while (dbus_message_iter_get_arg_type(&array_iter) != DBUS_TYPE_INVALID) {
        DBusMessageIter entry_iter;

        /* Cleanup from previous loop */
        g_free(key);
        key = NULL;

        dbus_message_iter_recurse(&array_iter, &entry_iter);

        if (!dbus_type_is_basic(dbus_message_iter_get_arg_type(&entry_iter))) {
            gjs_throw(context, "Dictionary keys are not a basic type, can't convert to JavaScript");
            goto out;
        }

        if (!gjs_js_one_value_from_dbus(context, &entry_iter, &key_value))
            goto out;

        key_str = JS_ValueToString(context, key_value);
        if (key_str == NULL) {
            gjs_throw(context, "Couldn't convert value to string");
            goto out;
        }
        if (!gjs_string_to_utf8(context, STRING_TO_JSVAL(key_str), &key))
            goto out;

        dbus_message_iter_next(&entry_iter);

        gjs_debug_dbus_marshal("Defining dict entry %s in jsval dict", key);

        if (!gjs_js_one_value_from_dbus(context, &entry_iter, &entry_value))
            goto out;

        if (!JS_DefineProperty(context, obj,
                               key, entry_value,
                               NULL, NULL, JSPROP_ENUMERATE))
            goto out;

        dbus_message_iter_next(&array_iter);
    }

    *value_p = OBJECT_TO_JSVAL(obj);
    retval = JS_TRUE;
 out:
    g_free(key);
    JS_RemoveObjectRoot(context, &obj);
    JS_RemoveValueRoot(context, &key_value);
    JS_RemoveValueRoot(context, &entry_value);
    JS_RemoveStringRoot(context, &key_str);
    return retval;
}

static JSBool
_gjs_js_one_value_from_dbus_array_byte(JSContext       *context,
                                       DBusMessageIter *iter,
                                       jsval           *value_p)
{
    /* byte arrays go to a string */
    const char *v_BYTES;
    int n_bytes;
    DBusMessageIter array_iter;

    dbus_message_iter_recurse(iter, &array_iter);
    dbus_message_iter_get_fixed_array(&array_iter,
                                      &v_BYTES, &n_bytes);

    return gjs_string_from_binary_data(context, v_BYTES, n_bytes, value_p);
}

static JSBool
_gjs_js_one_value_from_dbus_struct(JSContext       *context,
                                   DBusMessageIter *iter,
                                   jsval           *value_p)
{
    JSObject *obj;
    DBusMessageIter struct_iter;
    int index;
    jsval prop_value;
    JSBool retval = JS_FALSE;

    obj = JS_NewArrayObject(context, 0, NULL);
    if (obj == NULL)
        return JS_FALSE;

    prop_value = JSVAL_VOID;
    JS_AddValueRoot(context, &prop_value);

    dbus_message_iter_recurse(iter, &struct_iter);
    index = 0;
    while (dbus_message_iter_get_arg_type(&struct_iter) != DBUS_TYPE_INVALID) {

        if (!gjs_js_one_value_from_dbus(context, &struct_iter, &prop_value))
            goto out;

        if (!JS_DefineElement(context, obj,
                              index, prop_value,
                              NULL, NULL, JSPROP_ENUMERATE))
            goto out;

        dbus_message_iter_next(&struct_iter);
        index++;
    }
    *value_p = OBJECT_TO_JSVAL(obj);
    retval = JS_TRUE;
 out:
    JS_RemoveValueRoot(context, &prop_value);
    return retval;
}

static JSBool
_gjs_js_one_value_from_dbus_array_other(JSContext       *context,
                                        DBusMessageIter *iter,
                                        jsval           *value_p)
{
    JSObject *obj;
    jsval prop_value;
    DBusMessageIter array_iter;
    int index;
    JSBool retval = JS_FALSE;

    obj = JS_NewArrayObject(context, 0, NULL);
    if (obj == NULL)
        return JS_FALSE;

    prop_value = JSVAL_VOID;

    JS_AddObjectRoot(context, &obj);
    JS_AddValueRoot(context, &prop_value);
    dbus_message_iter_recurse(iter, &array_iter);
    index = 0;
    while (dbus_message_iter_get_arg_type(&array_iter) != DBUS_TYPE_INVALID) {
        if (!gjs_js_one_value_from_dbus(context, &array_iter, &prop_value))
            goto out;

        if (!JS_DefineElement(context, obj,
                              index, prop_value,
                              NULL, NULL, JSPROP_ENUMERATE))
            goto out;

        dbus_message_iter_next(&array_iter);
        index++;
    }

    *value_p = OBJECT_TO_JSVAL(obj);
    retval = JS_TRUE;
 out:
    JS_RemoveObjectRoot(context, &obj);
    JS_RemoveValueRoot(context, &prop_value);
    return retval;
}

JSBool
gjs_js_one_value_from_dbus(JSContext       *context,
                           DBusMessageIter *iter,
                           jsval           *value_p)
{
    int arg_type;

    *value_p = JSVAL_VOID;

    arg_type = dbus_message_iter_get_arg_type(iter);

    gjs_debug_dbus_marshal("Converting dbus type '%c' to jsval",
                           arg_type != DBUS_TYPE_INVALID ? arg_type : '0');

    switch (arg_type) {
    case DBUS_TYPE_STRUCT:
        return _gjs_js_one_value_from_dbus_struct(context, iter, value_p);
    case DBUS_TYPE_ARRAY:
        {
            int elem_type = dbus_message_iter_get_element_type(iter);

            if (elem_type == DBUS_TYPE_DICT_ENTRY) {
                return _gjs_js_one_value_from_dbus_array_dict_entry(context, iter, value_p);
            } else if (elem_type == DBUS_TYPE_BYTE) {
                return _gjs_js_one_value_from_dbus_array_byte(context, iter, value_p);
            } else {
                return _gjs_js_one_value_from_dbus_array_other(context, iter, value_p);
            }
        }
        break;
    case DBUS_TYPE_BOOLEAN:
        {
            dbus_bool_t v_BOOLEAN;
            dbus_message_iter_get_basic(iter, &v_BOOLEAN);
            *value_p = BOOLEAN_TO_JSVAL(v_BOOLEAN);
            return JS_TRUE;
        }
        break;
    case DBUS_TYPE_BYTE:
        {
            unsigned char v_BYTE;
            dbus_message_iter_get_basic(iter, &v_BYTE);
            return JS_NewNumberValue(context, v_BYTE, value_p);
        }
        break;
    case DBUS_TYPE_INT16:
        {
            dbus_int16_t v_INT16;
            dbus_message_iter_get_basic(iter, &v_INT16);
            return JS_NewNumberValue(context, v_INT16, value_p);
        }
        break;
    case DBUS_TYPE_UINT16:
        {
            dbus_uint16_t v_UINT16;
            dbus_message_iter_get_basic(iter, &v_UINT16);
            return JS_NewNumberValue(context, v_UINT16, value_p);
        }
        break;
    case DBUS_TYPE_INT32:
        {
            dbus_int32_t v_INT32;
            dbus_message_iter_get_basic(iter, &v_INT32);
            return JS_NewNumberValue(context, v_INT32, value_p);
        }
        break;
    case DBUS_TYPE_UINT32:
        {
            dbus_uint32_t v_UINT32;
            dbus_message_iter_get_basic(iter, &v_UINT32);
            return JS_NewNumberValue(context, v_UINT32, value_p);
        }
        break;
    case DBUS_TYPE_INT64:
        {
            dbus_int64_t v_INT64;
            dbus_message_iter_get_basic(iter, &v_INT64);
            return JS_NewNumberValue(context, v_INT64, value_p);
        }
        break;
    case DBUS_TYPE_UINT64:
        {
            dbus_uint64_t v_UINT64;
            dbus_message_iter_get_basic(iter, &v_UINT64);
            return JS_NewNumberValue(context, v_UINT64, value_p);
        }
        break;
    case DBUS_TYPE_DOUBLE:
        {
            double v_DOUBLE;
            dbus_message_iter_get_basic(iter, &v_DOUBLE);
            return JS_NewNumberValue(context, v_DOUBLE, value_p);
        }
        break;
    case DBUS_TYPE_OBJECT_PATH:
    case DBUS_TYPE_STRING:
        {
            const char *v_STRING;

            dbus_message_iter_get_basic(iter, &v_STRING);

            return gjs_string_from_utf8(context, v_STRING, -1, value_p);
        }
        break;

    case DBUS_TYPE_VARIANT:
        {
            DBusMessageIter variant_iter;

            dbus_message_iter_recurse(iter, &variant_iter);

            return gjs_js_one_value_from_dbus(context, &variant_iter, value_p);
        }
        break;

    case DBUS_TYPE_INVALID:
        *value_p = JSVAL_VOID;
        return JS_TRUE;
        break;

    default:
        gjs_debug(GJS_DEBUG_DBUS, "Don't know how to convert dbus type %c to JavaScript",
                  arg_type);
        gjs_throw(context, "Don't know how to convert dbus type %c to JavaScript",
                     arg_type);
        return JS_FALSE;
    }
}

JSBool
gjs_js_values_from_dbus(JSContext          *context,
                        DBusMessageIter    *iter,
                        GjsRootedArray    **array_p)
{
    GjsRootedArray *array;
    jsval value;

    value = JSVAL_VOID;
    JS_AddValueRoot(context, &value);

    *array_p = NULL;

    array = gjs_rooted_array_new();

    if (dbus_message_iter_get_arg_type(iter) != DBUS_TYPE_INVALID) {
        do {
            if (!gjs_js_one_value_from_dbus(context, iter, &value)) {
                gjs_rooted_array_free(context, array, TRUE);
                JS_RemoveValueRoot(context, &value);
                return JS_FALSE; /* error message already set */
            }

            gjs_rooted_array_append(context, array, value);
        } while (dbus_message_iter_next(iter));
    }

    *array_p = array;

    JS_RemoveValueRoot(context, &value);

    return JS_TRUE;
}

static void
append_basic_maybe_in_variant(DBusMessageIter *iter,
                              int              dbus_type,
                              void            *value,
                              gboolean         wrap_in_variant)
{
    if (wrap_in_variant) {
        char buf[2];
        DBusMessageIter variant_iter;

        buf[0] = dbus_type;
        buf[1] = '\0';

        dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, buf, &variant_iter);
        dbus_message_iter_append_basic(&variant_iter, dbus_type, value);
        dbus_message_iter_close_container(iter, &variant_iter);
    } else {
        dbus_message_iter_append_basic(iter, dbus_type, value);
    }
}

static void
append_byte_array_maybe_in_variant(DBusMessageIter *iter,
                                   const char      *data,
                                   gsize            len,
                                   gboolean         wrap_in_variant)
{
    DBusMessageIter array_iter;
    DBusMessageIter variant_iter;

    if (wrap_in_variant) {
        dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, "ay",
                                         &variant_iter);
    }

    dbus_message_iter_open_container(wrap_in_variant ? &variant_iter : iter,
                                     DBUS_TYPE_ARRAY, "y", &array_iter);

    dbus_message_iter_append_fixed_array(&array_iter, DBUS_TYPE_BYTE,
                                         &data, len);

    dbus_message_iter_close_container(wrap_in_variant ? &variant_iter : iter,
                                      &array_iter);

    if (wrap_in_variant) {
        dbus_message_iter_close_container(iter, &variant_iter);
    }
}

static JSBool
append_string(JSContext       *context,
              DBusMessageIter *iter,
              const char      *forced_signature,
              const char      *s,
              gsize            len)
{
    int forced_type;

    if (forced_signature == NULL ||
        *forced_signature == DBUS_TYPE_INVALID)
        forced_type = DBUS_TYPE_STRING;
    else
        forced_type = *forced_signature;

    switch (forced_type) {
    case DBUS_TYPE_STRING:
    case DBUS_TYPE_OBJECT_PATH:
    case DBUS_TYPE_SIGNATURE:
        append_basic_maybe_in_variant(iter, forced_type, &s, FALSE);
        break;
    case DBUS_TYPE_VARIANT:
        append_basic_maybe_in_variant(iter, DBUS_TYPE_STRING, &s, TRUE);
        break;
    case DBUS_TYPE_ARRAY:
        g_assert(forced_signature != NULL);
        g_assert(forced_signature[0] == DBUS_TYPE_ARRAY);
        if (forced_signature[1] == DBUS_TYPE_BYTE) {
            append_byte_array_maybe_in_variant(iter,
                                               s, len,
                                               FALSE);
        } else {
            gjs_throw(context,
                      "JavaScript string can't be converted to dbus array with elements of type '%c'",
                      forced_signature[1]);
            return JS_FALSE;
        }
        break;
    default:
        gjs_throw(context,
                  "JavaScript string can't be converted to dbus type '%c'",
                  forced_type);
        return JS_FALSE;
    }

    return JS_TRUE;
}

static JSBool
append_int32(JSContext       *context,
             DBusMessageIter *iter,
             int              forced_type,
             dbus_int32_t     v_INT32)
{
    if (forced_type == DBUS_TYPE_INVALID)
        forced_type = DBUS_TYPE_INT32;

    switch (forced_type) {
    case DBUS_TYPE_INT32:
        append_basic_maybe_in_variant(iter, forced_type, &v_INT32, FALSE);
        break;
    case DBUS_TYPE_VARIANT:
        append_basic_maybe_in_variant(iter, DBUS_TYPE_INT32, &v_INT32, TRUE);
        break;
    case DBUS_TYPE_UINT32:
        {
            dbus_uint32_t v_UINT32 = v_INT32;
            append_basic_maybe_in_variant(iter, forced_type, &v_UINT32, FALSE);
        }
        break;
    case DBUS_TYPE_DOUBLE:
        {
            double v_DOUBLE = v_INT32;
            append_basic_maybe_in_variant(iter, forced_type, &v_DOUBLE, FALSE);
        }
        break;
    case DBUS_TYPE_BYTE:
        {
            unsigned char v_BYTE = v_INT32;
            append_basic_maybe_in_variant(iter, forced_type, &v_BYTE, FALSE);
        }
        break;
    // All JavaScript integers can be converted to DBus INT64/UINT64
    // (just not the other way 'round)
    case DBUS_TYPE_INT64:
        {
            dbus_int64_t v_INT64 = v_INT32;
            append_basic_maybe_in_variant(iter, forced_type, &v_INT64, FALSE);
        }
        break;
    case DBUS_TYPE_UINT64:
        {
            dbus_uint64_t v_UINT64 = v_INT32;
            append_basic_maybe_in_variant(iter, forced_type, &v_UINT64, FALSE);
        }
        break;
    default:
        gjs_throw(context,
                  "JavaScript Integer can't be converted to dbus type '%c'",
                  forced_type);
        return JS_FALSE;
    }

    return JS_TRUE;
}

static JSBool
append_double(JSContext       *context,
              DBusMessageIter *iter,
              int              forced_type,
              double           v_DOUBLE)
{
    if (forced_type == DBUS_TYPE_INVALID)
        forced_type = DBUS_TYPE_DOUBLE;

    switch (forced_type) {
    case DBUS_TYPE_DOUBLE:
        append_basic_maybe_in_variant(iter, forced_type, &v_DOUBLE, FALSE);
        break;
    case DBUS_TYPE_VARIANT:
        append_basic_maybe_in_variant(iter, DBUS_TYPE_DOUBLE, &v_DOUBLE, TRUE);
        break;
    case DBUS_TYPE_INT32:
        {
            dbus_int32_t v_INT32 = v_DOUBLE;
            append_basic_maybe_in_variant(iter, forced_type, &v_INT32, FALSE);
        }
        break;
    case DBUS_TYPE_UINT32:
        {
            dbus_uint32_t v_UINT32 = v_DOUBLE;
            append_basic_maybe_in_variant(iter, forced_type, &v_UINT32, FALSE);
        }
        break;
    // All JavaScript integers can be converted to DBus INT64/UINT64
    // (just not the other way 'round)
    case DBUS_TYPE_INT64:
        {
            dbus_int64_t v_INT64 = v_DOUBLE;
            append_basic_maybe_in_variant(iter, forced_type, &v_INT64, FALSE);
        }
        break;
    case DBUS_TYPE_UINT64:
        {
            dbus_uint64_t v_UINT64 = v_DOUBLE;
            append_basic_maybe_in_variant(iter, forced_type, &v_UINT64, FALSE);
        }
        break;
    default:
        gjs_throw(context,
                  "JavaScript Number can't be converted to dbus type '%c'",
                  forced_type);
        return JS_FALSE;
    }

    return JS_TRUE;
}

static JSBool
append_boolean(JSContext       *context,
               DBusMessageIter *iter,
               int              forced_type,
               dbus_bool_t      v_BOOLEAN)
{
    if (forced_type == DBUS_TYPE_INVALID)
        forced_type = DBUS_TYPE_BOOLEAN;

    switch (forced_type) {
    case DBUS_TYPE_BOOLEAN:
        append_basic_maybe_in_variant(iter, forced_type, &v_BOOLEAN, FALSE);
        break;
    case DBUS_TYPE_VARIANT:
        append_basic_maybe_in_variant(iter, DBUS_TYPE_BOOLEAN, &v_BOOLEAN, TRUE);
        break;
    default:
        gjs_throw(context,
                  "JavaScript Boolean can't be converted to dbus type '%c'",
                  forced_type);
        return JS_FALSE;
    }

    return JS_TRUE;
}

static JSBool
append_array(JSContext         *context,
             DBusMessageIter   *iter,
             DBusSignatureIter *sig_iter,
             JSObject          *array,
             int                length)
{
    DBusSignatureIter element_sig_iter;
    int forced_type;
    jsval element;
    int i;
    char *sig;

    forced_type = dbus_signature_iter_get_current_type(sig_iter);

    if (forced_type == DBUS_TYPE_VARIANT) {
        DBusMessageIter variant_iter;
        DBusSignatureIter variant_sig_iter;

        dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT,
                                         "av",
                                         &variant_iter);
        dbus_signature_iter_init(&variant_sig_iter, "av");
        if (!append_array(context, &variant_iter,
                          &variant_sig_iter,
                          array, length))
            return JS_FALSE;
        dbus_message_iter_close_container(iter, &variant_iter);

        return JS_TRUE;
    } else if (forced_type == DBUS_TYPE_STRUCT) {
        DBusMessageIter struct_iter;
        dbus_bool_t have_next;

        g_assert(dbus_signature_iter_get_current_type(sig_iter) == DBUS_TYPE_STRUCT);
        dbus_signature_iter_recurse(sig_iter, &element_sig_iter);

        dbus_message_iter_open_container(iter, DBUS_TYPE_STRUCT, NULL, &struct_iter);

        have_next = dbus_signature_iter_get_current_type(&element_sig_iter) != DBUS_TYPE_INVALID;

        for (i = 0; i < length; i++) {
            element = JSVAL_VOID;

            if (!have_next) {
                gjs_throw(context, "Insufficient elements for structure in JS Array");
                return JS_FALSE;
            }

            if (!JS_GetElement(context, array, i, &element)) {
                gjs_throw(context, "Failed to get element in JS Array");
                return JS_FALSE;
            }

            gjs_debug_dbus_marshal(" Adding struct element %u", i);

            if (!gjs_js_one_value_to_dbus(context, element, &struct_iter,
                                          &element_sig_iter))
                return JS_FALSE;

            have_next = dbus_signature_iter_next (&element_sig_iter);
        }

        if (have_next) {
            gjs_throw(context, "Too many elements for structure in JS Array");
            return JS_FALSE;
        }

        dbus_message_iter_close_container(iter, &struct_iter);

        return JS_TRUE;
    } else if (forced_type == DBUS_TYPE_ARRAY) {
        DBusMessageIter array_iter;

        g_assert(dbus_signature_iter_get_current_type(sig_iter) == DBUS_TYPE_ARRAY);
        dbus_signature_iter_recurse(sig_iter, &element_sig_iter);

        sig = dbus_signature_iter_get_signature(&element_sig_iter);
        dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY, sig, &array_iter);
        dbus_free(sig);

        for (i = 0; i < length; i++) {
            element = JSVAL_VOID;

            if (!JS_GetElement(context, array, i, &element)) {
                gjs_throw(context, "Failed to get element in JS Array");
                return JS_FALSE;
            }

            gjs_debug_dbus_marshal(" Adding array element %u", i);

            if (!gjs_js_one_value_to_dbus(context, element, &array_iter,
                                          &element_sig_iter))
                return JS_FALSE;
        }

        dbus_message_iter_close_container(iter, &array_iter);

        return JS_TRUE;
    } else {
      gjs_throw(context,
                "JavaScript Array can't be converted to dbus type %c",
                forced_type);
      return JS_FALSE;
    }
}

static JSBool
append_dict(JSContext         *context,
            DBusMessageIter   *iter,
            DBusSignatureIter *sig_iter,
            JSObject          *props)
{
    DBusSignatureIter element_sig_iter;
    int forced_type;
    DBusMessageIter variant_iter;
    JSObject *props_iter;
    jsid prop_id;
    DBusMessageIter dict_iter;
    DBusSignatureIter dict_value_sig_iter;
    char *sig;
    jsval prop_signatures;

    forced_type = dbus_signature_iter_get_current_type(sig_iter);

    if (forced_type == DBUS_TYPE_VARIANT) {
        DBusSignatureIter variant_sig_iter;

        dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT,
                                         "a{sv}",
                                         &variant_iter);
        dbus_signature_iter_init(&variant_sig_iter, "a{sv}");
        if (!append_dict(context, &variant_iter,
                         &variant_sig_iter,
                         props))
            return JS_FALSE;
        dbus_message_iter_close_container(iter, &variant_iter);

        return JS_TRUE;
    } else if (forced_type != DBUS_TYPE_ARRAY) {
        gjs_throw(context,
                  "JavaScript Object can't be converted to dbus type %c",
                  forced_type);
        return JS_FALSE;
    }

    g_assert(dbus_signature_iter_get_current_type(sig_iter) == DBUS_TYPE_ARRAY);
    dbus_signature_iter_recurse(sig_iter, &element_sig_iter);

    if (dbus_signature_iter_get_current_type(&element_sig_iter) != DBUS_TYPE_DICT_ENTRY) {
        gjs_throw(context,
                  "Objects must be marshaled as array of dict entry not of %c",
                  dbus_signature_iter_get_current_type(&element_sig_iter));
        return JS_FALSE;
    }

    /* dbus itself enforces that dict keys are strings */

    g_assert(dbus_signature_iter_get_current_type(&element_sig_iter) ==
             DBUS_TYPE_DICT_ENTRY);
    dbus_signature_iter_recurse(&element_sig_iter, &dict_value_sig_iter);
    /* check it points to key type first */
    g_assert(dbus_signature_iter_get_current_type(&dict_value_sig_iter) ==
             DBUS_TYPE_STRING);
    /* move to value type */
    dbus_signature_iter_next(&dict_value_sig_iter);

    sig = dbus_signature_iter_get_signature(&element_sig_iter);
    dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY, sig, &dict_iter);
    dbus_free(sig);

    /* If a dictionary contains another dictionary at key
     * _dbus_signatures, the sub-dictionary can provide the signature
     * of each value in the outer dictionary. This allows forcing
     * integers to unsigned or whatever.
     *
     * _dbus_signatures has a weird name to avoid conflicting with
     * real properties. Matches _dbus_sender which is used elsewhere.
     *
     * We don't bother rooting the signature object or the stuff in it
     * because we assume the outer dictionary is rooted so the stuff
     * in it is also.
     */
    prop_signatures = JSVAL_VOID;
    gjs_object_get_property(context, props,
                            "_dbus_signatures",
                            &prop_signatures);

    if (!JSVAL_IS_VOID(prop_signatures) &&
        !JSVAL_IS_OBJECT(prop_signatures)) {
        gjs_throw(context,
                  "_dbus_signatures prop must be an object");
        return JS_FALSE;
    }

    if (!JSVAL_IS_VOID(prop_signatures) &&
        dbus_signature_iter_get_current_type(&dict_value_sig_iter) !=
        DBUS_TYPE_VARIANT) {
        gjs_throw(context,
                  "Specifying _dbus_signatures for a dictionary with non-variant values is useless");
        return JS_FALSE;
    }

    props_iter = JS_NewPropertyIterator(context, props);
    if (props_iter == NULL) {
        gjs_throw(context, "Failed to create property iterator for object props");
        return JS_FALSE;
    }

    prop_id = JSID_VOID;
    if (!JS_NextProperty(context, props_iter, &prop_id))
        return JS_FALSE;

    while (!JSID_IS_VOID(prop_id)) {
        jsval nameval;
        char *name;
        jsval propval;
        DBusMessageIter entry_iter;
        char *value_signature;

        if (!JS_IdToValue(context, prop_id, &nameval))
            return JS_FALSE;

        if (!gjs_string_to_utf8(context, nameval, &name))
            return JS_FALSE;

        if (strcmp(name, "_dbus_signatures") == 0) {
            /* skip the magic "_dbus_signatures" field */
            goto next;
        }

        /* see if this prop has a forced signature */
        value_signature = NULL;
        if (!JSVAL_IS_VOID(prop_signatures)) {
            jsval signature_value;
            signature_value = JSVAL_VOID;
            gjs_object_get_property(context,
                                    JSVAL_TO_OBJECT(prop_signatures),
                                    name, &signature_value);
            if (!JSVAL_IS_VOID(signature_value)) {
                value_signature = gjs_string_get_ascii(context,
                                                               signature_value);
                if (value_signature == NULL) {
                    return JS_FALSE;
                }
            }
        }

        if (!gjs_object_require_property(context, props, "DBus append_dict", name, &propval)) {
            g_free(value_signature);
            return JS_FALSE;
        }

        gjs_debug_dbus_marshal(" Adding property %s",
                               name);

        /* gjs_js_one_value_to_dbus() would check this also, but would not
         * print the property name, which is often useful
         */
        if (JSVAL_IS_NULL(propval)) {
            gjs_throw(context, "Property '%s' has a null value, can't send over dbus",
                      name);
            g_free(value_signature);
            return JS_FALSE;
        }

        dbus_message_iter_open_container(&dict_iter, DBUS_TYPE_DICT_ENTRY,
                                         NULL, &entry_iter);

        dbus_message_iter_append_basic(&entry_iter, DBUS_TYPE_STRING, &name);
        g_free(name);

        if (value_signature != NULL) {
            DBusSignatureIter forced_signature_iter;
            DBusMessageIter variant_iter;

            g_assert(dbus_signature_iter_get_current_type(&dict_value_sig_iter) ==
                     DBUS_TYPE_VARIANT);

            dbus_message_iter_open_container(&entry_iter,
                                             DBUS_TYPE_VARIANT,
                                             value_signature,
                                             &variant_iter);

            dbus_signature_iter_init(&forced_signature_iter, value_signature);

            if (!gjs_js_one_value_to_dbus(context, propval, &variant_iter,
                                          &forced_signature_iter))
                return JS_FALSE;

            dbus_message_iter_close_container(&entry_iter, &variant_iter);
            g_free(value_signature);
        } else {
            if (!gjs_js_one_value_to_dbus(context, propval, &entry_iter,
                                          &dict_value_sig_iter))
                return JS_FALSE;
        }

        dbus_message_iter_close_container(&dict_iter, &entry_iter);

    next:
        prop_id = JSID_VOID;
        if (!JS_NextProperty(context, props_iter, &prop_id))
            return JS_FALSE;
    }

    dbus_message_iter_close_container(iter, &dict_iter);

    return JS_TRUE;
}

JSBool
gjs_js_one_value_to_dbus(JSContext         *context,
                         jsval              value,
                         DBusMessageIter   *iter,
                         DBusSignatureIter *sig_iter)
{
    int forced_type;

    forced_type = dbus_signature_iter_get_current_type(sig_iter);

    gjs_debug_dbus_marshal("Converting dbus type '%c' from jsval",
                           forced_type != DBUS_TYPE_INVALID ? forced_type : '0');

    /* Don't write anything on the bus if the signature is empty */
    if (forced_type == DBUS_TYPE_INVALID)
        return JS_TRUE;

    if (JSVAL_IS_NULL(value)) {
        gjs_debug(GJS_DEBUG_DBUS, "Can't send null values over dbus");
        gjs_throw(context, "Can't send null values over dbus");
        return JS_FALSE;
    } else if (JSVAL_IS_STRING(value)) {
        char *data;
        gsize len;
        char buf[3] = { '\0', '\0', '\0' };
        if (forced_type == DBUS_TYPE_ARRAY) {
            buf[0] = DBUS_TYPE_ARRAY;
            buf[1] = dbus_signature_iter_get_element_type(sig_iter);
        } else {
            buf[0] = forced_type;
        }

        data = NULL;
        len = 0;
        if (buf[1] == DBUS_TYPE_BYTE) {
            if (!gjs_string_get_binary_data(context, value,
                                            &data, &len))
                return JS_FALSE;
        } else {
            if (!gjs_string_to_utf8(context, value, &data))
                return JS_FALSE;
            len = strlen(data);
        }

        if (!append_string(context,
                           iter, buf,
                           data, len)) {
            g_free(data);
            return JS_FALSE;
        }

        g_free(data);
    } else if (JSVAL_IS_INT(value)) {
        dbus_int32_t v_INT32;
        if (!JS_ValueToInt32(context, value, &v_INT32))
            return JS_FALSE;

        if (!append_int32(context,
                          iter, forced_type,
                          v_INT32))
            return JS_FALSE;
    } else if (JSVAL_IS_DOUBLE(value)) {
        double v_DOUBLE;
        if (!JS_ValueToNumber(context, value, &v_DOUBLE))
            return JS_FALSE;

        if (!append_double(context,
                           iter, forced_type,
                           v_DOUBLE))
            return JS_FALSE;
    } else if (JSVAL_IS_BOOLEAN(value)) {
        JSBool v_JS_BOOLEAN;
        dbus_bool_t v_BOOLEAN;
        if (!JS_ValueToBoolean(context, value, &v_JS_BOOLEAN))
            return JS_FALSE;
        v_BOOLEAN = v_JS_BOOLEAN != JS_FALSE;

        if (!append_boolean(context,
                            iter, forced_type,
                            v_BOOLEAN))
            return JS_FALSE;
    } else if (JSVAL_IS_OBJECT(value)) {
        JSObject *obj;
        jsval lengthval;

        obj = JSVAL_TO_OBJECT(value);

        /* see if there's a length property */
        gjs_object_get_property(context, obj, "length", &lengthval);

        if (JSVAL_IS_INT(lengthval)) {
            guint length;

            length = JSVAL_TO_INT(lengthval);

            gjs_debug_dbus_marshal("Looks like an array length %u", length);
            if (!append_array(context, iter, sig_iter, obj, length))
                return JS_FALSE;
        } else {
            gjs_debug_dbus_marshal("Looks like a dictionary");
            if (!append_dict(context, iter, sig_iter, obj))
                return JS_FALSE;
        }
    } else if (JSVAL_IS_VOID(value)) {
        gjs_debug(GJS_DEBUG_DBUS, "Can't send void (undefined) values over dbus");
        gjs_throw(context, "Can't send void (undefined) values over dbus");
        return JS_FALSE;
    } else {
        gjs_debug(GJS_DEBUG_DBUS, "Don't know how to convert this jsval to dbus type");
        gjs_throw(context, "Don't know how to convert this jsval to dbus type");
        return JS_FALSE;
    }

    return JS_TRUE;
}

JSBool
gjs_js_values_to_dbus(JSContext         *context,
                      int                index,
                      jsval              values,
                      DBusMessageIter   *iter,
                      DBusSignatureIter *sig_iter)
{
    jsval value;
    jsuint length;

    if (!JS_GetArrayLength(context, JSVAL_TO_OBJECT(values), &length)) {
        gjs_throw(context, "Error retrieving length property of args array");
        return JS_FALSE;
    }

    if (index > (int)length) {
        gjs_throw(context, "Index %d is bigger than array length %d", index, length);
        return JS_FALSE;
    }

    if (index == (int)length)
        return JS_TRUE;

    if (!JS_GetElement(context, JSVAL_TO_OBJECT(values),
                       index, &value)) {
        gjs_throw(context, "Error accessing element %d of args array", index);
        return JS_FALSE;
    }

    if (!gjs_js_one_value_to_dbus(context, value, iter, sig_iter)) {
        gjs_throw(context, "Error marshalling js value to dbus");
        return JS_FALSE;
    }

    if (dbus_signature_iter_next(sig_iter)) {
        return gjs_js_values_to_dbus(context, index + 1, values, iter, sig_iter);
    }

    return JS_TRUE;
}

