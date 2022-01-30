// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2021 Evan Welsh <contact@evanwelsh.com>

#ifndef UTIL_CONSOLE_H_
#define UTIL_CONSOLE_H_

#include <config.h>

namespace Gjs {
namespace Console {
extern const int stdout_fd;
extern const int stdin_fd;
extern const int stderr_fd;

[[nodiscard]] bool is_tty(int fd = stdout_fd);

[[nodiscard]] bool clear();

[[nodiscard]] bool enable_raw_mode();

[[nodiscard]] bool disable_raw_mode();

[[nodiscard]] bool get_size(int* width, int* height);

};  // namespace Console
};  // namespace Gjs

#endif  // UTIL_CONSOLE_H_
