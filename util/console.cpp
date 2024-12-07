// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2021 Evan Welsh <contact@evanwelsh.com>

#include <config.h>

#include <stdio.h>

#ifdef HAVE_UNISTD_H
#    include <unistd.h>
#elif defined(_WIN32)
#    include <io.h>
#endif

#ifdef HAVE_READLINE_READLINE_H
#    include <readline/history.h>
#endif

#include <glib.h>

#include "gjs/auto.h"
#include "util/console.h"

/**
 * ANSI escape code sequences to manipulate terminals.
 *
 * See
 * https://en.wikipedia.org/wiki/ANSI_escape_code#CSI_(Control_Sequence_Introducer)_sequences
 */
namespace ANSICode {
/**
 * ANSI escape code sequence to clear the terminal screen.
 *
 * Combination of 0x1B (Escape) and the sequence nJ where n=2,
 * n=2 clears the entire display instead of only after the cursor.
 */
constexpr const char CLEAR_SCREEN[] = "\x1b[2J";

}  // namespace ANSICode

#ifdef HAVE_UNISTD_H
const int stdin_fd = STDIN_FILENO;
const int stdout_fd = STDOUT_FILENO;
const int stderr_fd = STDERR_FILENO;
#elif defined(_WIN32)
const int stdin_fd = _fileno(stdin);
const int stdout_fd = _fileno(stdout);
const int stderr_fd = _fileno(stderr);
#else
const int stdin_fd = 0;
const int stdout_fd = 1;
const int stderr_fd = 2;
#endif

bool gjs_console_is_tty(int fd) {
#ifdef HAVE_UNISTD_H
    return isatty(fd);
#elif defined(_WIN32)
    return _isatty(fd);
#else
    return false;
#endif
}

bool gjs_console_clear() {
    if (stdout_fd < 0 || !g_log_writer_supports_color(stdout_fd))
        return false;

    return fputs(ANSICode::CLEAR_SCREEN, stdout) > 0 && fflush(stdout) > 0;
}

#ifdef HAVE_READLINE_READLINE_H
char* gjs_console_get_repl_history_path() {
    const char* user_history_path = g_getenv("GJS_REPL_HISTORY");
    Gjs::AutoChar default_history_path =
        g_build_filename(g_get_user_cache_dir(), "gjs_repl_history", nullptr);
    bool is_write_history_disabled =
        user_history_path && user_history_path[0] == '\0';
    if (is_write_history_disabled)
        return nullptr;

    if (user_history_path)
        return g_strdup(user_history_path);
    return default_history_path.release();
}

void gjs_console_write_repl_history(const char* path) {
    if (path) {
        int err = write_history(path);
        if (err != 0)
            g_warning("Could not persist history to defined file %s: %s", path,
                      g_strerror(err));
    }
}
#endif
