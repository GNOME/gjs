/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2013 Red Hat, Inc.

#include <config.h>  // for ENABLE_CAIRO

#include "gjs/native.h"
#include "modules/console.h"
#include "modules/modules.h"
#include "modules/print.h"
#include "modules/system.h"

#ifdef ENABLE_CAIRO
#    include "modules/cairo-module.h"
#endif

void
gjs_register_static_modules (void)
{
#ifdef ENABLE_CAIRO
    gjs_register_native_module("cairoNative", gjs_js_define_cairo_stuff);
#endif
    gjs_register_native_module("system", gjs_js_define_system_stuff);
    gjs_register_native_module("console", gjs_define_console_stuff);
    gjs_register_native_module("_print", gjs_define_print_stuff);
}
