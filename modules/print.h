/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2020 Evan Welsh <contact@evanwelsh.com>

#pragma once

#include <config.h>

#include <js/TypeDecls.h>

#include "gjs/macros.h"

GJS_JSAPI_RETURN_CONVENTION
bool gjs_define_print_stuff(JSContext*, JS::MutableHandleObject module);
