/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * Copyright (c) 2008  litl, LLC
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

#ifndef __GJS_NATIVE_H__
#define __GJS_NATIVE_H__

#if !defined (__GJS_GJS_MODULE_H__) && !defined (GJS_COMPILATION)
#error "Only <gjs/gjs-module.h> can be included directly."
#endif

#include <glib.h>

#include <jsapi.h>

G_BEGIN_DECLS

typedef enum {
    /* This means that the GjsDefineModuleFunc defines the module
     * name in the parent module, as opposed to the normal process
     * where the GjsDefineModuleFunc defines module contents. When
     * importing imports.foo.bar, this flag means the native module is
     * given foo and defines bar in it, while normally the native
     * module is given bar and defines stuff in that.
     *
     * The purpose of this is to allow a module with lazy properties
     * by allowing module objects to be custom classes. It's used for
     * the gobject-introspection module for example.
     */
    GJS_NATIVE_SUPPLIES_MODULE_OBJ = 1 << 0

} GjsNativeFlags;

/*
 * In a native module, you define a GjsDefineModuleFunc that
 * adds your stuff to module_obj.
 *
 * You then declare GJS_REGISTER_NATIVE_MODULE("my.module.path", my_module_func)
 *
 * This declaration will call gjs_register_native_module() when your
 * module is dlopen'd. We can't just use a well-known symbol name
 * in your module, because we need to dlopen modules with
 * global symbols.
 */

typedef JSBool (* GjsDefineModuleFunc) (JSContext *context,
                                        JSObject  *module_obj);

#define GJS_REGISTER_NATIVE_MODULE_WITH_FLAGS(module_id_string, module_func, flags) \
    __attribute__((constructor)) static void                                 \
    register_native_module (void)                                            \
    {                                                                        \
        gjs_register_native_module(module_id_string, module_func, flags); \
    }


#define GJS_REGISTER_NATIVE_MODULE(module_id_string, module_func)           \
    GJS_REGISTER_NATIVE_MODULE_WITH_FLAGS(module_id_string, module_func, 0)

/* called in constructor function on dlopen() load */
void   gjs_register_native_module (const char            *module_id,
                                   GjsDefineModuleFunc  func,
                                   GjsNativeFlags       flags);

/* called by importer.c to to check for already loaded modules */
gboolean gjs_is_registered_native_module(JSContext  *context,
                                         JSObject   *parent,
                                         const char *name);

/* called by importer.c to load a native module once it finds
 * it in the search path
 */
JSBool gjs_import_native_module   (JSContext             *context,
                                   JSObject              *module_obj,
                                   const char            *filename,
                                   GjsNativeFlags      *flags_p);


G_END_DECLS

#endif  /* __GJS_NATIVE_H__ */
