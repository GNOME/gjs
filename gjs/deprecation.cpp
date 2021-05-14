/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2018 Philip Chimento <philip.chimento@gmail.com>

#include <config.h>

#include <cstddef>        // for size_t
#include <functional>     // for hash<int>
#include <string>         // for string
#include <string_view>    // for hash<string>
#include <unordered_set>  // for unordered_set
#include <utility>        // for move

#include <glib.h>  // for g_warning

#include <js/CharacterEncoding.h>
#include <js/Conversions.h>
#include <js/RootingAPI.h>
#include <js/TypeDecls.h>
#include <js/Utility.h>  // for UniqueChars
#include <js/Value.h>
#include <jsapi.h>        // for MaxFrames, CaptureCurrentStack
#include <jsfriendapi.h>  // for FormatStackDump

#include "gjs/deprecation.h"
#include "gjs/jsapi-util.h"  // IWYU pragma: keep
#include "gjs/macros.h"

const char* messages[] = {
    // None:
    "(invalid message)",

    // ByteArrayInstanceToString:
    "Some code called array.toString() on a Uint8Array instance. Previously "
    "this would have interpreted the bytes of the array as a string, but that "
    "is nonstandard. In the future this will return the bytes as "
    "comma-separated digits. For the time being, the old behavior has been "
    "preserved, but please fix your code anyway to explicitly call ByteArray"
    ".toString(array).\n"
    "(Note that array.toString() may have been called implicitly.)",

    // DeprecatedGObjectProperty:
    "Some code tried to set a deprecated GObject property.",
};

struct DeprecationEntry {
    GjsDeprecationMessageId id;
    std::string loc;

    DeprecationEntry(GjsDeprecationMessageId an_id, const char* a_loc)
        : id(an_id), loc(a_loc) {}

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
static JS::UniqueChars get_callsite(JSContext* cx) {
    JS::RootedObject stack_frame(cx);
    if (!JS::CaptureCurrentStack(cx, &stack_frame,
                                 JS::StackCapture(JS::MaxFrames(1))) ||
        !stack_frame)
        return nullptr;

    JS::RootedValue v_frame(cx, JS::ObjectValue(*stack_frame));
    JS::RootedString frame_string(cx, JS::ToString(cx, v_frame));
    if (!frame_string)
        return nullptr;

    return JS_EncodeStringToUTF8(cx, frame_string);
}

/* Note, this can only be called from the JS thread because it uses the full
 * stack dump API and not the "safe" gjs_dumpstack() which can only print to
 * stdout or stderr. Do not use this function during GC, for example. */
void _gjs_warn_deprecated_once_per_callsite(JSContext* cx,
                                            const GjsDeprecationMessageId id) {
    JS::UniqueChars callsite(get_callsite(cx));
    DeprecationEntry entry(id, callsite.get());
    if (!logged_messages.count(entry)) {
        JS::UniqueChars stack_dump =
            JS::FormatStackDump(cx, false, false, false);
        g_warning("%s\n%s", messages[id], stack_dump.get());
        logged_messages.insert(std::move(entry));
    }
}
