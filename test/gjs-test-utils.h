/*
 * Copyright © 2013 Endless Mobile, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Authored By: Sam Spilsbury <sam@endlessm.com>
 */
#ifndef GJS_TEST_UTILS_H
#define GJS_TEST_UTILS_H

typedef struct _GjsUnitTestFixture GjsUnitTestFixture;
struct _GjsUnitTestFixture {
    GjsContext *gjs_context;
    JSContext *cx;
    JSCompartment *compartment;
    char *message;  /* Thrown exception message */
};

void gjs_unit_test_fixture_setup(GjsUnitTestFixture *fx,
                                 gconstpointer       unused);

void gjs_unit_test_fixture_teardown(GjsUnitTestFixture *fx,
                                    gconstpointer      unused);

void gjs_test_add_tests_for_coverage ();

void gjs_test_add_tests_for_parse_call_args(void);

#endif
