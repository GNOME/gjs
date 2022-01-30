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

namespace Gjs {
namespace Console {

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

#ifdef HAVE_TERMIOS_H
struct termios saved_termios;
#endif

bool disable_raw_mode() {
#ifdef HAVE_TERMIOS_H
    return tcsetattr(stdin_fd, TCSAFLUSH, &saved_termios) != -1;
#else
    return false;
#endif
}

void _disable_raw_mode() {
    void* _ [[maybe_unused]] = reinterpret_cast<void*>(disable_raw_mode());
}

bool enable_raw_mode() {
#ifdef HAVE_TERMIOS_H
    // Save the current terminal flags to reset later
    if (tcgetattr(stdin_fd, &saved_termios) == -1) {
        if (disable_raw_mode())
            return false;

        return false;
    }

    // Register an exit handler to restore
    // the terminal modes on exit.
    atexit(_disable_raw_mode);

    struct termios raw = saved_termios;
    // - Disable \r to \n conversion on input
    // - Disable parity checking
    // - Disable stripping characters to 7-bits
    // - Disable START/STOP characters
    // https://www.gnu.org/software/libc/manual/html_node/Input-Modes.html
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    // Enforce 8-bit characters
    // https://www.gnu.org/software/libc/manual/html_node/Control-Modes.html
    raw.c_cflag |= (CS8);
    // Disable echoing (terminal reprinting input)
    // Disable canonical mode (output reflects input)
    // Disable "extensions" that allow users to inject
    // Disable C signal handling
    // https://www.gnu.org/software/libc/manual/html_node/Other-Special.html
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    // Set 0 characters required for a read
    raw.c_cc[VMIN] = 0;
    // Set the read timeout to 1 decisecond (0.1 seconds)
    raw.c_cc[VTIME] = 1;

    return tcsetattr(stdin_fd, TCSAFLUSH, &raw) != -1;
#else
    return false;
#endif
}

bool is_tty(int fd) {
#ifdef HAVE_UNISTD_H
    return isatty(fd);
#elif defined(_WIN32)
    return _isatty(fd);
#else
    return false;
#endif
}

bool clear() {
    if (stdout_fd < 0 || !g_log_writer_supports_color(stdout_fd))
        return false;

    return fputs(ANSICode::CLEAR_SCREEN, stdout) > 0 && fflush(stdout) > 0;
}

}  // namespace Console
}  // namespace Gjs
