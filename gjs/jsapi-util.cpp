/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC
// SPDX-FileCopyrightText: 2009 Red Hat, Inc.

#include <config.h>

#include <stdio.h>   // for sscanf
#include <string.h>  // for strlen

#ifdef _WIN32
#    define WIN32_LEAN_AND_MEAN
#    include <windows.h>
#endif

#include <codecvt>  // for codecvt_utf8_utf16
#include <locale>   // for wstring_convert
#include <string>
#include <utility>  // for move
#include <vector>

#include <js/Array.h>
#include <js/CallArgs.h>
#include <js/CharacterEncoding.h>
#include <js/Class.h>
#include <js/Conversions.h>
#include <js/ErrorReport.h>
#include <js/GCAPI.h>     // for JS_MaybeGC, NonIncrementalGC, GCRe...
#include <js/GCVector.h>  // for RootedVector
#include <js/RootingAPI.h>
#include <js/TypeDecls.h>
#include <js/Value.h>
#include <js/ValueArray.h>
#include <jsapi.h>        // for JS_GetPropertyById, JS_ClearPendin...
#include <jsfriendapi.h>  // for ProtoKeyToClass

#include "gjs/atoms.h"
#include "gjs/context-private.h"
#include "gjs/jsapi-util.h"

static void
throw_property_lookup_error(JSContext       *cx,
                            JS::HandleObject obj,
                            const char      *description,
                            JS::HandleId     property_name,
                            const char      *reason)
{
    /* remember gjs_throw() is a no-op if JS_GetProperty()
     * already set an exception
     */
    if (description)
        gjs_throw(cx, "No property '%s' in %s (or %s)",
                  gjs_debug_id(property_name).c_str(), description, reason);
    else
        gjs_throw(cx, "No property '%s' in object %p (or %s)",
                  gjs_debug_id(property_name).c_str(), obj.get(), reason);
}

/* Returns whether the object had the property; if the object did
 * not have the property, always sets an exception. Treats
 * "the property's value is undefined" the same as "no such property,".
 * Guarantees that *value_p is set to something, if only JS::UndefinedValue(),
 * even if an exception is set and false is returned.
 *
 * SpiderMonkey will emit a warning if the property is not present, so don't
 * use this if you expect the property not to be present some of the time.
 */
bool
gjs_object_require_property(JSContext             *context,
                            JS::HandleObject       obj,
                            const char            *obj_description,
                            JS::HandleId           property_name,
                            JS::MutableHandleValue value)
{
    value.setUndefined();

    if (G_UNLIKELY(!JS_GetPropertyById(context, obj, property_name, value)))
        return false;

    if (G_LIKELY(!value.isUndefined()))
        return true;

    throw_property_lookup_error(context, obj, obj_description, property_name,
                                "its value was undefined");
    return false;
}

bool
gjs_object_require_property(JSContext       *cx,
                            JS::HandleObject obj,
                            const char      *description,
                            JS::HandleId     property_name,
                            bool            *value)
{
    JS::RootedValue prop_value(cx);
    if (JS_GetPropertyById(cx, obj, property_name, &prop_value) &&
        prop_value.isBoolean()) {
        *value = prop_value.toBoolean();
        return true;
    }

    throw_property_lookup_error(cx, obj, description, property_name,
                                "it was not a boolean");
    return false;
}

bool
gjs_object_require_property(JSContext       *cx,
                            JS::HandleObject obj,
                            const char      *description,
                            JS::HandleId     property_name,
                            int32_t         *value)
{
    JS::RootedValue prop_value(cx);
    if (JS_GetPropertyById(cx, obj, property_name, &prop_value) &&
        prop_value.isInt32()) {
        *value = prop_value.toInt32();
        return true;
    }

    throw_property_lookup_error(cx, obj, description, property_name,
                                "it was not a 32-bit integer");
    return false;
}

/* Converts JS string value to UTF-8 string. */
bool gjs_object_require_property(JSContext* cx, JS::HandleObject obj,
                                 const char* description,
                                 JS::HandleId property_name,
                                 JS::UniqueChars* value) {
    JS::RootedValue prop_value(cx);
    if (JS_GetPropertyById(cx, obj, property_name, &prop_value)) {
        JS::UniqueChars tmp = gjs_string_to_utf8(cx, prop_value);
        if (tmp) {
            *value = std::move(tmp);
            return true;
        }
    }

    throw_property_lookup_error(cx, obj, description, property_name,
                                "it was not a valid string");
    return false;
}

bool
gjs_object_require_property(JSContext              *cx,
                            JS::HandleObject        obj,
                            const char             *description,
                            JS::HandleId            property_name,
                            JS::MutableHandleObject value)
{
    JS::RootedValue prop_value(cx);
    if (JS_GetPropertyById(cx, obj, property_name, &prop_value) &&
        prop_value.isObject()) {
        value.set(&prop_value.toObject());
        return true;
    }

    throw_property_lookup_error(cx, obj, description, property_name,
                                "it was not an object");
    return false;
}

bool
gjs_object_require_converted_property(JSContext       *cx,
                                      JS::HandleObject obj,
                                      const char      *description,
                                      JS::HandleId     property_name,
                                      uint32_t        *value)
{
    JS::RootedValue prop_value(cx);
    if (JS_GetPropertyById(cx, obj, property_name, &prop_value) &&
        JS::ToUint32(cx, prop_value, value)) {
        return true;
    }

    throw_property_lookup_error(cx, obj, description, property_name,
                                "it couldn't be converted to uint32");
    return false;
}

void
gjs_throw_constructor_error(JSContext *context)
{
    gjs_throw(context,
              "Constructor called as normal method. Use 'new SomeObject()' not 'SomeObject()'");
}

void gjs_throw_abstract_constructor_error(JSContext* context,
                                          const JS::CallArgs& args) {
    const JSClass *proto_class;
    const char *name = "anonymous";

    const GjsAtoms& atoms = GjsContextPrivate::atoms(context);
    JS::RootedObject callee(context, &args.callee());
    JS::RootedValue prototype(context);
    if (JS_GetPropertyById(context, callee, atoms.prototype(), &prototype)) {
        proto_class = JS_GetClass(&prototype.toObject());
        name = proto_class->name;
    }

    gjs_throw(context, "You cannot construct new instances of '%s'", name);
}

JSObject* gjs_build_string_array(JSContext* context,
                                 const std::vector<std::string>& strings) {
    JS::RootedValueVector elems(context);
    if (!elems.reserve(strings.size())) {
        JS_ReportOutOfMemory(context);
        return nullptr;
    }

    for (const std::string& string : strings) {
        JS::ConstUTF8CharsZ chars(string.c_str(), string.size());
        JS::RootedValue element(context,
            JS::StringValue(JS_NewStringCopyUTF8Z(context, chars)));
        elems.infallibleAppend(element);
    }

    return JS::NewArrayObject(context, elems);
}

JSObject* gjs_define_string_array(JSContext* context,
                                  JS::HandleObject in_object,
                                  const char* array_name,
                                  const std::vector<std::string>& strings,
                                  unsigned attrs) {
    JS::RootedObject array(context, gjs_build_string_array(context, strings));
    if (!array)
        return nullptr;

    if (!JS_DefineProperty(context, in_object, array_name, array, attrs))
        return nullptr;

    return array;
}

/**
 * gjs_string_readable:
 *
 * Return a string that can be read back by gjs-console; for
 * JS strings that contain valid Unicode, we return a UTF-8 formatted
 * string.  Otherwise, we return one where non-ASCII-printable bytes
 * are \x escaped.
 *
 */
[[nodiscard]] static char* gjs_string_readable(JSContext* context,
                                               JS::HandleString string) {
    GString *buf = g_string_new("");

    g_string_append_c(buf, '"');

    JS::UniqueChars chars(JS_EncodeStringToUTF8(context, string));
    if (!chars) {
        /* I'm not sure this code will actually ever be reached except in the
         * case of OOM, since JS_EncodeStringToUTF8() seems to happily output
         * non-valid UTF-8 bytes. However, let's leave this in, since
         * SpiderMonkey may decide to do validation in the future. */

        /* Find out size of buffer to allocate, not counting 0-terminator */
        size_t len = JS_PutEscapedString(context, NULL, 0, string, '"');
        char *escaped = g_new(char, len + 1);

        JS_PutEscapedString(context, escaped, len, string, '"');
        g_string_append(buf, escaped);
        g_free(escaped);
    } else {
        g_string_append(buf, chars.get());
    }

    g_string_append_c(buf, '"');

    return g_string_free(buf, false);
}

[[nodiscard]] static char* _gjs_g_utf8_make_valid(const char* name) {
    GString *string;
    const char *remainder, *invalid;
    int remaining_bytes, valid_bytes;

    g_return_val_if_fail (name != NULL, NULL);

    string = nullptr;
    remainder = name;
    remaining_bytes = strlen (name);

    while (remaining_bytes != 0) {
        if (g_utf8_validate (remainder, remaining_bytes, &invalid))
            break;
        valid_bytes = invalid - remainder;

        if (!string)
            string = g_string_sized_new (remaining_bytes);

        g_string_append_len (string, remainder, valid_bytes);
        /* append U+FFFD REPLACEMENT CHARACTER */
        g_string_append (string, "\357\277\275");

        remaining_bytes -= valid_bytes + 1;
        remainder = invalid + 1;
    }

    if (!string)
        return g_strdup (name);

    g_string_append (string, remainder);

    g_assert (g_utf8_validate (string->str, -1, NULL));

    return g_string_free (string, false);
}

/**
 * gjs_value_debug_string:
 * @context:
 * @value: Any JavaScript value
 *
 * Returns: A UTF-8 encoded string describing @value
 */
char*
gjs_value_debug_string(JSContext      *context,
                       JS::HandleValue value)
{
    /* Special case debug strings for strings */
    if (value.isString()) {
        JS::RootedString str(context, value.toString());
        return gjs_string_readable(context, str);
    }

    JS::RootedString str(context, JS::ToString(context, value));

    if (!str) {
        JS_ClearPendingException(context);
        str = JS_ValueToSource(context, value);
    }

    if (!str) {
        if (value.isObject()) {
            /* Specifically the Call object (see jsfun.c in spidermonkey)
             * does not have a toString; there may be others also.
             */
            const JSClass *klass = JS_GetClass(&value.toObject());
            if (klass != NULL) {
                str = JS_NewStringCopyZ(context, klass->name);
                JS_ClearPendingException(context);
                if (!str)
                    return g_strdup("[out of memory copying class name]");
            } else {
                gjs_log_exception(context);
                return g_strdup("[unknown object]");
            }
        } else {
            return g_strdup("[unknown non-object]");
        }
    }

    g_assert(str);

    JS::UniqueChars bytes = JS_EncodeStringToUTF8(context, str);
    return _gjs_g_utf8_make_valid(bytes.get());
}

/**
 * gjs_log_exception_full:
 * @cx: the #JSContext
 * @exc: the exception value to be logged
 * @message: a string to prepend to the log message
 * @level: the severity level at which to log the exception
 *
 * Currently, uses %G_LOG_LEVEL_WARNING if the exception is being printed after
 * being caught, and %G_LOG_LEVEL_CRITICAL if it was not caught by user code.
 *
 * Returns: %true if an exception was logged, %false if there was none pending.
 */
bool gjs_log_exception_full(JSContext* context, JS::HandleValue exc,
                            JS::HandleString message, GLogLevelFlags level) {
    JS::AutoSaveExceptionState saved_exc(context);
    const GjsAtoms& atoms = GjsContextPrivate::atoms(context);

    JS::RootedObject exc_obj(context);
    JS::RootedString exc_str(context);
    bool is_syntax = false, is_internal = false;
    if (exc.isObject()) {
        exc_obj = &exc.toObject();
        const JSClass* syntax_error = js::ProtoKeyToClass(JSProto_SyntaxError);
        is_syntax = JS_InstanceOf(context, exc_obj, syntax_error, nullptr);

        const JSClass* internal_error =
            js::ProtoKeyToClass(JSProto_InternalError);
        is_internal = JS_InstanceOf(context, exc_obj, internal_error, nullptr);
    }

    if (is_internal) {
        JSErrorReport* report = JS_ErrorFromException(context, exc_obj);
        if (!report->message())
            exc_str = JS_NewStringCopyZ(context, "(unknown internal error)");
        else
            exc_str = JS_NewStringCopyUTF8Z(context, report->message());
    } else {
        exc_str = JS::ToString(context, exc);
    }
    JS::UniqueChars utf8_exception;
    if (exc_str)
        utf8_exception = JS_EncodeStringToUTF8(context, exc_str);

    JS::UniqueChars utf8_message;
    if (message)
        utf8_message = JS_EncodeStringToUTF8(context, message);

    /* We log syntax errors differently, because the stack for those includes
       only the referencing module, but we want to print out the filename and
       line number from the exception.
    */

    if (is_syntax) {
        JS::RootedValue js_lineNumber(context), js_fileName(context);
        unsigned lineNumber;

        JS_GetPropertyById(context, exc_obj, atoms.line_number(),
                           &js_lineNumber);
        JS_GetPropertyById(context, exc_obj, atoms.file_name(), &js_fileName);

        JS::UniqueChars utf8_filename;
        if (js_fileName.isString()) {
            JS::RootedString str(context, js_fileName.toString());
            utf8_filename = JS_EncodeStringToUTF8(context, str);
        }

        lineNumber = js_lineNumber.toInt32();

        if (message) {
            g_log(G_LOG_DOMAIN, level, "JS ERROR: %s: %s @ %s:%u",
                  utf8_message.get(), utf8_exception.get(),
                  utf8_filename ? utf8_filename.get() : "unknown", lineNumber);
        } else {
            g_log(G_LOG_DOMAIN, level, "JS ERROR: %s @ %s:%u",
                  utf8_exception.get(),
                  utf8_filename ? utf8_filename.get() : "unknown", lineNumber);
        }

    } else {
        JS::UniqueChars utf8_stack;
        if (exc.isObject()) {
            // Check both the internal SavedFrame object and the stack property.
            // GErrors will not have the former, and internal errors will not
            // have the latter.
            JS::RootedObject saved_frame(context,
                                         JS::ExceptionStackOrNull(exc_obj));
            JS::RootedString str(context);
            if (saved_frame) {
                JS::BuildStackString(context, nullptr, saved_frame, &str, 0);
            } else {
                JS::RootedValue stack(context);
                JS_GetPropertyById(context, exc_obj, atoms.stack(), &stack);
                if (stack.isString())
                    str = stack.toString();
            }
            if (str)
                utf8_stack = JS_EncodeStringToUTF8(context, str);
        }

        if (message) {
            if (utf8_stack)
                g_log(G_LOG_DOMAIN, level, "JS ERROR: %s: %s\n%s",
                      utf8_message.get(), utf8_exception.get(),
                      utf8_stack.get());
            else
                g_log(G_LOG_DOMAIN, level, "JS ERROR: %s: %s",
                      utf8_message.get(), utf8_exception.get());
        } else {
            if (utf8_stack)
                g_log(G_LOG_DOMAIN, level, "JS ERROR: %s\n%s",
                      utf8_exception.get(), utf8_stack.get());
            else
                g_log(G_LOG_DOMAIN, level, "JS ERROR: %s",
                      utf8_exception.get());
        }
    }

    saved_exc.restore();

    return true;
}

bool
gjs_log_exception(JSContext  *context)
{
    JS::RootedValue exc(context);
    if (!JS_GetPendingException(context, &exc))
        return false;

    JS_ClearPendingException(context);

    gjs_log_exception_full(context, exc, nullptr, G_LOG_LEVEL_WARNING);
    return true;
}

/**
 * gjs_log_exception_uncaught:
 * @cx: the #JSContext
 *
 * Logs the exception pending on @cx, if any, indicating an uncaught exception
 * in the running JS program.
 * (Currently, due to main loop boundaries, uncaught exceptions may not bubble
 * all the way back up to the top level, so this doesn't necessarily mean the
 * program exits with an error.)
 *
 * Returns: %true if an exception was logged, %false if there was none pending.
 */
bool gjs_log_exception_uncaught(JSContext* cx) {
    JS::RootedValue exc(cx);
    if (!JS_GetPendingException(cx, &exc))
        return false;

    JS_ClearPendingException(cx);

    gjs_log_exception_full(cx, exc, nullptr, G_LOG_LEVEL_CRITICAL);
    return true;
}

#ifdef __linux__
// This type has to be long and not int32_t or int64_t, because of the %ld
// sscanf specifier mandated in "man proc". The NOLINT comment is because
// cpplint will ask you to avoid long in favour of defined bit width types.
static void _linux_get_self_process_size(long* rss_size)  // NOLINT(runtime/int)
{
    char *iter;
    gsize len;
    int i;

    *rss_size = 0;

    char* contents_unowned;
    if (!g_file_get_contents("/proc/self/stat", &contents_unowned, &len,
                             nullptr))
        return;

    GjsAutoChar contents = contents_unowned;
    iter = contents;
    // See "man proc" for where this 23 comes from
    for (i = 0; i < 23; i++) {
        iter = strchr (iter, ' ');
        if (!iter)
            return;
        iter++;
    }
    sscanf(iter, " %ld", rss_size);
}

// We initiate a GC if RSS has grown by this much
static uint64_t linux_rss_trigger;
static int64_t last_gc_check_time;
#endif

void
gjs_gc_if_needed (JSContext *context)
{
#ifdef __linux__
    {
        long rss_size;  // NOLINT(runtime/int)
        gint64 now;

        /* We rate limit GCs to at most one per 5 frames.
           One frame is 16666 microseconds (1000000/60)*/
        now = g_get_monotonic_time();
        if (now - last_gc_check_time < 5 * 16666)
            return;

        last_gc_check_time = now;

        _linux_get_self_process_size(&rss_size);

        /* linux_rss_trigger is initialized to 0, so currently
         * we always do a full GC early.
         *
         * Here we see if the RSS has grown by 25% since
         * our last look; if so, initiate a full GC.  In
         * theory using RSS is bad if we get swapped out,
         * since we may be overzealous in GC, but on the
         * other hand, if swapping is going on, better
         * to GC.
         */
        if (rss_size < 0)
            return;  // doesn't make sense
        uint64_t rss_usize = rss_size;
        if (rss_usize > linux_rss_trigger) {
            linux_rss_trigger = MIN(G_MAXUINT32, rss_usize * 1.25);
            JS::NonIncrementalGC(context, GC_SHRINK, JS::GCReason::API);
        } else if (rss_size < (0.75 * linux_rss_trigger)) {
            /* If we've shrunk by 75%, lower the trigger */
            linux_rss_trigger = rss_usize * 1.25;
        }
    }
#else  // !__linux__
    (void)context;
#endif
}

/**
 * gjs_maybe_gc:
 *
 * Low level version of gjs_context_maybe_gc().
 */
void
gjs_maybe_gc (JSContext *context)
{
    JS_MaybeGC(context);
    gjs_gc_if_needed(context);
}

/**
 * gjs_get_import_global:
 * @context: a #JSContext
 *
 * Gets the "import global" for the context's runtime. The import
 * global object is the global object for the context. It is used
 * as the root object for the scope of modules loaded by GJS in this
 * runtime, and should also be used as the globals 'obj' argument passed
 * to JS_InitClass() and the parent argument passed to JS_ConstructObject()
 * when creating a native classes that are shared between all contexts using
 * the runtime. (The standard JS classes are not shared, but we share
 * classes such as GObject proxy classes since objects of these classes can
 * easily migrate between contexts and having different classes depending
 * on the context where they were first accessed would be confusing.)
 *
 * Return value: the "import global" for the context's
 *  runtime. Will never return %NULL while GJS has an active context
 *  for the runtime.
 */
JSObject* gjs_get_import_global(JSContext* cx) {
    return GjsContextPrivate::from_cx(cx)->global();
}

#if defined(G_OS_WIN32) && (defined(_MSC_VER) && (_MSC_VER >= 1900))
/* Unfortunately Visual Studio's C++ .lib somehow did not contain the right
 * codecvt stuff that we need to convert from utf8 to utf16 (char16_t), so we
 * need to work around this Visual Studio bug.  Use Windows API
 * MultiByteToWideChar() and obtain the std::u16string on the std::wstring we
 * obtain from MultiByteToWideChar().  See:
 * https://social.msdn.microsoft.com/Forums/en-US/8f40dcd8-c67f-4eba-9134-a19b9178e481/vs-2015-rc-linker-stdcodecvt-error?forum=vcgeneral
 */
static std::wstring gjs_win32_vc140_utf8_to_utf16(const char* str,
                                                  ssize_t len) {
    int bufsize = MultiByteToWideChar(CP_UTF8, 0, str, len, nullptr, 0);
    if (bufsize == 0)
        return nullptr;

    std::wstring wstr(bufsize, 0);
    int result = MultiByteToWideChar(CP_UTF8, 0, str, len, &wstr[0], bufsize);
    if (result == 0)
        return nullptr;

    wstr.resize(len < 0 ? strlen(str) : len);
    return wstr;
}
#endif

std::u16string gjs_utf8_script_to_utf16(const char* script, ssize_t len) {
#if defined(G_OS_WIN32) && (defined(_MSC_VER) && (_MSC_VER >= 1900))
    std::wstring wscript = gjs_win32_vc140_utf8_to_utf16(script, len);
    return std::u16string(reinterpret_cast<const char16_t*>(wscript.c_str()));
#else
    std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> convert;
    if (len < 0)
        return convert.from_bytes(script);
    return convert.from_bytes(script, script + len);
#endif
}
