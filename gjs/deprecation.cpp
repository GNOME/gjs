/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2018 Philip Chimento <philip.chimento@gmail.com>

#include <config.h>

#include <cstddef>        // for size_t
#include <functional>     // for hash<int>
#include <string>         // for string
#include <unordered_set>  // for unordered_set
#include <utility>        // for move

#include <js/CharacterEncoding.h>
#include <js/Conversions.h>
#include <js/RootingAPI.h>
#include <js/Stack.h>  // for CaptureCurrentStack, MaxFrames
#include <js/TypeDecls.h>
#include <js/Utility.h>  // for UniqueChars
#include <js/Value.h>
#include <js/friend/DumpFunctions.h>

#include "gjs/deprecation.h"
#include "gjs/jsapi-util.h"  // IWYU pragma: keep (for formatter)
#include "gjs/macros.h"
#include "util/log.h"

struct DeprecationEntry {
    GjsDeprecationMessageId id;
    std::string loc;

    DeprecationEntry(GjsDeprecationMessageId an_id, const char* a_loc)
        : id(an_id), loc(a_loc ? a_loc : "unknown") {}

    bool operator==(const DeprecationEntry& other) const = default;
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
static JS::UniqueChars get_callsite(JSContext* cx, unsigned max_frames) {
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

void Gjs::detail::warn_deprecated_internal(JSContext* cx,
                                           const GjsDeprecationMessageId id,
                                           const char* msg,
                                           unsigned max_frames) {
    JS::UniqueChars callsite{get_callsite(cx, max_frames)};
    DeprecationEntry entry(id, callsite.get());
    auto insert_result = logged_messages.insert(std::move(entry));
    if (insert_result.second) {
        JS::UniqueChars stack_dump =
            JS::FormatStackDump(cx, false, false, false);
        gjs_warning("{}\n{}", msg, stack_dump);
    }
}
