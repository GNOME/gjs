/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC
// SPDX-FileCopyrightText: 2021 Canonical, Ltd

#ifndef GJS_MEM_PRIVATE_H_
#define GJS_MEM_PRIVATE_H_

#include <config.h>

#include <stddef.h>  // for size_t

#include <atomic>

// clang-format off
#define GJS_FOR_EACH_COUNTER(macro) \
    macro(boxed_instance, 0)        \
    macro(boxed_prototype, 1)       \
    macro(closure, 2)               \
    macro(function, 3)              \
    macro(fundamental_instance, 4)  \
    macro(fundamental_prototype, 5) \
    macro(gerror_instance, 6)       \
    macro(gerror_prototype, 7)      \
    macro(interface, 8)             \
    macro(module, 9)                \
    macro(ns, 10)                   \
    macro(object_instance, 11)      \
    macro(object_prototype, 12)     \
    macro(param, 13)                \
    macro(union_instance, 14)       \
    macro(union_prototype, 15)
// clang-format on

namespace Gjs {
namespace Memory {

struct Counter {
    explicit Counter(const char* n) : name(n) {}
    std::atomic_int64_t value = ATOMIC_VAR_INIT(0);
    const char* name;
};

namespace Counters {
#define GJS_DECLARE_COUNTER(name, ix) extern Counter name;
GJS_DECLARE_COUNTER(everything, -1)
GJS_FOR_EACH_COUNTER(GJS_DECLARE_COUNTER)
#undef GJS_DECLARE_COUNTER

template <Counter* counter>
constexpr void inc() {
    everything.value++;
    counter->value++;
}

template <Counter* counter>
constexpr void dec() {
    counter->value--;
    everything.value--;
}

}  // namespace Counters
}  // namespace Memory
}  // namespace Gjs

#define COUNT(name, ix) +1
static constexpr size_t GJS_N_COUNTERS = 0 GJS_FOR_EACH_COUNTER(COUNT);
#undef COUNT

static constexpr const char GJS_COUNTER_DESCRIPTIONS[GJS_N_COUNTERS][52] = {
    // max length of description string ---------------v
    "Number of boxed type wrapper objects",
    "Number of boxed type prototype objects",
    "Number of signal handlers",
    "Number of introspected functions",
    "Number of fundamental type wrapper objects",
    "Number of fundamental type prototype objects",
    "Number of GError wrapper objects",
    "Number of GError prototype objects",
    "Number of GObject interface objects",
    "Number of modules",
    "Number of GI namespace objects",
    "Number of GObject wrapper objects",
    "Number of GObject prototype objects",
    "Number of GParamSpec wrapper objects",
    "Number of C union wrapper objects",
    "Number of C union prototype objects",
};

#define GJS_INC_COUNTER(name) \
    (Gjs::Memory::Counters::inc<&Gjs::Memory::Counters::name>());

#define GJS_DEC_COUNTER(name) \
    (Gjs::Memory::Counters::dec<&Gjs::Memory::Counters::name>());

#define GJS_GET_COUNTER(name) (Gjs::Memory::Counters::name.value.load())

#endif  // GJS_MEM_PRIVATE_H_
