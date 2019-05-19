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

#include <cstddef>        // for size_t
#include <string>         // for string
#include <unordered_set>  // for unordered_set
#include <utility>        // for hash, move

#include <glib.h>  // for g_warning

#include "gjs/jsapi-wrapper.h"

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
