/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * Copyright © 2016 Endless Mobile, Inc.
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
 * Authored by: Philip Chimento <philip@endlessm.com>
 */

#include <type_traits>

#include <glib.h>

#include "jsapi-util.h"
#include "jsapi-wrapper.h"

GJS_ALWAYS_INLINE GJS_USE static inline bool check_nullable(
    const char*& fchar, const char*& fmt_string) {
    if (*fchar != '?')
        return false;

    fchar++;
    fmt_string++;
    g_assert(((void) "Invalid format string, parameter required after '?'",
              *fchar != '\0'));
    return true;
}

/* This preserves the previous behaviour of gjs_parse_args(), but maybe we want
 * to use JS::ToBoolean instead? */
GJS_ALWAYS_INLINE
static inline void
assign(JSContext      *cx,
       char            c,
       bool            nullable,
       JS::HandleValue value,
       bool           *ref)
{
    if (c != 'b')
        throw g_strdup_printf("Wrong type for %c, got bool*", c);
    if (!value.isBoolean())
        throw g_strdup("Not a boolean");
    if (nullable)
        throw g_strdup("Invalid format string combination ?b");
    *ref = value.toBoolean();
}

/* This preserves the previous behaviour of gjs_parse_args(), but maybe we want
 * to box primitive types instead of throwing? */
GJS_ALWAYS_INLINE
static inline void
assign(JSContext              *cx,
       char                    c,
       bool                    nullable,
       JS::HandleValue         value,
       JS::MutableHandleObject ref)
{
    if (c != 'o')
        throw g_strdup_printf("Wrong type for %c, got JS::MutableHandleObject", c);
    if (nullable && value.isNull()) {
        ref.set(NULL);
        return;
    }
    if (!value.isObject())
        throw g_strdup("Not an object");
    ref.set(&value.toObject());
}

GJS_ALWAYS_INLINE
static inline void assign(JSContext* cx, char c, bool nullable,
                          JS::HandleValue value, JS::UniqueChars* ref) {
    if (c != 's')
        throw g_strdup_printf("Wrong type for %c, got JS::UniqueChars*", c);
    if (nullable && value.isNull()) {
        ref->reset();
        return;
    }
    if (!gjs_string_to_utf8(cx, value, ref))
        throw g_strdup("Couldn't convert to string");
}

GJS_ALWAYS_INLINE
static inline void
assign(JSContext      *cx,
       char            c,
       bool            nullable,
       JS::HandleValue value,
       GjsAutoChar    *ref)
{
    if (c != 'F')
        throw g_strdup_printf("Wrong type for %c, got GjsAutoChar*", c);
    if (nullable && value.isNull()) {
        ref->release();
        return;
    }
    if (!gjs_string_to_filename(cx, value, ref))
        throw g_strdup("Couldn't convert to filename");
}

GJS_ALWAYS_INLINE
static inline void
assign(JSContext      *cx,
       char            c,
       bool            nullable,
       JS::HandleValue value,
       int32_t        *ref)
{
    if (c != 'i')
        throw g_strdup_printf("Wrong type for %c, got int32_t*", c);
    if (nullable)
        throw g_strdup("Invalid format string combination ?i");
    if (!JS::ToInt32(cx, value, ref))
        throw g_strdup("Couldn't convert to integer");
}

GJS_ALWAYS_INLINE
static inline void
assign(JSContext      *cx,
       char            c,
       bool            nullable,
       JS::HandleValue value,
       uint32_t       *ref)
{
    double num;

    if (c != 'u')
        throw g_strdup_printf("Wrong type for %c, got uint32_t*", c);
    if (nullable)
        throw g_strdup("Invalid format string combination ?u");
    if (!value.isNumber() || !JS::ToNumber(cx, value, &num))
        throw g_strdup("Couldn't convert to unsigned integer");
    if (num > G_MAXUINT32 || num < 0)
        throw g_strdup_printf("Value %f is out of range", num);
    *ref = num;
}

GJS_ALWAYS_INLINE
static inline void
assign(JSContext      *cx,
       char            c,
       bool            nullable,
       JS::HandleValue value,
       int64_t        *ref)
{
    if (c != 't')
        throw g_strdup_printf("Wrong type for %c, got int64_t*", c);
    if (nullable)
        throw g_strdup("Invalid format string combination ?t");
    if (!JS::ToInt64(cx, value, ref))
        throw g_strdup("Couldn't convert to 64-bit integer");
}

GJS_ALWAYS_INLINE
static inline void
assign(JSContext      *cx,
       char            c,
       bool            nullable,
       JS::HandleValue value,
       double         *ref)
{
    if (c != 'f')
        throw g_strdup_printf("Wrong type for %c, got double*", c);
    if (nullable)
        throw g_strdup("Invalid format string combination ?f");
    if (!JS::ToNumber(cx, value, ref))
        throw g_strdup("Couldn't convert to double");
}

/* Special case: treat pointer-to-enum as pointer-to-int, but use enable_if to
 * prevent instantiation for any other types besides pointer-to-enum */
template<typename T,
         typename std::enable_if<std::is_enum<T>::value, int>::type = 0>
GJS_ALWAYS_INLINE
static inline void
assign(JSContext      *cx,
       char            c,
       bool            nullable,
       JS::HandleValue value,
       T              *ref)
{
    /* Sadly, we cannot use std::underlying_type<T> here; the underlying type of
     * an enum is implementation-defined, so it would not be clear what letter
     * to use in the format string. For the same reason, we can only support
     * enum types that are the same width as int.
     * Additionally, it would be nice to be able to check whether the resulting
     * value was in range for the enum, but that is not possible (yet?) */
    static_assert(sizeof(T) == sizeof(int),
                  "Short or wide enum types not supported");
    assign(cx, c, nullable, value, (int *)ref);
}

/* Force JS::RootedObject * to be converted to JS::MutableHandleObject,
 * see overload in jsapi-util-args.cpp */
template<typename T,
         typename std::enable_if<!std::is_same<T, JS::RootedObject *>::value, int>::type = 0>
static inline void
free_if_necessary(T param_ref) {}

GJS_ALWAYS_INLINE
static inline void
free_if_necessary(JS::MutableHandleObject param_ref)
{
    /* This is not exactly right, since before we consumed a JS::ObjectValue
     * there may have been something different inside the handle. But it has
     * already been clobbered at this point anyhow */
    param_ref.set(NULL);
}

template <typename T>
GJS_JSAPI_RETURN_CONVENTION static bool parse_call_args_helper(
    JSContext* cx, const char* function_name, const JS::CallArgs& args,
    bool ignore_trailing_args, const char*& fmt_required,
    const char*& fmt_optional, unsigned param_ix, const char* param_name,
    T param_ref) {
    bool nullable = false;
    const char *fchar = fmt_required;

    g_return_val_if_fail (param_name != NULL, false);

    if (*fchar != '\0') {
        nullable = check_nullable(fchar, fmt_required);
        fmt_required++;
    } else {
        /* No more args passed in JS, only optional formats left */
        if (args.length() <= param_ix)
            return true;

        fchar = fmt_optional;
        g_assert(((void) "Wrong number of parameters passed to gjs_parse_call_args()",
                  *fchar != '\0'));
        nullable = check_nullable(fchar, fmt_optional);
        fmt_optional++;
    }

    try {
        assign(cx, *fchar, nullable, args[param_ix], param_ref);
    } catch (char *message) {
        /* Our error messages are going to be more useful than whatever was
         * thrown by the various conversion functions */
        JS_ClearPendingException(cx);
        gjs_throw(cx, "Error invoking %s, at argument %d (%s): %s",
                  function_name, param_ix, param_name, message);
        g_free(message);
        return false;
    }

    return true;
}

template <typename T, typename... Args>
GJS_JSAPI_RETURN_CONVENTION static bool parse_call_args_helper(
    JSContext* cx, const char* function_name, const JS::CallArgs& args,
    bool ignore_trailing_args, const char*& fmt_required,
    const char*& fmt_optional, unsigned param_ix, const char* param_name,
    T param_ref, Args... params) {
    bool retval;

    if (!parse_call_args_helper(cx, function_name, args, ignore_trailing_args,
                                fmt_required, fmt_optional, param_ix,
                                param_name, param_ref))
        return false;

    retval = parse_call_args_helper(cx, function_name, args,
                                    ignore_trailing_args,
                                    fmt_required, fmt_optional, ++param_ix,
                                    params...);

    /* We still own the strings in the error case, free any we converted */
    if (!retval)
        free_if_necessary(param_ref);
    return retval;
}

/* Empty-args version of the template */
G_GNUC_UNUSED GJS_JSAPI_RETURN_CONVENTION static bool gjs_parse_call_args(
    JSContext* cx, const char* function_name, const JS::CallArgs& args,
    const char* format) {
    bool ignore_trailing_args = false;

    if (*format == '!') {
        ignore_trailing_args = true;
        format++;
    }

    g_assert(((void) "Wrong number of parameters passed to gjs_parse_call_args()",
              *format == '\0'));

    if (!ignore_trailing_args && args.length() > 0) {
        JSAutoRequest ar(cx);
        gjs_throw(cx, "Error invoking %s: Expected 0 arguments, got %d",
                  function_name, args.length());
        return false;
    }

    return true;
}

/**
 * gjs_parse_call_args:
 * @context:
 * @function_name: The name of the function being called
 * @format: Printf-like format specifier containing the expected arguments
 * @args: #JS::CallArgs from #JSNative function
 * @params: for each character in @format, a pair of const char * which is the
 * name of the argument, and a location to store the value. The type of
 * location argument depends on the format character, as described below.
 *
 * This function is inspired by Python's PyArg_ParseTuple for those
 * familiar with it.  It takes a format specifier which gives the
 * types of the expected arguments, and a list of argument names and
 * value location pairs.  The currently accepted format specifiers are:
 *
 * b: A boolean (pass a bool *)
 * s: A string, converted into UTF-8 (pass a JS::UniqueChars*)
 * F: A string, converted into "filename encoding" (i.e. active locale) (pass
 *   a GjsAutoChar *)
 * i: A number, will be converted to a 32-bit int (pass an int32_t * or a
 *   pointer to an enum type)
 * u: A number, converted into a 32-bit unsigned int (pass a uint32_t *)
 * t: A 64-bit number, converted into a 64-bit int (pass an int64_t *)
 * f: A number, will be converted into a double (pass a double *)
 * o: A JavaScript object (pass a JS::MutableHandleObject)
 *
 * If the first character in the format string is a '!', then JS is allowed
 * to pass extra arguments that are ignored, to the function.
 *
 * The '|' character introduces optional arguments.  All format specifiers
 * after a '|' when not specified, do not cause any changes in the C
 * value location.
 *
 * A prefix character '?' in front of 's', 'F', or 'o' means that the next value
 * may be null. For 's' or 'F' a null pointer is returned, for 'o' the handle is
 * set to null.
 */
template <typename... Args>
GJS_JSAPI_RETURN_CONVENTION static bool gjs_parse_call_args(
    JSContext* cx, const char* function_name, const JS::CallArgs& args,
    const char* format, Args... params) {
    const char *fmt_iter, *fmt_required, *fmt_optional;
    unsigned n_required = 0, n_total = 0;
    bool optional_args = false, ignore_trailing_args = false, retval;

    if (*format == '!') {
        ignore_trailing_args = true;
        format++;
    }

    for (fmt_iter = format; *fmt_iter; fmt_iter++) {
        switch (*fmt_iter) {
        case '|':
            n_required = n_total;
            optional_args = true;
            continue;
        case '?':
            continue;
        default:
            n_total++;
        }
    }

    if (!optional_args)
        n_required = n_total;

    g_assert(((void) "Wrong number of parameters passed to gjs_parse_call_args()",
              sizeof...(Args) / 2 == n_total));

    JSAutoRequest ar(cx);

    /* COMPAT: In future, use args.requireAtLeast()
     * https://bugzilla.mozilla.org/show_bug.cgi?id=1334338 */
    if (args.length() < n_required ||
        (args.length() > n_total && !ignore_trailing_args)) {
        if (n_required == n_total) {
            gjs_throw(cx, "Error invoking %s: Expected %d arguments, got %d",
                      function_name, n_required, args.length());
        } else {
            gjs_throw(cx,
                      "Error invoking %s: Expected minimum %d arguments (and %d optional), got %d",
                      function_name, n_required, n_total - n_required,
                      args.length());
        }
        return false;
    }

    GjsAutoStrv parts = g_strsplit(format, "|", 2);
    fmt_required = parts[0];
    fmt_optional = parts[1];  /* may be NULL */

    retval = parse_call_args_helper(cx, function_name, args,
                                    ignore_trailing_args, fmt_required,
                                    fmt_optional, 0, params...);

    return retval;
}
