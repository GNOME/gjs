/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
 * SPDX-FileCopyrightText: 2023 Philip Chimento <philip.chimento@gmail.com>
 */

#include <config.h>

#include <stdbool.h>
#include <stddef.h>    /* for NULL */
#include <stdint.h>
#include <sys/types.h> /* for ssize_t */

#include <glib-object.h>
#include <glib.h>

#include "libgjs-private/gjs-match-info.h"

G_DEFINE_BOXED_TYPE(GjsMatchInfo, gjs_match_info, gjs_match_info_ref,
                    gjs_match_info_unref)

struct _GjsMatchInfo {
    gatomicrefcount refcount;
    GMatchInfo* base; /* owned */
    char* str;
};

/* Takes ownership of string */
static GjsMatchInfo* new_match_info(GMatchInfo* base, char* s) {
    GjsMatchInfo* retval = g_new0(GjsMatchInfo, 1);
    g_atomic_ref_count_init(&retval->refcount);
    retval->base = base;
    retval->str = s;
    return retval;
}

/**
 * gjs_match_info_get_regex:
 * @self: a #GjsMatchInfo
 *
 * Wrapper for g_match_info_get_regex().
 *
 * Returns: (transfer none): #GRegex object
 */
GRegex* gjs_match_info_get_regex(const GjsMatchInfo* self) {
    g_return_val_if_fail(self != NULL, NULL);
    return g_match_info_get_regex(self->base);
}

/**
 * gjs_match_info_get_string:
 * @self: a #GjsMatchInfo
 *
 * Replacement for g_match_info_get_string(), but the string is owned by @self.
 *
 * Returns: (transfer none): the string searched with @match_info
 */
const char* gjs_match_info_get_string(const GjsMatchInfo* self) {
    g_return_val_if_fail(self != NULL, NULL);
    return self->str;
}

/**
 * gjs_match_info_ref:
 * @self: a #GjsMatchInfo
 *
 * Replacement for g_match_info_ref().
 *
 * Returns: @self
 */
GjsMatchInfo* gjs_match_info_ref(GjsMatchInfo* self) {
    g_return_val_if_fail(self != NULL, NULL);
    g_atomic_ref_count_inc(&self->refcount);
    return self;
}

/**
 * gjs_match_info_unref:
 * @self: a #GjsMatchInfo
 *
 * Replacement for g_match_info_unref().
 */
void gjs_match_info_unref(GjsMatchInfo* self) {
    g_return_if_fail(self != NULL);
    if (g_atomic_ref_count_dec(&self->refcount)) {
        g_match_info_unref(self->base);
        g_free(self->str);
        g_free(self);
    }
}

/**
 * gjs_match_info_free:
 * @self: (nullable): a #GjsMatchInfo, or %NULL
 *
 * Replacement for g_match_info_free().
 */
void gjs_match_info_free(GjsMatchInfo* self) {
    g_return_if_fail(self != NULL);
    if (self == NULL)
        return;

    gjs_match_info_unref(self);
}

/**
 * gjs_match_info_next:
 * @self: a #GjsMatchInfo
 * @error: location to store the error occurring, or %NULL to ignore errors
 *
 * Wrapper for g_match_info_next().
 *
 * Returns: %TRUE or %FALSE
 */
gboolean gjs_match_info_next(GjsMatchInfo* self, GError** error) {
    g_return_val_if_fail(self != NULL, FALSE);
    return g_match_info_next(self->base, error);
}

/**
 * gjs_match_info_matches:
 * @self: a #GjsMatchInfo
 *
 * Wrapper for g_match_info_matches().
 *
 * Returns: %TRUE or %FALSE
 */
gboolean gjs_match_info_matches(const GjsMatchInfo* self) {
    g_return_val_if_fail(self != NULL, FALSE);
    return g_match_info_matches(self->base);
}

/**
 * gjs_match_info_get_match_count:
 * @self: a #GjsMatchInfo
 *
 * Wrapper for g_match_info_get_match_count().
 *
 * Returns: Number of matched substrings, or -1 if an error occurred
 */
int gjs_match_info_get_match_count(const GjsMatchInfo* self) {
    g_return_val_if_fail(self != NULL, -1);
    return g_match_info_get_match_count(self->base);
}

/**
 * gjs_match_info_is_partial_match:
 * @self: a #GjsMatchInfo
 *
 * Wrapper for g_match_info_is_partial_match().
 *
 * Returns: %TRUE or %FALSE
 */
gboolean gjs_match_info_is_partial_match(const GjsMatchInfo* self) {
    g_return_val_if_fail(self != NULL, FALSE);
    return g_match_info_is_partial_match(self->base);
}

/**
 * gjs_match_info_expand_references:
 * @self: (nullable): a #GjsMatchInfo or %NULL
 * @string_to_expand: the string to expand
 * @error: location to store the error occurring, or %NULL to ignore errors
 *
 * Wrapper for g_match_info_expand_references().
 *
 * Returns: (nullable): the expanded string, or %NULL if an error occurred
 */
char* gjs_match_info_expand_references(const GjsMatchInfo* self,
                                       const char* string_to_expand,
                                       GError** error) {
    return g_match_info_expand_references(self->base, string_to_expand, error);
}

/**
 * gjs_match_info_fetch:
 * @self: a #GjsMatchInfo
 * @match_num: number of the sub expression
 *
 * Wrapper for g_match_info_fetch().
 *
 * Returns: (nullable): The matched substring, or %NULL if an error occurred.
 */
char* gjs_match_info_fetch(const GjsMatchInfo* self, int match_num) {
    g_return_val_if_fail(self != NULL, NULL);
    return g_match_info_fetch(self->base, match_num);
}

/**
 * gjs_match_info_fetch_pos:
 * @self: a #GMatchInfo
 * @match_num: number of the sub expression
 * @start_pos: (out) (optional): pointer to location for the start position
 * @end_pos: (out) (optional): pointer to location for the end position
 *
 * Wrapper for g_match_info_fetch_pos().
 *
 * Returns: %TRUE or %FALSE
 */
gboolean gjs_match_info_fetch_pos(const GjsMatchInfo* self, int match_num,
                                  int* start_pos, int* end_pos) {
    g_return_val_if_fail(self != NULL, FALSE);
    return g_match_info_fetch_pos(self->base, match_num, start_pos, end_pos);
}

/**
 * gjs_match_info_fetch_named:
 * @self: a #GjsMatchInfo
 * @name: name of the subexpression
 *
 * Wrapper for g_match_info_fetch_named().
 *
 * Returns: (nullable): The matched substring, or %NULL if an error occurred.
 */
char* gjs_match_info_fetch_named(const GjsMatchInfo* self, const char* name) {
    g_return_val_if_fail(self != NULL, NULL);
    return g_match_info_fetch_named(self->base, name);
}

/**
 * gjs_match_info_fetch_named_pos:
 * @self: a #GMatchInfo
 * @name: name of the subexpression
 * @start_pos: (out) (optional): pointer to location for the start position
 * @end_pos: (out) (optional): pointer to location for the end position
 *
 * Wrapper for g_match_info_fetch_named_pos().
 *
 * Returns: %TRUE or %FALSE
 */
gboolean gjs_match_info_fetch_named_pos(const GjsMatchInfo* self,
                                        const char* name, int* start_pos,
                                        int* end_pos) {
    g_return_val_if_fail(self != NULL, FALSE);
    return g_match_info_fetch_named_pos(self->base, name, start_pos, end_pos);
}

/**
 * gjs_match_info_fetch_all:
 * @self: a #GMatchInfo
 *
 * Wrapper for g_match_info_fetch_all().
 *
 * Returns: (transfer full): a %NULL-terminated array of strings. If the
 *     previous match failed %NULL is returned
 */
char** gjs_match_info_fetch_all(const GjsMatchInfo* self) {
    g_return_val_if_fail(self != NULL, NULL);
    return g_match_info_fetch_all(self->base);
}

/**
 * gjs_regex_match:
 * @regex: a #GRegex
 * @s: the string to scan for matches
 * @match_options: match options
 * @match_info: (out) (optional): pointer to location for the #GjsMatchInfo
 *
 * Wrapper for g_regex_match() that doesn't require the string to be kept alive.
 *
 * Returns: %TRUE or %FALSE
 */
gboolean gjs_regex_match(const GRegex* regex, const char* s,
                         GRegexMatchFlags match_options,
                         GjsMatchInfo** match_info) {
    return gjs_regex_match_full(regex, (const uint8_t*)s, -1, 0, match_options,
                                match_info, NULL);
}

/**
 * gjs_regex_match_full:
 * @regex: a #GRegex
 * @bytes: (array length=len): the string to scan for matches
 * @len: the length of @bytes
 * @start_position: starting index of the string to match, in bytes
 * @match_options: match options
 * @match_info: (out) (optional): pointer to location for the #GjsMatchInfo
 * @error: location to store the error occurring, or %NULL to ignore errors
 *
 * Wrapper for g_regex_match_full() that doesn't require the string to be kept
 * alive.
 *
 * Returns: %TRUE or %FALSE
 */
gboolean gjs_regex_match_full(const GRegex* regex, const uint8_t* bytes,
                              ssize_t len, int start_position,
                              GRegexMatchFlags match_options,
                              GjsMatchInfo** match_info, GError** error) {
    const char* s = (const char*)bytes;
    if (match_info == NULL)
        return g_regex_match_full(regex, s, len, start_position, match_options,
                                  NULL, error);

    char* string_copy = len < 0 ? g_strdup(s) : g_strndup(s, len);
    GMatchInfo* base = NULL;
    bool retval = g_regex_match_full(regex, string_copy, len, start_position,
                                     match_options, &base, error);

    if (base)
        *match_info = new_match_info(base, string_copy);

    return retval;
}

/**
 * gjs_regex_match_all:
 * @regex: a #GRegex
 * @s: the string to scan for matches
 * @match_options: match options
 * @match_info: (out) (optional): pointer to location for the #GjsMatchInfo
 *
 * Wrapper for g_regex_match_all() that doesn't require the string to be kept
 * alive.
 *
 * Returns: %TRUE or %FALSE
 */
gboolean gjs_regex_match_all(const GRegex* regex, const char* s,
                             GRegexMatchFlags match_options,
                             GjsMatchInfo** match_info) {
    return gjs_regex_match_all_full(regex, (const uint8_t*)s, -1, 0,
                                    match_options, match_info, NULL);
}

/**
 * gjs_regex_match_all_full:
 * @regex: a #GRegex
 * @bytes: (array length=len): the string to scan for matches
 * @len: the length of @bytes
 * @start_position: starting index of the string to match, in bytes
 * @match_options: match options
 * @match_info: (out) (optional): pointer to location for the #GMatchInfo
 * @error: location to store the error occurring, or %NULL to ignore errors
 *
 * Wrapper for g_regex_match_all_full() that doesn't require the string to be
 * kept alive.
 *
 * Returns: %TRUE or %FALSE
 */
gboolean gjs_regex_match_all_full(const GRegex* regex, const uint8_t* bytes,
                                  ssize_t len, int start_position,
                                  GRegexMatchFlags match_options,
                                  GjsMatchInfo** match_info, GError** error) {
    const char* s = (const char*)bytes;
    if (match_info == NULL)
        return g_regex_match_all_full(regex, s, len, start_position,
                                      match_options, NULL, error);

    char* string_copy = len < 0 ? g_strdup(s) : g_strndup(s, len);
    GMatchInfo* base = NULL;
    bool retval = g_regex_match_all_full(
        regex, string_copy, len, start_position, match_options, &base, error);

    if (base)
        *match_info = new_match_info(base, string_copy);

    return retval;
}
