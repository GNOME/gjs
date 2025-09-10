/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2018 Philip Chimento <philip.chimento@gmail.com>

#include <config.h>

#include <cstddef>        // for size_t
#include <functional>     // for hash<int>
#include <sstream>
#include <string>         // for string
#include <string_view>
#include <unordered_set>  // for unordered_set
#include <utility>        // for move
#include <vector>

#include <glib.h>  // for g_warning

#include <js/CharacterEncoding.h>
#include <js/Conversions.h>
#include <js/RootingAPI.h>
#include <js/Stack.h>  // for CaptureCurrentStack, MaxFrames
#include <js/TypeDecls.h>
#include <js/Utility.h>  // for UniqueChars
#include <js/Value.h>
#include <js/friend/DumpFunctions.h>

#include "gjs/deprecation.h"
#include "gjs/macros.h"

const char* messages[] = {
    // None:
    "(invalid message)",

    // ByteArrayInstanceToString:
    "Some code called array.toString() on a Uint8Array instance. Previously "
    "this would have interpreted the bytes of the array as a string, but that "
    "is nonstandard. In the future this will return the bytes as "
    "comma-separated digits. For the time being, the old behavior has been "
    "preserved, but please fix your code anyway to use TextDecoder.\n"
    "(Note that array.toString() may have been called implicitly.)",

    // DeprecatedGObjectProperty:
    "The GObject property {}.{} is deprecated.",

    // ModuleExportedLetOrConst:
    "Some code accessed the property '{}' on the module '{}'. That property "
    "was defined with 'let' or 'const' inside the module. This was previously "
    "supported, but is not correct according to the ES6 standard. Any symbols "
    "to be exported from a module must be defined with 'var'. The property "
    "access will work as previously for the time being, but please fix your "
    "code anyway.",

    // PlatformSpecificTypelib:
    ("{} has been moved to a separate platform-specific library. Please update "
     "your code to use {} instead."),
};

static_assert(G_N_ELEMENTS(messages) == GjsDeprecationMessageId::LastValue);

struct DeprecationEntry {
    GjsDeprecationMessageId id;
    std::string loc;

    DeprecationEntry(GjsDeprecationMessageId an_id, const char* a_loc)
        : id(an_id), loc(a_loc ? a_loc : "unknown") {}

    bool operator==(const DeprecationEntry& other) const {
        return id == other.id && loc == other.loc;
    }
};

namespace std {
template <>
struct hash<DeprecationEntry> {
    size_t operator()(const DeprecationEntry& key) const {
        return hash<int>()(key.id) ^ hash<std::string>()(key.loc);
    }
};
};  // namespace std

static std::unordered_set<DeprecationEntry> logged_messages;

GJS_JSAPI_RETURN_CONVENTION
static JS::UniqueChars get_callsite(JSContext* cx,
                                    unsigned max_frames /* = 1 */) {
    JS::RootedObject stack_frame(cx);
    if (!JS::CaptureCurrentStack(cx, &stack_frame,
                                 JS::StackCapture{JS::MaxFrames{max_frames}}) ||
        !stack_frame)
        return nullptr;

    JS::RootedValue v_frame(cx, JS::ObjectValue(*stack_frame));
    JS::RootedString frame_string(cx, JS::ToString(cx, v_frame));
    if (!frame_string)
        return nullptr;

    return JS_EncodeStringToUTF8(cx, frame_string);
}

static void warn_deprecated_unsafe_internal(JSContext* cx,
                                            const GjsDeprecationMessageId id,
                                            const char* msg,
                                            unsigned max_frames /* = 1 */) {
    JS::UniqueChars callsite{get_callsite(cx, max_frames)};
    DeprecationEntry entry(id, callsite.get());
    if (!logged_messages.count(entry)) {
        JS::UniqueChars stack_dump =
            JS::FormatStackDump(cx, false, false, false);
        g_warning("%s\n%s", msg, stack_dump.get());
        logged_messages.insert(std::move(entry));
    }
}

/* Note, this can only be called from the JS thread because it uses the full
 * stack dump API and not the "safe" gjs_dumpstack() which can only print to
 * stdout or stderr. Do not use this function during GC, for example. */
void _gjs_warn_deprecated_once_per_callsite(JSContext* cx,
                                            const GjsDeprecationMessageId id,
                                            unsigned max_frames) {
    warn_deprecated_unsafe_internal(cx, id, messages[id], max_frames);
}

void _gjs_warn_deprecated_once_per_callsite(
    JSContext* cx, GjsDeprecationMessageId id,
    const std::vector<std::string>& args, unsigned max_frames) {
    // In C++20, use std::format() for this
    std::string_view format_string{messages[id]};
    std::stringstream message;

    size_t pos = 0;
    size_t copied = 0;
    size_t args_ptr = 0;
    size_t nargs_given = args.size();

    while ((pos = format_string.find("{}", pos)) != std::string::npos) {
        if (args_ptr >= nargs_given) {
            g_critical("Only %zu format args passed for message ID %u",
                       nargs_given, unsigned{id});
            return;
        }

        message << format_string.substr(copied, pos - copied);
        message << args[args_ptr++];
        pos = copied = pos + 2;  // skip over braces
    }
    if (args_ptr != nargs_given) {
        g_critical("Excess %zu format args passed for message ID %u",
                   nargs_given, unsigned{id});
        return;
    }

    message << format_string.substr(copied, std::string::npos);

    std::string message_formatted = message.str();
    warn_deprecated_unsafe_internal(cx, id, message_formatted.c_str(),
                                    max_frames);
}
