/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Philip Chimento <philip.chimento@gmail.com>

#pragma once

#include <config.h>

#include <format>

// Shared facilities for GJS's custom formatters

namespace Gjs {

/**
 * Gjs::FormatterBase:
 *
 * Base class for formatters that accept a closed set of single-character
 * format specifiers, and no other formatting options. For example, have a
 * std::formatter specialization inherit from FormatterBase<'?', 't'> in order
 * to support formatting that type with {:?}, {:t}, and {}. In the latter case,
 * spec() will return '\0'.
 */
template <char... ACCEPTED>
class FormatterBase {
    char m_spec = '\0';

    // std::format_parse_context::iterator is const char* on most platforms, but
    // is allowed to be an opaque type, so don't hardcode it.
    using ParseIter = std::format_parse_context::iterator;

 public:
    constexpr ParseIter parse(std::format_parse_context& cx) {
        ParseIter it = cx.begin();
        if (it != cx.end() && ((*it == ACCEPTED) || ...)) {
            m_spec = *it;
            ++it;
        }
        // Doesn't consume character if not recognized, letting stdlib error
        return it;
    }

    [[nodiscard]] constexpr char spec() const { return m_spec; }
};

}  // namespace Gjs
