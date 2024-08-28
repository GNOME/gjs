/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2016 Endless Mobile, Inc.
// SPDX-FileContributor: Authored by: Philip Chimento <philip@endlessm.com>

#ifndef GJS_JSAPI_UTIL_ARGS_H_
#define GJS_JSAPI_UTIL_ARGS_H_

#include <config.h>

#include <stdint.h>

#include <type_traits>  // for enable_if, is_enum, is_same
#include <utility>      // for move

#include <glib.h>

#include <js/CallArgs.h>
#include <js/Conversions.h>
#include <js/Exception.h>
#include <js/Result.h>
#include <js/RootingAPI.h>
#include <js/TypeDecls.h>
#include <js/Utility.h>  // for UniqueChars
#include <js/Value.h>
#include <mozilla/Result.h>  // for GenericErrorResult
#include <mozilla/ResultVariant.h>  // IWYU pragma: keep

#include "gjs/auto.h"
#include "gjs/jsapi-util.h"
#include "gjs/macros.h"

namespace detail {

[[nodiscard]] GJS_ALWAYS_INLINE static inline bool check_nullable(
    const char*& fchar, const char*& fmt_string) {
    if (*fchar != '?')
        return false;

    fchar++;
    fmt_string++;
    g_assert(((void) "Invalid format string, parameter required after '?'",
              *fchar != '\0'));
    return true;
}

class ParseArgsErr {
    Gjs::AutoChar m_message;

 public:
    explicit ParseArgsErr(const char* literal_msg)
        : m_message(literal_msg, Gjs::TakeOwnership{}) {}
    template <typename F>
    ParseArgsErr(const char* format_string, F param)
        : m_message(g_strdup_printf(format_string, param)) {}

    const char* message() const { return m_message.get(); }
};

template <typename... Args>
inline constexpr auto Err(Args... args) {
    return mozilla::GenericErrorResult{
        ParseArgsErr{std::forward<Args>(args)...}};
}

using ParseArgsResult = JS::Result<JS::Ok, ParseArgsErr>;

/* This preserves the previous behaviour of gjs_parse_args(), but maybe we want
 * to use JS::ToBoolean instead? */
GJS_ALWAYS_INLINE
static inline ParseArgsResult assign(JSContext*, char c, bool nullable,
                                     JS::HandleValue value, bool* ref) {
    if (c != 'b')
        return Err("Wrong type for %c, got bool*", c);
    if (!value.isBoolean())
        return Err("Not a boolean");
    if (nullable)
        return Err("Invalid format string combination ?b");
    *ref = value.toBoolean();
    return JS::Ok();
}

GJS_ALWAYS_INLINE
static inline ParseArgsResult assign(JSContext*, char c, bool nullable,
                                     JS::HandleValue value,
                                     JS::MutableHandleObject ref) {
    if (c != 'o')
        return Err("Wrong type for %c, got JS::MutableHandleObject", c);
    if (nullable && value.isNull()) {
        ref.set(nullptr);
        return JS::Ok();
    }
    if (!value.isObject())
        return Err("Not an object");
    ref.set(&value.toObject());
    return JS::Ok();
}

GJS_ALWAYS_INLINE
static inline ParseArgsResult assign(JSContext* cx, char c, bool nullable,
                                     JS::HandleValue value,
                                     JS::UniqueChars* ref) {
    if (c != 's')
        return Err("Wrong type for %c, got JS::UniqueChars*", c);
    if (nullable && value.isNull()) {
        ref->reset();
        return JS::Ok();
    }
    JS::UniqueChars tmp = gjs_string_to_utf8(cx, value);
    if (!tmp)
        return Err("Couldn't convert to string");
    *ref = std::move(tmp);
    return JS::Ok();
}

GJS_ALWAYS_INLINE
static inline ParseArgsResult assign(JSContext* cx, char c, bool nullable,
                                     JS::HandleValue value,
                                     Gjs::AutoChar* ref) {
    if (c != 'F')
        return Err("Wrong type for %c, got Gjs::AutoChar*", c);
    if (nullable && value.isNull()) {
        ref->release();
        return JS::Ok();
    }
    if (!gjs_string_to_filename(cx, value, ref))
        return Err("Couldn't convert to filename");
    return JS::Ok();
}

GJS_ALWAYS_INLINE
static inline ParseArgsResult assign(JSContext*, char c, bool nullable,
                                     JS::HandleValue value,
                                     JS::MutableHandleString ref) {
    if (c != 'S')
        return Err("Wrong type for %c, got JS::MutableHandleString", c);
    if (nullable && value.isNull()) {
        ref.set(nullptr);
        return JS::Ok();
    }
    if (!value.isString())
        return Err("Not a string");
    ref.set(value.toString());
    return JS::Ok();
}

GJS_ALWAYS_INLINE
static inline ParseArgsResult assign(JSContext* cx, char c, bool nullable,
                                     JS::HandleValue value, int32_t* ref) {
    if (c != 'i')
        return Err("Wrong type for %c, got int32_t*", c);
    if (nullable)
        return Err("Invalid format string combination ?i");
    if (!JS::ToInt32(cx, value, ref))
        return Err("Couldn't convert to integer");
    return JS::Ok();
}

GJS_ALWAYS_INLINE
static inline ParseArgsResult assign(JSContext* cx, char c, bool nullable,
                                     JS::HandleValue value, uint32_t* ref) {
    double num;

    if (c != 'u')
        return Err("Wrong type for %c, got uint32_t*", c);
    if (nullable)
        return Err("Invalid format string combination ?u");
    if (!value.isNumber() || !JS::ToNumber(cx, value, &num))
        return Err("Couldn't convert to unsigned integer");
    if (num > G_MAXUINT32 || num < 0)
        return Err("Value %f is out of range", num);
    *ref = num;
    return JS::Ok();
}

GJS_ALWAYS_INLINE
static inline ParseArgsResult assign(JSContext* cx, char c, bool nullable,
                                     JS::HandleValue value, int64_t* ref) {
    if (c != 't')
        return Err("Wrong type for %c, got int64_t*", c);
    if (nullable)
        return Err("Invalid format string combination ?t");
    if (!JS::ToInt64(cx, value, ref))
        return Err("Couldn't convert to 64-bit integer");
    return JS::Ok();
}

GJS_ALWAYS_INLINE
static inline ParseArgsResult assign(JSContext* cx, char c, bool nullable,
                                     JS::HandleValue value, double* ref) {
    if (c != 'f')
        return Err("Wrong type for %c, got double*", c);
    if (nullable)
        return Err("Invalid format string combination ?f");
    if (!JS::ToNumber(cx, value, ref))
        return Err("Couldn't convert to double");
    return JS::Ok();
}

/* Special case: treat pointer-to-enum as pointer-to-int, but use enable_if to
 * prevent instantiation for any other types besides pointer-to-enum */
template <typename T, typename std::enable_if_t<std::is_enum_v<T>, int> = 0>
GJS_ALWAYS_INLINE static inline ParseArgsResult assign(JSContext* cx, char c,
                                                       bool nullable,
                                                       JS::HandleValue value,
                                                       T* ref) {
    /* Sadly, we cannot use std::underlying_type<T> here; the underlying type of
     * an enum is implementation-defined, so it would not be clear what letter
     * to use in the format string. For the same reason, we can only support
     * enum types that are the same width as int.
     * Additionally, it would be nice to be able to check whether the resulting
     * value was in range for the enum, but that is not possible (yet?) */
    static_assert(sizeof(T) == sizeof(int),
                  "Short or wide enum types not supported");
    return assign(cx, c, nullable, value, reinterpret_cast<int*>(ref));
}

template <typename T>
static inline void free_if_necessary(T param_ref [[maybe_unused]]) {}

template <typename T>
GJS_ALWAYS_INLINE static inline void free_if_necessary(
    JS::Rooted<T>* param_ref) {
    // This is not exactly right, since before we consumed a JS::Value there may
    // have been something different inside the handle. But it has already been
    // clobbered at this point anyhow.
    JS::MutableHandle<T>(param_ref).set(nullptr);
}

template <typename T>
GJS_JSAPI_RETURN_CONVENTION static bool parse_call_args_helper(
    JSContext* cx, const char* function_name, const JS::CallArgs& args,
    const char*& fmt_required, const char*& fmt_optional, unsigned param_ix,
    const char* param_name, T param_ref) {
    bool nullable = false;
    const char *fchar = fmt_required;

    g_return_val_if_fail(param_name, false);

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

    ParseArgsResult res =
        assign(cx, *fchar, nullable, args[param_ix], param_ref);
    if (res.isErr()) {
        /* Our error messages are going to be more useful than whatever was
         * thrown by the various conversion functions */
        const char* message = res.inspectErr().message();
        JS_ClearPendingException(cx);
        gjs_throw(cx, "Error invoking %s, at argument %d (%s): %s",
                  function_name, param_ix, param_name, message);
        return false;
    }

    return true;
}

template <typename T, typename... Args>
GJS_JSAPI_RETURN_CONVENTION static bool parse_call_args_helper(
    JSContext* cx, const char* function_name, const JS::CallArgs& args,
    const char*& fmt_required, const char*& fmt_optional, unsigned param_ix,
    const char* param_name, T param_ref, Args... params) {
    if (!parse_call_args_helper(cx, function_name, args, fmt_required,
                                fmt_optional, param_ix, param_name, param_ref))
        return false;

    bool retval = parse_call_args_helper(cx, function_name, args, fmt_required,
                                         fmt_optional, ++param_ix, params...);

    // We still own JSString/JSObject in the error case, free any we converted
    if (!retval)
        free_if_necessary(param_ref);
    return retval;
}

}  // namespace detail

/* Empty-args version of the template */
GJS_JSAPI_RETURN_CONVENTION [[maybe_unused]] static bool gjs_parse_call_args(
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
 * @args: #JS::CallArgs from #JSNative function
 * @format: Printf-like format specifier containing the expected arguments
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
 *   a Gjs::AutoChar *)
 * S: A string, no conversion (pass a JS::MutableHandleString)
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
 * A prefix character '?' in front of 's', 'F', 'S', or 'o' means that the next
 * value may be null. For 's' or 'F' a null pointer is returned, for 'S' or 'o'
 * the handle is set to null.
 */
template <typename... Args>
GJS_JSAPI_RETURN_CONVENTION static bool gjs_parse_call_args(
    JSContext* cx, const char* function_name, const JS::CallArgs& args,
    const char* format, Args... params) {
    const char *fmt_iter, *fmt_required, *fmt_optional;
    unsigned n_required = 0, n_total = 0;
    bool optional_args = false, ignore_trailing_args = false;

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

    if (!args.requireAtLeast(cx, function_name, n_required))
        return false;
    if (!ignore_trailing_args && args.length() > n_total) {
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

    Gjs::AutoStrv parts{g_strsplit(format, "|", 2)};
    fmt_required = parts.get()[0];
    fmt_optional = parts.get()[1];  // may be null

    return detail::parse_call_args_helper(cx, function_name, args, fmt_required,
                                          fmt_optional, 0, params...);
}

#endif  // GJS_JSAPI_UTIL_ARGS_H_
