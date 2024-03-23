/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
 * SPDX-FileCopyrightText: 2023 Philip Chimento <philip.chimento@gmail.com>
 */

#pragma once

#include <stdint.h>
#include <sys/types.h> /* for ssize_t */

#include <glib-object.h>
#include <glib.h>

#include "gjs/macros.h"

G_BEGIN_DECLS

/**
 * GjsMatchInfo:
 *
 * A GjsMatchInfo is an opaque struct used to return information about
 * matches.
 */
typedef struct _GjsMatchInfo GjsMatchInfo;

/**
 * GJS_TYPE_MATCH_INFO:
 *
 * The #GType for a boxed type holding a #GjsMatchInfo reference.
 */
#define GJS_TYPE_MATCH_INFO (gjs_match_info_get_type())

GJS_EXPORT
GType gjs_match_info_get_type(void) G_GNUC_CONST;

GJS_EXPORT
GRegex* gjs_match_info_get_regex(const GjsMatchInfo* self);
GJS_EXPORT
const char* gjs_match_info_get_string(const GjsMatchInfo* self);

GJS_EXPORT
GjsMatchInfo* gjs_match_info_ref(GjsMatchInfo* self);
GJS_EXPORT
void gjs_match_info_unref(GjsMatchInfo* self);
GJS_EXPORT
void gjs_match_info_free(GjsMatchInfo* self);
GJS_EXPORT
gboolean gjs_match_info_next(GjsMatchInfo* self, GError** error);
GJS_EXPORT
gboolean gjs_match_info_matches(const GjsMatchInfo* self);
GJS_EXPORT
int gjs_match_info_get_match_count(const GjsMatchInfo* self);
GJS_EXPORT
gboolean gjs_match_info_is_partial_match(const GjsMatchInfo* self);
GJS_EXPORT
char* gjs_match_info_expand_references(const GjsMatchInfo* self,
                                       const char* string_to_expand,
                                       GError** error);
GJS_EXPORT
char* gjs_match_info_fetch(const GjsMatchInfo* self, int match_num);
GJS_EXPORT
gboolean gjs_match_info_fetch_pos(const GjsMatchInfo* self, int match_num,
                                  int* start_pos, int* end_pos);
GJS_EXPORT
char* gjs_match_info_fetch_named(const GjsMatchInfo* self, const char* name);
GJS_EXPORT
gboolean gjs_match_info_fetch_named_pos(const GjsMatchInfo* self,
                                        const char* name, int* start_pos,
                                        int* end_pos);
GJS_EXPORT
char** gjs_match_info_fetch_all(const GjsMatchInfo* self);

GJS_EXPORT
gboolean gjs_regex_match(const GRegex* regex, const char* s,
                         GRegexMatchFlags match_options,
                         GjsMatchInfo** match_info);
GJS_EXPORT
gboolean gjs_regex_match_full(const GRegex* regex, const uint8_t* bytes,
                              ssize_t len, int start_position,
                              GRegexMatchFlags match_options,
                              GjsMatchInfo** match_info, GError** error);
GJS_EXPORT
gboolean gjs_regex_match_all(const GRegex* regex, const char* s,
                             GRegexMatchFlags match_options,
                             GjsMatchInfo** match_info);
GJS_EXPORT
gboolean gjs_regex_match_all_full(const GRegex* regex, const uint8_t* bytes,
                                  ssize_t len, int start_position,
                                  GRegexMatchFlags match_options,
                                  GjsMatchInfo** match_info, GError** error);

G_END_DECLS
