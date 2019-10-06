/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/*
 * Copyright (c) 2017  Philip Chimento <philip.chimento@gmail.com>
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

#ifndef GJS_MODULE_H_
#define GJS_MODULE_H_

#include <gio/gio.h>

#include "gjs/jsapi-wrapper.h"

#include "gjs/macros.h"

class GjsModule {
    JS::Heap<JSObject*> m_module_record;
    std::string m_module_uri;

 public:
    void set_module_record(JS::Handle<JSObject*> module_record);

    JSObject* module_record() const { return m_module_record; }

    void set_module_uri(std::string uri) { m_module_uri = uri; }

    std::string module_uri() { return m_module_uri; }
};

bool gjs_populate_module_meta(JSContext* m_cx,
                              JS::Handle<JS::Value> private_ref,
                              JS::Handle<JSObject*> meta_object);

bool gjs_load_internal_module(JSContext* js_context, unsigned argc,
                              JS::Value* vp);

bool gjs_load_gi_module(JSContext* js_context, unsigned argc, JS::Value* vp);

class GjsModuleLoader {
    // For all external files (e.g. ../main.js).
    ModuleTable* m_id_to_esm_module;
    // For all internal files. (e.g. resource://.../main.js)
    ModuleTable* m_id_to_internal_module;
    // For all gi-loaded modules.
    ModuleTable* m_id_to_gi_module;
    // For all "native" (cpp) modules.
    ModuleTable* m_id_to_native_module;

    bool register_internal_module(JSContext* m_cx, const char* identifier,
                                  const char* resource_path, GError** error);

    bool register_gi_module(JSContext* m_cx, const char* ns,
                            const char* mod_text, size_t mod_len);

 public:
    static bool populate_module_meta(JSContext* m_cx,
                                     JS::Handle<JS::Value> private_ref,
                                     JS::Handle<JSObject*> meta_object);

    GjsModuleLoader() {
        m_id_to_esm_module = new ModuleTable();
        m_id_to_internal_module = new ModuleTable();
        m_id_to_native_module = new ModuleTable();
        m_id_to_gi_module = new ModuleTable();
    }

    GJS_JSAPI_RETURN_CONVENTION
    bool load_internal_module(JSContext* m_cx, unsigned argc, JS::Value* vp);

    GJS_JSAPI_RETURN_CONVENTION
    bool load_gi_module(JSContext* m_cx, unsigned argc, JS::Value* vp);

    bool register_module(JSContext* m_cx, const char* identifier,
                         const char* filename, const char* mod_text,
                         size_t mod_len, GError** error);

    bool register_esm_module(JSContext* m_cx, const char* identifier,
                             const char* filename, const char* mod_text,
                             size_t mod_len);

    JSObject* module_resolve(JSContext* cx, JS::HandleValue mod_val,
                             JS::HandleString specifier);

    JSObject* lookup_module(const char* identifier);
};

GJS_JSAPI_RETURN_CONVENTION
JSObject* gjs_module_import(JSContext* cx, JS::HandleObject importer,
                            JS::HandleId id, const char* name, GFile* file);

JSObject* gjs_module_resolve(JSContext* cx, JS::HandleValue mod_val,
                             JS::HandleString specifier);

#endif  // GJS_MODULE_H_
