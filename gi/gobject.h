/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2018 Philip Chimento <philip.chimento@gmail.com>

#ifndef GI_GOBJECT_H_
#define GI_GOBJECT_H_

#include <config.h>

#include <vector>

#include <glib-object.h>

#include "gjs/auto.h"

using AutoParamArray = std::vector<Gjs::AutoParam>;

extern const GTypeInfo gjs_gobject_class_info;
extern const GTypeInfo gjs_gobject_interface_info;

void push_class_init_properties(GType gtype, AutoParamArray* params);
bool pop_class_init_properties(GType gtype, AutoParamArray* params_out);

#endif  // GI_GOBJECT_H_
