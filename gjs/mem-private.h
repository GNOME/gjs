/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC

#ifndef GJS_MEM_PRIVATE_H_
#define GJS_MEM_PRIVATE_H_

#include <glib.h>

typedef struct {
    volatile int value;
    const char* name;
} GjsMemCounter;

// clang-format off
#define GJS_FOR_EACH_COUNTER(macro) \
    macro(boxed_instance)           \
    macro(boxed_prototype)          \
    macro(closure)                  \
    macro(function)                 \
    macro(fundamental_instance)     \
    macro(fundamental_prototype)    \
    macro(gerror_instance)          \
    macro(gerror_prototype)         \
    macro(importer)                 \
    macro(interface)                \
    macro(module)                   \
    macro(ns)                       \
    macro(object_instance)          \
    macro(object_prototype)         \
    macro(param)                    \
    macro(union_instance)           \
    macro(union_prototype)
// clang-format on

#define GJS_DECLARE_COUNTER(name) extern GjsMemCounter gjs_counter_##name;

GJS_DECLARE_COUNTER(everything)
GJS_FOR_EACH_COUNTER(GJS_DECLARE_COUNTER)

#define GJS_INC_COUNTER(name)                               \
    do {                                                    \
        g_atomic_int_add(&gjs_counter_everything.value, 1); \
        g_atomic_int_add(&gjs_counter_##name.value, 1);     \
    } while (0)

#define GJS_DEC_COUNTER(name)                                \
    do {                                                     \
        g_atomic_int_add(&gjs_counter_everything.value, -1); \
        g_atomic_int_add(&gjs_counter_##name.value, -1);     \
    } while (0)

#define GJS_GET_COUNTER(name) g_atomic_int_get(&gjs_counter_##name.value)

#endif  // GJS_MEM_PRIVATE_H_
