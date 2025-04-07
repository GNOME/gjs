/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2010 litl, LLC.

#include <config.h>

#include <stddef.h>  // for size_t

#include <memory>

#include <cairo.h>
#include <girepository/girepository.h>
#include <glib.h>

#include <js/Array.h>  // for JS::NewArrayObject
#include <js/CallArgs.h>
#include <js/Conversions.h>
#include <js/PropertyAndElement.h>
#include <js/PropertyDescriptor.h>  // for JSPROP_READONLY
#include <js/PropertySpec.h>
#include <js/RootingAPI.h>
#include <js/TypeDecls.h>
#include <js/Utility.h>  // for UniqueChars
#include <js/Value.h>
#include <jsapi.h>  // for JS_NewPlainObject

#include "gi/arg-inl.h"
#include "gi/arg.h"
#include "gi/foreign.h"
#include "gjs/auto.h"
#include "gjs/enum-utils.h"
#include "gjs/jsapi-util-args.h"
#include "gjs/jsapi-util.h"
#include "gjs/macros.h"
#include "modules/cairo-private.h"

#define _GJS_CAIRO_CONTEXT_GET_PRIV_CR_CHECKED(cx, argc, vp, argv, obj) \
    GJS_GET_THIS(cx, argc, vp, argv, obj);                              \
    cairo_t* cr;                                                        \
    if (!CairoContext::for_js_typecheck(cx, obj, &cr, &argv))           \
        return false;                                                   \
    if (!cr)                                                            \
        return true;

#define _GJS_CAIRO_CONTEXT_DEFINE_FUNC_BEGIN(mname)             \
    GJS_JSAPI_RETURN_CONVENTION                                 \
    static bool mname##_func(JSContext* context, unsigned argc, \
                             JS::Value* vp) {                   \
        _GJS_CAIRO_CONTEXT_GET_PRIV_CR_CHECKED(context, argc, vp, argv, obj)

#define _GJS_CAIRO_CONTEXT_DEFINE_FUNC_END                               \
    return gjs_cairo_check_status(context, cairo_status(cr), "context"); \
}

#define _GJS_CAIRO_CONTEXT_CHECK_NO_ARGS(m)                        \
    if (argc > 0) {                                                \
        gjs_throw(context, "Context." #m "() takes no arguments"); \
        return false;                                              \
    }

#define _GJS_CAIRO_CONTEXT_DEFINE_FUNC0(method, cfunc)                     \
_GJS_CAIRO_CONTEXT_DEFINE_FUNC_BEGIN(method)                               \
    cfunc(cr);                                                             \
    argv.rval().setUndefined();                                            \
_GJS_CAIRO_CONTEXT_DEFINE_FUNC_END

#define _GJS_CAIRO_CONTEXT_DEFINE_FUNC0I(method, cfunc)                    \
_GJS_CAIRO_CONTEXT_DEFINE_FUNC_BEGIN(method)                               \
    int ret;                                                               \
   _GJS_CAIRO_CONTEXT_CHECK_NO_ARGS(method)                                \
    ret = (int)cfunc(cr);                                                  \
    argv.rval().setInt32(ret);                                             \
_GJS_CAIRO_CONTEXT_DEFINE_FUNC_END

#define _GJS_CAIRO_CONTEXT_DEFINE_FUNC0B(method, cfunc)                    \
_GJS_CAIRO_CONTEXT_DEFINE_FUNC_BEGIN(method)                               \
    cairo_bool_t ret;                                                      \
   _GJS_CAIRO_CONTEXT_CHECK_NO_ARGS(method)                                \
    ret = cfunc(cr);                                                       \
    argv.rval().setBoolean(ret);                                           \
_GJS_CAIRO_CONTEXT_DEFINE_FUNC_END

#define _GJS_CAIRO_CONTEXT_DEFINE_FUNC2FFAFF(method, cfunc, n1, n2)         \
    _GJS_CAIRO_CONTEXT_DEFINE_FUNC_BEGIN(method)                            \
    double arg1, arg2;                                                      \
    if (!gjs_parse_call_args(context, #method, argv, "ff", #n1, &arg1, #n2, \
                             &arg2))                                        \
        return false;                                                       \
    cfunc(cr, &arg1, &arg2);                                                \
    if (cairo_status(cr) == CAIRO_STATUS_SUCCESS) {                         \
        JS::RootedObject array(context, JS::NewArrayObject(context, 2));    \
        if (!array)                                                         \
            return false;                                                   \
        JS::RootedValue r{context,                                          \
                          JS::NumberValue(JS::CanonicalizeNaN(arg1))};      \
        if (!JS_SetElement(context, array, 0, r))                           \
            return false;                                                   \
        r.setNumber(JS::CanonicalizeNaN(arg2));                             \
        if (!JS_SetElement(context, array, 1, r))                           \
            return false;                                                   \
        argv.rval().setObject(*array);                                      \
    }                                                                       \
    _GJS_CAIRO_CONTEXT_DEFINE_FUNC_END

#define _GJS_CAIRO_CONTEXT_DEFINE_FUNC0AFF(method, cfunc)                \
    _GJS_CAIRO_CONTEXT_DEFINE_FUNC_BEGIN(method)                         \
    double arg1, arg2;                                                   \
    _GJS_CAIRO_CONTEXT_CHECK_NO_ARGS(method)                             \
    cfunc(cr, &arg1, &arg2);                                             \
    if (cairo_status(cr) == CAIRO_STATUS_SUCCESS) {                      \
        JS::RootedObject array(context, JS::NewArrayObject(context, 2)); \
        if (!array)                                                      \
            return false;                                                \
        JS::RootedValue r{context,                                       \
                          JS::NumberValue(JS::CanonicalizeNaN(arg1))};   \
        if (!JS_SetElement(context, array, 0, r))                        \
            return false;                                                \
        r.setNumber(JS::CanonicalizeNaN(arg2));                          \
        if (!JS_SetElement(context, array, 1, r))                        \
            return false;                                                \
        argv.rval().setObject(*array);                                   \
    }                                                                    \
    _GJS_CAIRO_CONTEXT_DEFINE_FUNC_END

#define _GJS_CAIRO_CONTEXT_DEFINE_FUNC0AFFFF(method, cfunc)              \
    _GJS_CAIRO_CONTEXT_DEFINE_FUNC_BEGIN(method)                         \
    double arg1, arg2, arg3, arg4;                                       \
    _GJS_CAIRO_CONTEXT_CHECK_NO_ARGS(method)                             \
    cfunc(cr, &arg1, &arg2, &arg3, &arg4);                               \
    {                                                                    \
        JS::RootedObject array(context, JS::NewArrayObject(context, 4)); \
        if (!array)                                                      \
            return false;                                                \
        JS::RootedValue r{context,                                       \
                          JS::NumberValue(JS::CanonicalizeNaN(arg1))};   \
        if (!JS_SetElement(context, array, 0, r))                        \
            return false;                                                \
        r.setNumber(JS::CanonicalizeNaN(arg2));                          \
        if (!JS_SetElement(context, array, 1, r))                        \
            return false;                                                \
        r.setNumber(JS::CanonicalizeNaN(arg3));                          \
        if (!JS_SetElement(context, array, 2, r))                        \
            return false;                                                \
        r.setNumber(JS::CanonicalizeNaN(arg4));                          \
        if (!JS_SetElement(context, array, 3, r))                        \
            return false;                                                \
        argv.rval().setObject(*array);                                   \
    }                                                                    \
    _GJS_CAIRO_CONTEXT_DEFINE_FUNC_END

#define _GJS_CAIRO_CONTEXT_DEFINE_FUNC0F(method, cfunc)                    \
_GJS_CAIRO_CONTEXT_DEFINE_FUNC_BEGIN(method)                               \
    double ret;                                                            \
   _GJS_CAIRO_CONTEXT_CHECK_NO_ARGS(method)                                \
    ret = cfunc(cr);                                                       \
    argv.rval().setNumber(JS::CanonicalizeNaN(ret));                       \
_GJS_CAIRO_CONTEXT_DEFINE_FUNC_END

#define _GJS_CAIRO_CONTEXT_DEFINE_FUNC1(method, cfunc, fmt, t1, n1)        \
_GJS_CAIRO_CONTEXT_DEFINE_FUNC_BEGIN(method)                               \
    t1 arg1;                                                               \
    if (!gjs_parse_call_args(context, #method, argv, fmt,                  \
                             #n1, &arg1))                                  \
        return false;                                                      \
    cfunc(cr, arg1);                                                       \
    argv.rval().setUndefined();                                            \
_GJS_CAIRO_CONTEXT_DEFINE_FUNC_END

#define _GJS_CAIRO_CONTEXT_DEFINE_FUNC2(method, cfunc, fmt, t1, n1, t2, n2) \
_GJS_CAIRO_CONTEXT_DEFINE_FUNC_BEGIN(method)                               \
    t1 arg1;                                                               \
    t2 arg2;                                                               \
    if (!gjs_parse_call_args(context, #method, argv, fmt,                  \
                             #n1, &arg1, #n2, &arg2))                      \
        return false;                                                      \
    cfunc(cr, arg1, arg2);                                                 \
    argv.rval().setUndefined();                                            \
_GJS_CAIRO_CONTEXT_DEFINE_FUNC_END

#define _GJS_CAIRO_CONTEXT_DEFINE_FUNC2B(method, cfunc, fmt, t1, n1, t2, n2) \
_GJS_CAIRO_CONTEXT_DEFINE_FUNC_BEGIN(method)                               \
    t1 arg1;                                                               \
    t2 arg2;                                                               \
    cairo_bool_t ret;                                                      \
    if (!gjs_parse_call_args(context, #method, argv, fmt,                  \
                             #n1, &arg1, #n2, &arg2))                      \
        return false;                                                      \
    ret = cfunc(cr, arg1, arg2);                                           \
    argv.rval().setBoolean(ret);                                           \
_GJS_CAIRO_CONTEXT_DEFINE_FUNC_END

#define _GJS_CAIRO_CONTEXT_DEFINE_FUNC3(method, cfunc, fmt, t1, n1, t2, n2, t3, n3) \
_GJS_CAIRO_CONTEXT_DEFINE_FUNC_BEGIN(method)                               \
    t1 arg1;                                                               \
    t2 arg2;                                                               \
    t3 arg3;                                                               \
    if (!gjs_parse_call_args(context, #method, argv, fmt,                  \
                             #n1, &arg1, #n2, &arg2, #n3, &arg3))          \
        return false;                                                      \
    cfunc(cr, arg1, arg2, arg3);                                           \
    argv.rval().setUndefined();                                            \
_GJS_CAIRO_CONTEXT_DEFINE_FUNC_END

#define _GJS_CAIRO_CONTEXT_DEFINE_FUNC4(method, cfunc, fmt, t1, n1, t2, n2, t3, n3, t4, n4) \
_GJS_CAIRO_CONTEXT_DEFINE_FUNC_BEGIN(method)                               \
    t1 arg1;                                                               \
    t2 arg2;                                                               \
    t3 arg3;                                                               \
    t4 arg4;                                                               \
    if (!gjs_parse_call_args(context, #method, argv, fmt,                  \
                             #n1, &arg1, #n2, &arg2,                       \
                             #n3, &arg3, #n4, &arg4))                      \
        return false;                                                      \
    cfunc(cr, arg1, arg2, arg3, arg4);                                     \
_GJS_CAIRO_CONTEXT_DEFINE_FUNC_END

#define _GJS_CAIRO_CONTEXT_DEFINE_FUNC5(method, cfunc, fmt, t1, n1, t2, n2, t3, n3, t4, n4, t5, n5) \
_GJS_CAIRO_CONTEXT_DEFINE_FUNC_BEGIN(method)                               \
    t1 arg1;                                                               \
    t2 arg2;                                                               \
    t3 arg3;                                                               \
    t4 arg4;                                                               \
    t5 arg5;                                                               \
    if (!gjs_parse_call_args(context, #method, argv, fmt,                  \
                             #n1, &arg1, #n2, &arg2, #n3, &arg3,           \
                             #n4, &arg4, #n5, &arg5))                      \
        return false;                                                      \
    cfunc(cr, arg1, arg2, arg3, arg4, arg5);                               \
    argv.rval().setUndefined();                                            \
_GJS_CAIRO_CONTEXT_DEFINE_FUNC_END

#define _GJS_CAIRO_CONTEXT_DEFINE_FUNC6(method, cfunc, fmt, t1, n1, t2, n2, t3, n3, t4, n4, t5, n5, t6, n6) \
_GJS_CAIRO_CONTEXT_DEFINE_FUNC_BEGIN(method)                               \
    t1 arg1;                                                               \
    t2 arg2;                                                               \
    t3 arg3;                                                               \
    t4 arg4;                                                               \
    t5 arg5;                                                               \
    t6 arg6;                                                               \
    if (!gjs_parse_call_args(context, #method, argv, fmt,                  \
                             #n1, &arg1, #n2, &arg2, #n3, &arg3,           \
                             #n4, &arg4, #n5, &arg5, #n6, &arg6))          \
        return false;                                                      \
    cfunc(cr, arg1, arg2, arg3, arg4, arg5, arg6);                         \
    argv.rval().setUndefined();                                            \
_GJS_CAIRO_CONTEXT_DEFINE_FUNC_END

GJS_JSAPI_RETURN_CONVENTION
cairo_t* CairoContext::constructor_impl(JSContext* context,
                                        const JS::CallArgs& argv) {
    cairo_t *cr;

    JS::RootedObject surface_wrapper(context);
    if (!gjs_parse_call_args(context, "Context", argv, "o",
                             "surface", &surface_wrapper))
        return nullptr;

    cairo_surface_t* surface = CairoSurface::for_js(context, surface_wrapper);
    if (!surface)
        return nullptr;

    cr = cairo_create(surface);

    if (!gjs_cairo_check_status(context, cairo_status(cr), "context"))
        return nullptr;

    return cr;
}

void CairoContext::finalize_impl(JS::GCContext*, cairo_t* cr) {
    if (!cr)
        return;
    cairo_destroy(cr);
}

/* Properties */
// clang-format off
const JSPropertySpec CairoContext::proto_props[] = {
    JS_STRING_SYM_PS(toStringTag, "Context", JSPROP_READONLY),
    JS_PS_END};
// clang-format on

/* Methods */

_GJS_CAIRO_CONTEXT_DEFINE_FUNC5(arc, cairo_arc, "fffff",
                                double, xc, double, yc, double, radius,
                                double, angle1, double, angle2)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC5(arcNegative, cairo_arc_negative, "fffff",
                                double, xc, double, yc, double, radius,
                                double, angle1, double, angle2)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC6(curveTo, cairo_curve_to, "ffffff",
                                double, x1, double, y1, double, x2, double, y2,
                                double, x3, double, y3)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC0(clip, cairo_clip)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC0(clipPreserve, cairo_clip_preserve)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC0AFFFF(clipExtents, cairo_clip_extents)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC0(closePath, cairo_close_path)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC0(copyPage, cairo_copy_page)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC2FFAFF(deviceToUser, cairo_device_to_user, "x", "y")
_GJS_CAIRO_CONTEXT_DEFINE_FUNC2FFAFF(deviceToUserDistance, cairo_device_to_user_distance, "x", "y")
_GJS_CAIRO_CONTEXT_DEFINE_FUNC0(fill, cairo_fill)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC0(fillPreserve, cairo_fill_preserve)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC0AFFFF(fillExtents, cairo_fill_extents)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC0I(getAntialias, cairo_get_antialias)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC0AFF(getCurrentPoint, cairo_get_current_point)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC0I(getDashCount, cairo_get_dash_count)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC0I(getFillRule, cairo_get_fill_rule)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC0I(getLineCap, cairo_get_line_cap)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC0I(getLineJoin, cairo_get_line_join)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC0F(getLineWidth, cairo_get_line_width)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC0F(getMiterLimit, cairo_get_miter_limit)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC0I(getOperator, cairo_get_operator)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC0F(getTolerance, cairo_get_tolerance)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC0B(hasCurrentPoint, cairo_has_current_point)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC0(identityMatrix, cairo_identity_matrix)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC2B(inFill, cairo_in_fill, "ff", double, x, double, y)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC2B(inStroke, cairo_in_stroke, "ff", double, x, double, y)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC2(lineTo, cairo_line_to, "ff", double, x, double, y)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC2(moveTo, cairo_move_to, "ff", double, x, double, y)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC0(newPath, cairo_new_path)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC0(newSubPath, cairo_new_sub_path)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC0(paint, cairo_paint)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC1(paintWithAlpha, cairo_paint_with_alpha, "f", double, alpha)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC0AFFFF(pathExtents, cairo_path_extents)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC0(pushGroup, cairo_push_group)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC1(pushGroupWithContent, cairo_push_group_with_content, "i",
                                cairo_content_t, content)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC0(popGroupToSource, cairo_pop_group_to_source)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC4(rectangle, cairo_rectangle, "ffff",
                                double, x, double, y, double, width, double, height)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC6(relCurveTo, cairo_rel_curve_to, "ffffff",
                                double, dx1, double, dy1, double, dx2, double, dy2,
                                double, dx3, double, dy3)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC2(relLineTo, cairo_rel_line_to, "ff", double, dx, double, dy)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC2(relMoveTo, cairo_rel_move_to, "ff", double, dx, double, dy)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC0(resetClip, cairo_reset_clip)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC0(restore, cairo_restore)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC1(rotate, cairo_rotate, "f", double, angle)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC0(save, cairo_save)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC2(scale, cairo_scale, "ff", double, sx, double, sy)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC1(setAntialias, cairo_set_antialias, "i", cairo_antialias_t, antialias)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC1(setFillRule, cairo_set_fill_rule, "i", cairo_fill_rule_t, fill_rule)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC1(setFontSize, cairo_set_font_size, "f", double, size)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC1(setLineCap, cairo_set_line_cap, "i", cairo_line_cap_t, line_cap)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC1(setLineJoin, cairo_set_line_join, "i", cairo_line_join_t, line_join)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC1(setLineWidth, cairo_set_line_width, "f", double, width)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC1(setMiterLimit, cairo_set_miter_limit, "f", double, limit)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC1(setOperator, cairo_set_operator, "i", cairo_operator_t, op)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC1(setTolerance, cairo_set_tolerance, "f", double, tolerance)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC3(setSourceRGB, cairo_set_source_rgb, "fff",
                                double, red, double, green, double, blue)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC4(setSourceRGBA, cairo_set_source_rgba, "ffff",
                                double, red, double, green, double, blue, double, alpha)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC0(showPage, cairo_show_page)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC0(stroke, cairo_stroke)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC0(strokePreserve, cairo_stroke_preserve)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC0AFFFF(strokeExtents, cairo_stroke_extents)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC2(translate, cairo_translate, "ff", double, tx, double, ty)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC2FFAFF(userToDevice, cairo_user_to_device, "x", "y")
_GJS_CAIRO_CONTEXT_DEFINE_FUNC2FFAFF(userToDeviceDistance, cairo_user_to_device_distance, "x", "y")

bool CairoContext::dispose(JSContext* context, unsigned argc, JS::Value* vp) {
    _GJS_CAIRO_CONTEXT_GET_PRIV_CR_CHECKED(context, argc, vp, rec, obj);

    cairo_destroy(cr);
    CairoContext::unset_private(obj);

    rec.rval().setUndefined();
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool
appendPath_func(JSContext *context,
                unsigned   argc,
                JS::Value *vp)
{
    _GJS_CAIRO_CONTEXT_GET_PRIV_CR_CHECKED(context, argc, vp, argv, obj);

    JS::RootedObject path_wrapper(context);

    if (!gjs_parse_call_args(context, "path", argv, "o",
                             "path", &path_wrapper))
        return false;

    cairo_path_t* path;
    if (!CairoPath::for_js_typecheck(context, path_wrapper, &path, &argv))
        return false;

    cairo_append_path(cr, path);
    argv.rval().setUndefined();
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool
copyPath_func(JSContext *context,
              unsigned   argc,
              JS::Value *vp)
{
    _GJS_CAIRO_CONTEXT_GET_PRIV_CR_CHECKED(context, argc, vp, argv, obj);

    cairo_path_t *path;

    if (!gjs_parse_call_args(context, "", argv, ""))
        return false;

    path = cairo_copy_path(cr);
    JSObject* retval = CairoPath::take_c_ptr(context, path);
    if (!retval)
        return false;

    argv.rval().setObject(*retval);
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool
copyPathFlat_func(JSContext *context,
                  unsigned   argc,
                  JS::Value *vp)
{
    _GJS_CAIRO_CONTEXT_GET_PRIV_CR_CHECKED(context, argc, vp, argv, obj);

    cairo_path_t *path;

    if (!gjs_parse_call_args(context, "", argv, ""))
        return false;

    path = cairo_copy_path_flat(cr);
    JSObject* retval = CairoPath::take_c_ptr(context, path);
    if (!retval)
        return false;

    argv.rval().setObject(*retval);
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool
mask_func(JSContext *context,
          unsigned   argc,
          JS::Value *vp)
{
    _GJS_CAIRO_CONTEXT_GET_PRIV_CR_CHECKED(context, argc, vp, argv, obj);

    JS::RootedObject pattern_wrapper(context);

    if (!gjs_parse_call_args(context, "mask", argv, "o",
                             "pattern", &pattern_wrapper))
        return false;

    cairo_pattern_t* pattern = CairoPattern::for_js(context, pattern_wrapper);
    if (!pattern)
        return false;

    cairo_mask(cr, pattern);

    if (!gjs_cairo_check_status(context, cairo_status(cr), "context"))
        return false;

    argv.rval().setUndefined();
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool
maskSurface_func(JSContext *context,
                 unsigned   argc,
                 JS::Value *vp)
{
    _GJS_CAIRO_CONTEXT_GET_PRIV_CR_CHECKED(context, argc, vp, argv, obj);

    JS::RootedObject surface_wrapper(context);
    double x, y;

    if (!gjs_parse_call_args(context, "maskSurface", argv, "off",
                             "surface", &surface_wrapper,
                             "x", &x,
                             "y", &y))
        return false;

    cairo_surface_t* surface = CairoSurface::for_js(context, surface_wrapper);
    if (!surface)
        return false;

    cairo_mask_surface(cr, surface, x, y);

    if (!gjs_cairo_check_status(context, cairo_status(cr), "context"))
        return false;

    argv.rval().setUndefined();
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool
setDash_func(JSContext *context,
             unsigned   argc,
             JS::Value *vp)
{
    _GJS_CAIRO_CONTEXT_GET_PRIV_CR_CHECKED(context, argc, vp, argv, obj);

    guint i;
    JS::RootedObject dashes(context);
    double offset;
    guint len;
    bool is_array;

    if (!gjs_parse_call_args(context, "setDash", argv, "of",
                             "dashes", &dashes,
                             "offset", &offset))
        return false;

    if (!JS::IsArrayObject(context, dashes, &is_array))
        return false;
    if (!is_array) {
        gjs_throw(context, "dashes must be an array");
        return false;
    }

    if (!JS::GetArrayLength(context, dashes, &len)) {
        gjs_throw(context, "Can't get length of dashes");
        return false;
    }

    std::unique_ptr<double[]> dashes_c = std::make_unique<double[]>(len);
    size_t dashes_c_size = 0;
    JS::RootedValue elem(context);
    for (i = 0; i < len; ++i) {
        double b;

        elem.setUndefined();
        if (!JS_GetElement(context, dashes, i, &elem)) {
            return false;
        }
        if (elem.isUndefined())
            continue;

        if (!JS::ToNumber(context, elem, &b))
            return false;
        if (b <= 0) {
            gjs_throw(context, "Dash value must be positive");
            return false;
        }

        dashes_c[dashes_c_size++] = b;
    }

    cairo_set_dash(cr, dashes_c.get(), dashes_c_size, offset);
    argv.rval().setUndefined();
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool
setSource_func(JSContext *context,
               unsigned   argc,
               JS::Value *vp)
{
    _GJS_CAIRO_CONTEXT_GET_PRIV_CR_CHECKED(context, argc, vp, argv, obj);

    JS::RootedObject pattern_wrapper(context);

    if (!gjs_parse_call_args(context, "setSource", argv, "o",
                             "pattern", &pattern_wrapper))
        return false;

    cairo_pattern_t* pattern = CairoPattern::for_js(context, pattern_wrapper);
    if (!pattern)
        return false;

    cairo_set_source(cr, pattern);

    if (!gjs_cairo_check_status(context, cairo_status(cr), "context"))
        return false;

    argv.rval().setUndefined();

    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool
setSourceSurface_func(JSContext *context,
                      unsigned   argc,
                      JS::Value *vp)
{
    _GJS_CAIRO_CONTEXT_GET_PRIV_CR_CHECKED(context, argc, vp, argv, obj);

    JS::RootedObject surface_wrapper(context);
    double x, y;

    if (!gjs_parse_call_args(context, "setSourceSurface", argv, "off",
                             "surface", &surface_wrapper,
                             "x", &x,
                             "y", &y))
        return false;

    cairo_surface_t* surface = CairoSurface::for_js(context, surface_wrapper);
    if (!surface)
        return false;

    cairo_set_source_surface(cr, surface, x, y);

    if (!gjs_cairo_check_status(context, cairo_status(cr), "context"))
        return false;

    argv.rval().setUndefined();

    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool
showText_func(JSContext *context,
              unsigned   argc,
              JS::Value *vp)
{
    _GJS_CAIRO_CONTEXT_GET_PRIV_CR_CHECKED(context, argc, vp, argv, obj);

    JS::UniqueChars utf8;

    if (!gjs_parse_call_args(context, "showText", argv, "s",
                             "utf8", &utf8))
        return false;

    cairo_show_text(cr, utf8.get());

    if (!gjs_cairo_check_status(context, cairo_status(cr), "context"))
        return false;

    argv.rval().setUndefined();

    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool
selectFontFace_func(JSContext *context,
                    unsigned   argc,
                    JS::Value *vp)
{
    _GJS_CAIRO_CONTEXT_GET_PRIV_CR_CHECKED(context, argc, vp, argv, obj);

    JS::UniqueChars family;
    cairo_font_slant_t slant;
    cairo_font_weight_t weight;

    if (!gjs_parse_call_args(context, "selectFontFace", argv, "sii",
                             "family", &family,
                             "slang", &slant,
                             "weight", &weight))
        return false;

    cairo_select_font_face(cr, family.get(), slant, weight);

    if (!gjs_cairo_check_status(context, cairo_status(cr), "context"))
        return false;
    argv.rval().setUndefined();

    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool
popGroup_func(JSContext *context,
              unsigned   argc,
              JS::Value *vp)
{
    _GJS_CAIRO_CONTEXT_GET_PRIV_CR_CHECKED(context, argc, vp, rec, obj);

    cairo_pattern_t *pattern;
    JSObject *pattern_wrapper;

    if (argc > 0) {
        gjs_throw(context, "Context.popGroup() takes no arguments");
        return false;
    }

    pattern = cairo_pop_group(cr);
    if (!gjs_cairo_check_status(context, cairo_status(cr), "context"))
        return false;

    pattern_wrapper = gjs_cairo_pattern_from_pattern(context, pattern);
    cairo_pattern_destroy(pattern);
    if (!pattern_wrapper) {
        gjs_throw(context, "failed to create pattern");
        return false;
    }

    rec.rval().setObject(*pattern_wrapper);

    return true;
}
GJS_JSAPI_RETURN_CONVENTION
static bool
getSource_func(JSContext *context,
               unsigned   argc,
               JS::Value *vp)
{
    _GJS_CAIRO_CONTEXT_GET_PRIV_CR_CHECKED(context, argc, vp, rec, obj);

    cairo_pattern_t *pattern;
    JSObject *pattern_wrapper;

    if (argc > 0) {
        gjs_throw(context, "Context.getSource() takes no arguments");
        return false;
    }

    pattern = cairo_get_source(cr);
    if (!gjs_cairo_check_status(context, cairo_status(cr), "context"))
        return false;

    /* pattern belongs to the context, so keep the reference */
    pattern_wrapper = gjs_cairo_pattern_from_pattern(context, pattern);
    if (!pattern_wrapper) {
        gjs_throw(context, "failed to create pattern");
        return false;
    }

    rec.rval().setObject(*pattern_wrapper);

    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool
getTarget_func(JSContext *context,
               unsigned   argc,
               JS::Value *vp)
{
    _GJS_CAIRO_CONTEXT_GET_PRIV_CR_CHECKED(context, argc, vp, rec, obj);

    cairo_surface_t *surface;

    if (argc > 0) {
        gjs_throw(context, "Context.getTarget() takes no arguments");
        return false;
    }

    surface = cairo_get_target(cr);
    if (!gjs_cairo_check_status(context, cairo_status(cr), "context"))
        return false;

    /* surface belongs to the context, so keep the reference */
    JSObject* surface_wrapper = CairoSurface::from_c_ptr(context, surface);
    if (!surface_wrapper) {
        /* exception already set */
        return false;
    }

    rec.rval().setObject(*surface_wrapper);

    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool
getGroupTarget_func(JSContext *context,
                    unsigned   argc,
                    JS::Value *vp)
{
    _GJS_CAIRO_CONTEXT_GET_PRIV_CR_CHECKED(context, argc, vp, rec, obj);

    cairo_surface_t *surface;

    if (argc > 0) {
        gjs_throw(context, "Context.getGroupTarget() takes no arguments");
        return false;
    }

    surface = cairo_get_group_target(cr);
    if (!gjs_cairo_check_status(context, cairo_status(cr), "context"))
        return false;

    /* surface belongs to the context, so keep the reference */
    JSObject* surface_wrapper = CairoSurface::from_c_ptr(context, surface);
    if (!surface_wrapper) {
        /* exception already set */
        return false;
    }

    rec.rval().setObject(*surface_wrapper);

    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool textExtents_func(JSContext* cx, unsigned argc, JS::Value* vp) {
    _GJS_CAIRO_CONTEXT_GET_PRIV_CR_CHECKED(cx, argc, vp, args, this_obj);

    JS::UniqueChars utf8;
    if (!gjs_parse_call_args(cx, "textExtents", args, "s", "utf8", &utf8))
        return false;

    cairo_text_extents_t extents;
    cairo_text_extents(cr, utf8.get(), &extents);
    if (!gjs_cairo_check_status(cx, cairo_status(cr), "context"))
        return false;

    JS::RootedObject extents_obj(cx, JS_NewPlainObject(cx));
    if (!extents_obj)
        return false;

    JSPropertySpec properties[] = {
        JS_DOUBLE_PS("xBearing", extents.x_bearing, JSPROP_ENUMERATE),
        JS_DOUBLE_PS("yBearing", extents.y_bearing, JSPROP_ENUMERATE),
        JS_DOUBLE_PS("width", extents.width, JSPROP_ENUMERATE),
        JS_DOUBLE_PS("height", extents.height, JSPROP_ENUMERATE),
        JS_DOUBLE_PS("xAdvance", extents.x_advance, JSPROP_ENUMERATE),
        JS_DOUBLE_PS("yAdvance", extents.y_advance, JSPROP_ENUMERATE),
        JS_PS_END};

    if (!JS_DefineProperties(cx, extents_obj, properties))
        return false;

    args.rval().setObject(*extents_obj);
    return true;
}

// clang-format off
const JSFunctionSpec CairoContext::proto_funcs[] = {
    JS_FN("$dispose", &CairoContext::dispose, 0, 0),
    JS_FN("appendPath", appendPath_func, 0, 0),
    JS_FN("arc", arc_func, 0, 0),
    JS_FN("arcNegative", arcNegative_func, 0, 0),
    JS_FN("clip", clip_func, 0, 0),
    JS_FN("clipExtents", clipExtents_func, 0, 0),
    JS_FN("clipPreserve", clipPreserve_func, 0, 0),
    JS_FN("closePath", closePath_func, 0, 0),
    JS_FN("copyPage", copyPage_func, 0, 0),
    JS_FN("copyPath", copyPath_func, 0, 0),
    JS_FN("copyPathFlat", copyPathFlat_func, 0, 0),
    JS_FN("curveTo", curveTo_func, 0, 0),
    JS_FN("deviceToUser", deviceToUser_func, 0, 0),
    JS_FN("deviceToUserDistance", deviceToUserDistance_func, 0, 0),
    JS_FN("fill", fill_func, 0, 0),
    JS_FN("fillPreserve", fillPreserve_func, 0, 0),
    JS_FN("fillExtents", fillExtents_func, 0, 0),
    // fontExtents
    JS_FN("getAntialias", getAntialias_func, 0, 0),
    JS_FN("getCurrentPoint", getCurrentPoint_func, 0, 0),
    // getDash
    JS_FN("getDashCount", getDashCount_func, 0, 0),
    JS_FN("getFillRule", getFillRule_func, 0, 0),
    // getFontFace
    // getFontMatrix
    // getFontOptions
    JS_FN("getGroupTarget", getGroupTarget_func, 0, 0),
    JS_FN("getLineCap", getLineCap_func, 0, 0),
    JS_FN("getLineJoin", getLineJoin_func, 0, 0),
    JS_FN("getLineWidth", getLineWidth_func, 0, 0),
    // getMatrix
    JS_FN("getMiterLimit", getMiterLimit_func, 0, 0),
    JS_FN("getOperator", getOperator_func, 0, 0),
    // getScaledFont
    JS_FN("getSource", getSource_func, 0, 0),
    JS_FN("getTarget", getTarget_func, 0, 0),
    JS_FN("getTolerance", getTolerance_func, 0, 0),
    // glyphPath
    // glyphExtents
    JS_FN("hasCurrentPoint", hasCurrentPoint_func, 0, 0),
    JS_FN("identityMatrix", identityMatrix_func, 0, 0),
    JS_FN("inFill", inFill_func, 0, 0),
    JS_FN("inStroke", inStroke_func, 0, 0),
    JS_FN("lineTo", lineTo_func, 0, 0),
    JS_FN("mask", mask_func, 0, 0),
    JS_FN("maskSurface", maskSurface_func, 0, 0),
    JS_FN("moveTo", moveTo_func, 0, 0),
    JS_FN("newPath", newPath_func, 0, 0),
    JS_FN("newSubPath", newSubPath_func, 0, 0),
    JS_FN("paint", paint_func, 0, 0),
    JS_FN("paintWithAlpha", paintWithAlpha_func, 0, 0),
    JS_FN("pathExtents", pathExtents_func, 0, 0),
    JS_FN("popGroup", popGroup_func, 0, 0),
    JS_FN("popGroupToSource", popGroupToSource_func, 0, 0),
    JS_FN("pushGroup", pushGroup_func, 0, 0),
    JS_FN("pushGroupWithContent", pushGroupWithContent_func, 0, 0),
    JS_FN("rectangle", rectangle_func, 0, 0),
    JS_FN("relCurveTo", relCurveTo_func, 0, 0),
    JS_FN("relLineTo", relLineTo_func, 0, 0),
    JS_FN("relMoveTo", relMoveTo_func, 0, 0),
    JS_FN("resetClip", resetClip_func, 0, 0),
    JS_FN("restore", restore_func, 0, 0),
    JS_FN("rotate", rotate_func, 0, 0),
    JS_FN("save", save_func, 0, 0),
    JS_FN("scale", scale_func, 0, 0),
    JS_FN("selectFontFace", selectFontFace_func, 0, 0),
    JS_FN("setAntialias", setAntialias_func, 0, 0),
    JS_FN("setDash", setDash_func, 0, 0),
    // setFontFace
    // setFontMatrix
    // setFontOptions
    JS_FN("setFontSize", setFontSize_func, 0, 0),
    JS_FN("setFillRule", setFillRule_func, 0, 0),
    JS_FN("setLineCap", setLineCap_func, 0, 0),
    JS_FN("setLineJoin", setLineJoin_func, 0, 0),
    JS_FN("setLineWidth", setLineWidth_func, 0, 0),
    // setMatrix
    JS_FN("setMiterLimit", setMiterLimit_func, 0, 0),
    JS_FN("setOperator", setOperator_func, 0, 0),
    // setScaledFont
    JS_FN("setSource", setSource_func, 0, 0),
    JS_FN("setSourceRGB", setSourceRGB_func, 0, 0),
    JS_FN("setSourceRGBA", setSourceRGBA_func, 0, 0),
    JS_FN("setSourceSurface", setSourceSurface_func, 0, 0),
    JS_FN("setTolerance", setTolerance_func, 0, 0),
    // showGlyphs
    JS_FN("showPage", showPage_func, 0, 0),
    JS_FN("showText", showText_func, 0, 0),
    // showTextGlyphs
    JS_FN("stroke", stroke_func, 0, 0),
    JS_FN("strokeExtents", strokeExtents_func, 0, 0),
    JS_FN("strokePreserve", strokePreserve_func, 0, 0),
    // textPath
    JS_FN("textExtents", textExtents_func, 1, 0),
    // transform
    JS_FN("translate", translate_func, 0, 0),
    JS_FN("userToDevice", userToDevice_func, 0, 0),
    JS_FN("userToDeviceDistance", userToDeviceDistance_func, 0, 0),
    JS_FS_END};
// clang-format on

GJS_JSAPI_RETURN_CONVENTION static bool context_to_gi_argument(
    JSContext* context, JS::Value value, const char* arg_name,
    GjsArgumentType argument_type, GITransfer transfer, GjsArgumentFlags flags,
    GIArgument* arg) {
    if (value.isNull()) {
        if (!(flags & GjsArgumentFlags::MAY_BE_NULL)) {
            Gjs::AutoChar display_name{
                gjs_argument_display_name(arg_name, argument_type)};
            gjs_throw(context, "%s may not be null", display_name.get());
            return false;
        }

        gjs_arg_unset(arg);
        return true;
    }

    JS::RootedObject obj(context, &value.toObject());
    cairo_t* cr = CairoContext::for_js(context, obj);
    if (!cr)
        return false;
    if (transfer == GI_TRANSFER_EVERYTHING)
        cairo_reference(cr);

    gjs_arg_set(arg, cr);
    return true;
}

GJS_JSAPI_RETURN_CONVENTION
static bool context_from_gi_argument(JSContext* context,
                                     JS::MutableHandleValue value_p,
                                     GIArgument* arg) {
    JSObject* obj = CairoContext::from_c_ptr(
        context, static_cast<cairo_t*>(arg->v_pointer));
    if (!obj) {
        gjs_throw(context, "Could not create Cairo context");
        return false;
    }

    value_p.setObject(*obj);
    return true;
}

static bool context_release_argument(JSContext*, GITransfer transfer,
                                     GIArgument* arg) {
    if (transfer != GI_TRANSFER_NOTHING)
        cairo_destroy(gjs_arg_get<cairo_t*>(arg));
    return true;
}

void gjs_cairo_context_init(void) {
    static GjsForeignInfo foreign_info = {context_to_gi_argument,
                                          context_from_gi_argument,
                                          context_release_argument};

    gjs_struct_foreign_register("cairo", "Context", &foreign_info);
}
