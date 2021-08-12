/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2021 Canonical Ltd.
// SPDX-FileContributor: Authored by: Marco Trevisan <marco.trevisan@canonical.com>

#include "config.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <deque>
#include <forward_list>
#include <functional>
#include <limits>
#include <memory>
#include <new>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <time.h>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <ffi.h>
#include <gio/gio.h>
#include <girepository.h>
#include <girffi.h>
#include <gjs/context.h>
#include <gjs/coverage.h>
#include <gjs/error-types.h>
#include <gjs/gjs.h>
#include <gjs/jsapi-util.h>
#include <gjs/macros.h>
#include <gjs/mem.h>
#include <gjs/profiler.h>
#include <glib-object.h>
#include <glib.h>
#ifdef G_OS_UNIX
#include <glib-unix.h>
#endif
#include <inttypes.h>
#include <iomanip>
#include <js/RequiredDefines.h>  // check-pch: ignore, part of mozjs cflags
#include <js/AllocPolicy.h>
#include <js/Array.h>
#include <js/ArrayBuffer.h>
#include <js/BigInt.h>
#include <js/CallArgs.h>
#include <js/CharacterEncoding.h>
#include <js/Class.h>
#include <js/ComparisonOperators.h>
#include <js/CompilationAndEvaluation.h>
#include <js/CompileOptions.h>
#include <js/ContextOptions.h>
#include <js/Conversions.h>
#include <js/ErrorReport.h>
#include <js/Exception.h>
#include <js/GCAPI.h>
#include <js/GCHashTable.h>
#include <js/GCPolicyAPI.h>
#include <js/GCVector.h>
#include <js/HashTable.h>
#include <js/HeapAPI.h>
#include <js/Id.h>
#include <js/Initialization.h>
#include <js/MemoryFunctions.h>
#include <js/Modules.h>
#include <js/ProfilingCategory.h>
#include <js/ProfilingStack.h>
#include <js/Promise.h>
#include <js/PropertyDescriptor.h>
#include <js/PropertySpec.h>
#include <js/Realm.h>
#include <js/RealmOptions.h>
#include <js/RootingAPI.h>
#include <js/SavedFrameAPI.h>
#include <js/SourceText.h>
#include <js/Symbol.h>
#include <js/TracingAPI.h>
#include <js/TypeDecls.h>
#include <js/UniquePtr.h>
#include <js/Utility.h>
#include <js/Value.h>
#include <js/ValueArray.h>
#include <js/Warnings.h>
#include <js/Wrapper.h>
#include <js/experimental/CodeCoverage.h>
#include <js/experimental/SourceHook.h>
#include <jsapi.h>
#include <jsfriendapi.h>
#include <jspubtd.h>
#include <locale.h>
#include <mozilla/Atomics.h>
#include <mozilla/CheckedInt.h>
#include <mozilla/HashFunctions.h>
#include <mozilla/HashTable.h>
#include <mozilla/Likely.h>
#include <mozilla/UniquePtr.h>
#include <mozilla/Unused.h>
#ifdef HAVE_READLINE_READLINE_H
#include <readline/history.h>
#include <readline/readline.h>
#endif
#ifndef _WIN32
#include <signal.h>
#endif
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#ifdef ENABLE_PROFILER
#include <alloca.h>
#include <syscall.h>
#include <sysprof-capture.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef _WIN32
#include <windows.h>
#    include <io.h>
# include <process.h>
#endif
