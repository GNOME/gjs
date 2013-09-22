/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * Copyright (c) 2012 Giovanni Campagna <scampa.giovanni@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef GJS_TYPE_MODULE_H
#define GJS_TYPE_MODULE_H

#include <glib-object.h>

typedef struct _GjsTypeModule GjsTypeModule;
typedef struct _GjsTypeModuleClass GjsTypeModuleClass;

#define GJS_TYPE_TYPE_MODULE              (gjs_type_module_get_type ())
#define GJS_TYPE_MODULE(module)           (G_TYPE_CHECK_INSTANCE_CAST ((module), GJS_TYPE_TYPE_MODULE, GjsTypeModule))
#define GJS_TYPE_MODULE_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GJS_TYPE_TYPE_MODULE, GjsTypeModuleClass))
#define GJS_IS_TYPE_MODULE(module)        (G_TYPE_CHECK_INSTANCE_TYPE ((module), GJS_TYPE_TYPE_MODULE))
#define GJS_IS_TYPE_MODULE_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GJS_TYPE_TYPE_MODULE))
#define GJS_TYPE_MODULE_GET_CLASS(module) (G_TYPE_INSTANCE_GET_CLASS ((module), GJS_TYPE_TYPE_MODULE, GjsTypeModuleClass))

GType gjs_type_module_get_type (void) G_GNUC_CONST;

GjsTypeModule *gjs_type_module_get (void);

#endif
