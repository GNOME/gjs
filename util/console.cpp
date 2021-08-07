// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2021 Evan Welsh <contact@evanwelsh.com>

#include <config.h>

#ifdef HAVE_UNISTD_H
#    include <unistd.h>
#elif defined(_WIN32)
#    include <io.h>
#endif

#include "util/console.h"

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
