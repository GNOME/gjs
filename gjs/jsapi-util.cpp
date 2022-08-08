/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC
// SPDX-FileCopyrightText: 2009 Red Hat, Inc.

#include <config.h>

#include <stdio.h>   // for sscanf
#include <string.h>  // for strlen

#ifdef _WIN32
#    include <windows.h>
#endif

#include <sstream>
#include <string>
#include <utility>  // for move
#include <vector>

#include <js/AllocPolicy.h>
#include <js/Array.h>
#include <js/CallArgs.h>
#include <js/CharacterEncoding.h>
#include <js/Class.h>
#include <js/Conversions.h>
#include <js/ErrorReport.h>
#include <js/Exception.h>
#include <js/GCAPI.h>     // for JS_MaybeGC, NonIncrementalGC, GCRe...
#include <js/GCHashTable.h>  // for GCHashSet
#include <js/GCVector.h>  // for RootedVector
#include <js/HashTable.h>  // for DefaultHasher
#include <js/Object.h>    // for GetClass
#include <js/PropertyAndElement.h>
#include <js/RootingAPI.h>
#include <js/Stack.h>  // for BuildStackString
#include <js/String.h>
#include <js/TypeDecls.h>
#include <js/Value.h>
#include <js/ValueArray.h>
#include <jsapi.h>        // for JS_InstanceOf
#include <jsfriendapi.h>  // for ProtoKeyToClass
#include <mozilla/HashTable.h>  // for HashSet::AddPtr

#include "gjs/atoms.h"
#include "gjs/context-private.h"
#include "gjs/jsapi-util.h"
#include "gjs/macros.h"

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
        proto_class = JS::GetClass(&prototype.toObject());
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

// Helper function: perform ToString on an exception (which may not even be an
// object), except if it is an InternalError, which would throw in ToString.
GJS_JSAPI_RETURN_CONVENTION
static JSString* exception_to_string(JSContext* cx, JS::HandleValue exc) {
    if (exc.isObject()) {
        JS::RootedObject exc_obj(cx, &exc.toObject());
        const JSClass* internal_error =
            js::ProtoKeyToClass(JSProto_InternalError);
        if (JS_InstanceOf(cx, exc_obj, internal_error, nullptr)) {
            JSErrorReport* report = JS_ErrorFromException(cx, exc_obj);
            if (!report->message())
                return JS_NewStringCopyZ(cx, "(unknown internal error)");
            return JS_NewStringCopyUTF8Z(cx, report->message());
        }
    }

    return JS::ToString(cx, exc);
}

// Helper function: format the file name, line number, and column number where a
// SyntaxError occurred.
static std::string format_syntax_error_location(JSContext* cx,
                                                JS::HandleObject exc) {
    const GjsAtoms& atoms = GjsContextPrivate::atoms(cx);

    JS::RootedValue property(cx);
    int32_t line = 0;
    if (JS_GetPropertyById(cx, exc, atoms.line_number(), &property)) {
        if (property.isInt32())
            line = property.toInt32();
    }
    JS_ClearPendingException(cx);

    int32_t column = 0;
    if (JS_GetPropertyById(cx, exc, atoms.column_number(), &property)) {
        if (property.isInt32())
            column = property.toInt32();
    }
    JS_ClearPendingException(cx);

    JS::UniqueChars utf8_filename;
    if (JS_GetPropertyById(cx, exc, atoms.file_name(), &property)) {
        if (property.isString()) {
            JS::RootedString str(cx, property.toString());
            utf8_filename = JS_EncodeStringToUTF8(cx, str);
        }
    }
    JS_ClearPendingException(cx);

    std::ostringstream out;
    out << " @ ";
    if (utf8_filename)
        out << utf8_filename.get();
    else
        out << "<unknown>";
    out << ":" << line << ":" << column;
    return out.str();
}

using CauseSet = JS::GCHashSet<JSObject*, js::DefaultHasher<JSObject*>,
                               js::SystemAllocPolicy>;

static std::string format_exception_with_cause(
    JSContext* cx, JS::HandleObject exc_obj,
    JS::MutableHandle<CauseSet> seen_causes) {
    std::ostringstream out;
    const GjsAtoms& atoms = GjsContextPrivate::atoms(cx);

    JS::UniqueChars utf8_stack;
    // Check both the internal SavedFrame object and the stack property.
    // GErrors will not have the former, and internal errors will not
    // have the latter.
    JS::RootedObject saved_frame(cx, JS::ExceptionStackOrNull(exc_obj));
    JS::RootedString str(cx);
    if (saved_frame) {
        JS::BuildStackString(cx, nullptr, saved_frame, &str, 0);
    } else {
        JS::RootedValue stack(cx);
        if (JS_GetPropertyById(cx, exc_obj, atoms.stack(), &stack) &&
            stack.isString())
            str = stack.toString();
    }
    if (str)
        utf8_stack = JS_EncodeStringToUTF8(cx, str);
    if (utf8_stack)
        out << '\n' << utf8_stack.get();
    JS_ClearPendingException(cx);

    // COMPAT: use JS::GetExceptionCause, mozjs 91.6 and later, on Error objects
    // in order to avoid side effects
    JS::RootedValue v_cause(cx);
    if (!JS_GetPropertyById(cx, exc_obj, atoms.cause(), &v_cause))
        JS_ClearPendingException(cx);
    if (v_cause.isUndefined())
        return out.str();

    JS::RootedObject cause(cx);
    if (v_cause.isObject()) {
        cause = &v_cause.toObject();
        CauseSet::AddPtr entry = seen_causes.lookupForAdd(cause);
        if (entry)
            return out.str();  // cause has been printed already, ref cycle
        if (!seen_causes.add(entry, cause))
            return out.str();  // out of memory, just stop here
    }

    out << "Caused by: ";
    JS::RootedString exc_str(cx, exception_to_string(cx, v_cause));
    if (exc_str) {
        JS::UniqueChars utf8_exception = JS_EncodeStringToUTF8(cx, exc_str);
        if (utf8_exception)
            out << utf8_exception.get();
    }
    JS_ClearPendingException(cx);

    if (v_cause.isObject())
        out << format_exception_with_cause(cx, cause, seen_causes);

    return out.str();
}

static std::string format_exception_log_message(JSContext* cx,
                                                JS::HandleValue exc,
                                                JS::HandleString message) {
    std::ostringstream out;

    if (message) {
        JS::UniqueChars utf8_message = JS_EncodeStringToUTF8(cx, message);
        JS_ClearPendingException(cx);
        if (utf8_message)
            out << utf8_message.get() << ": ";
    }

    JS::RootedString exc_str(cx, exception_to_string(cx, exc));
    if (exc_str) {
        JS::UniqueChars utf8_exception = JS_EncodeStringToUTF8(cx, exc_str);
        if (utf8_exception)
            out << utf8_exception.get();
    }
    JS_ClearPendingException(cx);

    if (!exc.isObject())
        return out.str();

    JS::RootedObject exc_obj(cx, &exc.toObject());
    const JSClass* syntax_error = js::ProtoKeyToClass(JSProto_SyntaxError);
    if (JS_InstanceOf(cx, exc_obj, syntax_error, nullptr)) {
        // We log syntax errors differently, because the stack for those
        // includes only the referencing module, but we want to print out the
        // file name, line number, and column number from the exception.
        // We assume that syntax errors have no cause property, and are not the
        // cause of other exceptions, so no recursion.
        out << format_syntax_error_location(cx, exc_obj);
        return out.str();
    }

    JS::Rooted<CauseSet> seen_causes(cx);
    seen_causes.putNew(exc_obj);
    out << format_exception_with_cause(cx, exc_obj, &seen_causes);
    return out.str();
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
 */
void gjs_log_exception_full(JSContext* cx, JS::HandleValue exc,
                            JS::HandleString message, GLogLevelFlags level) {
    JS::AutoSaveExceptionState saved_exc(cx);
    std::string log_msg = format_exception_log_message(cx, exc, message);
    g_log(G_LOG_DOMAIN, level, "JS ERROR: %s", log_msg.c_str());
    saved_exc.restore();
}

/**
 * gjs_log_exception:
 * @cx: the #JSContext
 *
 * Logs the exception pending on @cx, if any, in response to an exception being
 * thrown that user code cannot catch or has already caught.
 *
 * Returns: %true if an exception was logged, %false if there was none pending.
 */
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

    GjsAutoChar contents;
    if (!g_file_get_contents("/proc/self/stat", contents.out(), &len, nullptr))
        return;

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
            JS::NonIncrementalGC(context, JS::GCOptions::Shrink,
                                 Gjs::GCReason::LINUX_RSS_TRIGGER);
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

/**
 * gjs_get_internal_global:
 *
 * @brief Gets the "internal global" for the context's runtime. The internal
 * global object is the global object used for all "internal" JavaScript
 * code (e.g. the module loader) that should not be accessible from users'
 * code.
 *
 * @param cx a #JSContext
 *
 * @returns the "internal global" for the context's
 *  runtime. Will never return %NULL while GJS has an active context
 *  for the runtime.
 */
JSObject* gjs_get_internal_global(JSContext* cx) {
    return GjsContextPrivate::from_cx(cx)->internal_global();
}

const char* gjs_explain_gc_reason(JS::GCReason reason) {
    if (JS::InternalGCReason(reason))
        return JS::ExplainGCReason(reason);

    static const char* reason_strings[] = {
        "RSS above threshold",
        "GjsContext disposed",
        "Big Hammer hit",
        "gjs_context_gc() called",
    };
    static_assert(G_N_ELEMENTS(reason_strings) == Gjs::GCReason::N_REASONS,
                  "Explanations must match the values in Gjs::GCReason");

    g_assert(size_t(reason) < size_t(JS::GCReason::FIRST_FIREFOX_REASON) +
                                  Gjs::GCReason::N_REASONS &&
             "Bad Gjs::GCReason");
    return reason_strings[size_t(reason) -
                          size_t(JS::GCReason::FIRST_FIREFOX_REASON)];
}
