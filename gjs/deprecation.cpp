/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * Copyright (c) 2018  Philip Chimento <philip.chimento@gmail.com>
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

#include <string.h>

#include <unordered_set>

#include "gjs/context-private.h"
#include "gjs/deprecation.h"
#include "gjs/jsapi-util.h"
#include "gjs/jsapi-wrapper.h"

const char* messages[] = {
    // None:
    "(invalid message)",
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

static char* get_callsite(JSContext* cx) {
    JS::RootedObject stack_frame(cx);
    if (!JS::CaptureCurrentStack(cx, &stack_frame,
                                 JS::StackCapture(JS::MaxFrames(1))) ||
        !stack_frame)
        return nullptr;

    JS::RootedValue v_frame(cx, JS::ObjectValue(*stack_frame));
    JS::RootedString frame_string(cx, JS::ToString(cx, v_frame));
    if (!frame_string)
        return nullptr;

    GjsAutoJSChar frame_utf8;
    if (!gjs_string_to_utf8(cx, JS::StringValue(frame_string), &frame_utf8))
        return nullptr;
    return frame_utf8.release();
}

/* Note, this can only be called from the JS thread because it uses the full
 * stack dump API and not the "safe" gjs_dumpstack() which can only print to
 * stdout or stderr. Do not use this function during GC, for example. */
void _gjs_warn_deprecated_once_per_callsite(JSContext* cx,
                                            const GjsDeprecationMessageId id) {
    GjsAutoJSChar callsite = get_callsite(cx);
    DeprecationEntry entry(id, callsite);
    if (!logged_messages.count(entry)) {
        JS::UniqueChars stack_dump = JS::FormatStackDump(cx, nullptr, false,
            false, false);
        g_warning("%s\n%s", messages[id], stack_dump.get());
        logged_messages.insert(std::move(entry));
    }
}
