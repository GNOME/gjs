/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC

#ifndef UTIL_MISC_H_
#define UTIL_MISC_H_

#include <config.h>

#include <errno.h>
#include <stdio.h>  // for FILE, stdout
#include <string.h>  // for memcpy

#ifdef G_DISABLE_ASSERT
#    define GJS_USED_ASSERT [[maybe_unused]]
#else
#    define GJS_USED_ASSERT
#endif

bool    gjs_environment_variable_is_set   (const char *env_variable_name);

char** gjs_g_strv_concat(char*** strv_array, int len);

/*
 * LogFile:
 * RAII class encapsulating access to a FILE* pointer that must be closed,
 * unless it is an already-open fallback file such as stdout or stderr.
 */
class LogFile {
    FILE* m_fp;
    const char* m_errmsg;
    bool m_should_close : 1;

    LogFile(const LogFile&) = delete;
    LogFile& operator=(const LogFile&) = delete;

 public:
    explicit LogFile(const char* filename, FILE* fallback_fp = stdout)
        : m_errmsg(nullptr), m_should_close(false) {
        if (filename) {
            m_fp = fopen(filename, "a");
            if (!m_fp)
                m_errmsg = strerror(errno);
            else
                m_should_close = true;
        } else {
            m_fp = fallback_fp;
        }
    }

    ~LogFile() {
        if (m_should_close)
            fclose(m_fp);
    }

    FILE* fp() { return m_fp; }
    bool has_error() { return !!m_errmsg; }
    const char* errmsg() { return m_errmsg; }
};

#endif  // UTIL_MISC_H_
