/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
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

#ifndef GI_UNION_H_
#define GI_UNION_H_

#include <girepository.h>
#include <glib-object.h>

#include "gjs/jsapi-wrapper.h"

#include "gi/wrapperutils.h"
#include "gjs/macros.h"
#include "util/log.h"

class UnionPrototype;
class UnionInstance;

class UnionBase
    : public GIWrapperBase<UnionBase, UnionPrototype, UnionInstance> {
    friend class GIWrapperBase<UnionBase, UnionPrototype, UnionInstance>;

 protected:
    explicit UnionBase(UnionPrototype* proto = nullptr)
        : GIWrapperBase(proto) {}
    ~UnionBase(void) {}

    static const GjsDebugTopic debug_topic = GJS_DEBUG_GBOXED;
    static constexpr const char* debug_tag = "union";

    static const JSClassOps class_ops;
    static const JSClass klass;

    GJS_USE static const char* to_string_kind(void) { return "union"; }
};

class UnionPrototype : public GIWrapperPrototype<UnionBase, UnionPrototype,
                                                 UnionInstance, GIUnionInfo> {
    friend class GIWrapperPrototype<UnionBase, UnionPrototype, UnionInstance,
                                    GIUnionInfo>;
    friend class GIWrapperBase<UnionBase, UnionPrototype, UnionInstance>;

    static constexpr InfoType::Tag info_type_tag = InfoType::Union;

    explicit UnionPrototype(GIUnionInfo* info, GType gtype);
    ~UnionPrototype(void);

    GJS_JSAPI_RETURN_CONVENTION
    bool resolve_impl(JSContext* cx, JS::HandleObject obj, JS::HandleId id,
                      const char* prop_name, bool* resolved);

    // Overrides GIWrapperPrototype::constructor_nargs().
    GJS_USE unsigned constructor_nargs(void) const { return 0; }
};

class UnionInstance
    : public GIWrapperInstance<UnionBase, UnionPrototype, UnionInstance> {
    friend class GIWrapperInstance<UnionBase, UnionPrototype, UnionInstance>;
    friend class GIWrapperBase<UnionBase, UnionPrototype, UnionInstance>;

    explicit UnionInstance(JSContext* cx, JS::HandleObject obj);
    ~UnionInstance(void);

    GJS_JSAPI_RETURN_CONVENTION
    bool constructor_impl(JSContext* cx, JS::HandleObject obj,
                          const JS::CallArgs& args);

 public:
    /*
     * UnionInstance::copy_union:
     *
     * Allocate a new union pointer using g_boxed_copy(), from a raw union
     * pointer.
     */
    void copy_union(void* ptr) { m_ptr = g_boxed_copy(gtype(), ptr); }

    GJS_JSAPI_RETURN_CONVENTION
    static void* copy_ptr(JSContext* cx, GType gtype, void* ptr);
};

GJS_JSAPI_RETURN_CONVENTION
bool gjs_define_union_class(JSContext       *context,
                            JS::HandleObject in_object,
                            GIUnionInfo     *info);

GJS_JSAPI_RETURN_CONVENTION
JSObject* gjs_union_from_c_union       (JSContext    *context,
                                        GIUnionInfo  *info,
                                        void         *gboxed);

#endif  // GI_UNION_H_
