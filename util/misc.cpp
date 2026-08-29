/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC

#include <config.h>

#include <charconv>
#include <string>
#include <string_view>
#include <system_error>

#include <glib.h>

#include <mozilla/Result.h>

#include "util/misc.h"

using mozilla::Err;

bool gjs_environment_variable_is_set(const char* env_variable_name) {
    const char* s = g_getenv(env_variable_name);
    if (!s)
        return false;

    if (*s == '\0')
        return false;

    return true;
}

namespace Gjs {

StatmParseResult parse_statm_file_rss(const char* file_contents) {
    auto npos = std::string_view::npos;

    // See "man proc_pid_statm"; RSS is the 2nd space-separated field, after
    // SIZE, which we skip.
    std::string_view view{file_contents};
    size_t space_index = view.find(' ');
    if (space_index == npos)
        return Err("Unexpected missing RSS field in /proc/self/statm");
    view.remove_prefix(space_index + 1);

    uint64_t rss_size;
    auto result =
        std::from_chars(view.data(), view.data() + view.size(), rss_size);
    if (result.ec != std::errc{}) {  // COMPAT: operator bool in c++26
        return Err(StatmParseError{
            "Error reading RSS field in /proc/self/statm", result});
    }
    if (*result.ptr != ' ' && *result.ptr != '\0')
        return Err("Badly formatted RSS field in /proc/self/statm");
    return rss_size;
}

StatmParseError::StatmParseError(const char* message,
                                 std::from_chars_result result) {
    // COMPAT: operator bool in c++26
    g_assert(result.ec != std::errc() && "result should not be successful");

    std::error_code code = std::make_error_code(result.ec);
    m_message = std::string(message) + ": " + code.message() +
                " (remaining string '" + result.ptr + "')";
}

}  // namespace Gjs
