// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2021 Evan Welsh <contact@evanwelsh.com>

#include <config.h>

#include <stdio.h>

#ifdef HAVE_UNISTD_H
#    include <unistd.h>
#elif defined(_WIN32)
#    include <io.h>
#endif

#ifdef HAVE_TERMIOS_H
#    include <stdlib.h>
#    include <termios.h>
#endif

#include <glib.h>

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
