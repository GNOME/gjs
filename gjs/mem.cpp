/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 litl, LLC

#include <config.h>

#include <inttypes.h>

#include <girepository/girepository.h>
#include <glib-object.h>
#include <glib.h>

#include <mozilla/CheckedInt.h>
#include <mozilla/Maybe.h>
#include <mozilla/Result.h>
#include <mozilla/Span.h>

#include "gi/arg-inl.h"
#include "gi/info.h"
#include "gjs/mem-private.h"  // IWYU pragma: associated
#include "gjs/mem.h"
#include "util/log.h"

namespace Gjs::Memory::Counters {
#define GJS_DEFINE_COUNTER(name, ix) Counter name(#name);

GJS_DEFINE_COUNTER(everything, -1)
GJS_FOR_EACH_COUNTER(GJS_DEFINE_COUNTER)
}  // namespace Gjs::Memory::Counters

#define GJS_LIST_COUNTER(name, ix) &Gjs::Memory::Counters::name,

static Gjs::Memory::Counter* counters[] = {
    GJS_FOR_EACH_COUNTER(GJS_LIST_COUNTER)};

void gjs_memory_report(const char* where, bool die_if_leaks) {
    gjs_debug(GJS_DEBUG_MEMORY,
              "Memory report: %s",
              where);

    size_t n_counters = G_N_ELEMENTS(counters);

    int64_t total_objects = 0;
    for (size_t i = 0; i < n_counters; ++i) {
        total_objects += counters[i]->value;
    }

    if (total_objects != GJS_GET_COUNTER(everything)) {
        gjs_debug(GJS_DEBUG_MEMORY,
                  "Object counts don't add up!");
    }

    gjs_debug(GJS_DEBUG_MEMORY, "  %" PRId64 " objects currently alive",
              GJS_GET_COUNTER(everything));

    if (GJS_GET_COUNTER(everything) != 0) {
        for (size_t i = 0; i < n_counters; ++i) {
            gjs_debug(GJS_DEBUG_MEMORY, "    %24s = %" PRId64,
                      counters[i]->name, counters[i]->value.load());
        }

        if (die_if_leaks)
            g_error("%s: JavaScript objects were leaked.", where);
    }
}

int32_t estimate_size_of_gdkpixbuf(GObject* wrapped) {
    GI::Repository repo;
    GI::AutoObjectInfo gdkpixbuf_info{
        repo.find_by_name<GI::InfoTag::OBJECT>("GdkPixbuf", "Pixbuf")
        .value()};
    GI::AutoFunctionInfo get_width_func{
        gdkpixbuf_info.method("get_width").value()};
    GI::AutoFunctionInfo get_height_func{
        gdkpixbuf_info.method("get_height").value()};
    GI::AutoFunctionInfo get_bits_per_sample_func{
        gdkpixbuf_info.method("get_bits_per_sample").value()};
    GIArgument args;
    gjs_arg_set(&args, wrapped);
    GIArgument width, height, bits_per_sample;
    auto result_width = get_width_func.invoke({{args}}, {}, &width);
    auto result_height = get_height_func.invoke({{args}}, {}, &height);
    auto result_bits_per_sample =
        get_bits_per_sample_func.invoke({{args}}, {}, &bits_per_sample);
    if (result_width.isErr() || result_height.isErr() ||
        result_bits_per_sample.isErr()) {
        gjs_debug(GJS_DEBUG_MEMORY, "Failed to estimate GdkPixbuf size");
        return 0;
    }
    int width_int = gjs_arg_get<int>(&width);
    int height_int = gjs_arg_get<int>(&height);
    int bits_per_sample_int = gjs_arg_get<int>(&bits_per_sample);
    mozilla::CheckedInt32 estimated_size =
        mozilla::CheckedInt32{width_int} * height_int * bits_per_sample_int;
    if (!estimated_size.isValid())
        return INT32_MAX;
    return estimated_size.value();
}
