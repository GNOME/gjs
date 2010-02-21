/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/* Copyright 2010 litl, LLC. All Rights Reserved.
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

#include <config.h>

#include <gjs/gjs.h>
#include <cairo.h>
#include "cairo-private.h"

#define _GJS_CAIRO_CONTEXT_DEFINE_FUNC_BEGIN(mname) \
static JSBool \
mname##_func(JSContext *context, \
              JSObject  *obj, \
              uintN      argc, \
              jsval     *argv, \
              jsval     *retval) \
{ \
    cairo_t *cr;

#define _GJS_CAIRO_CONTEXT_DEFINE_FUNC_END \
    return JS_TRUE; \
}

#define _GJS_CAIRO_CONTEXT_DEFINE_FUNC0(method, cfunc) \
_GJS_CAIRO_CONTEXT_DEFINE_FUNC_BEGIN(method) \
    if (argc > 0) { \
        gjs_throw(context, "Context." #method "() requires no arguments"); \
        return JS_FALSE; \
    } \
    cr = gjs_cairo_context_get_cr(context, obj); \
    cfunc(cr); \
_GJS_CAIRO_CONTEXT_DEFINE_FUNC_END

#define _GJS_CAIRO_CONTEXT_DEFINE_FUNC0I(method, cfunc) \
_GJS_CAIRO_CONTEXT_DEFINE_FUNC_BEGIN(method) \
    int ret; \
    if (argc > 0) { \
        gjs_throw(context, "Context." #method "() requires no arguments"); \
        return JS_FALSE; \
    } \
    cr = gjs_cairo_context_get_cr(context, obj); \
    ret = (int)cfunc(cr); \
    *retval = INT_TO_JSVAL(ret); \
_GJS_CAIRO_CONTEXT_DEFINE_FUNC_END

#define _GJS_CAIRO_CONTEXT_DEFINE_FUNC0AFF(method, cfunc) \
_GJS_CAIRO_CONTEXT_DEFINE_FUNC_BEGIN(method) \
    double arg1, arg2; \
    if (argc > 0) { \
        gjs_throw(context, "Context." #method "() requires no arguments"); \
        return JS_FALSE; \
    } \
    cr = gjs_cairo_context_get_cr(context, obj); \
    cfunc(cr, &arg1, &arg2); \
    { \
      JSObject *array = JS_NewArrayObject(context, 0, NULL); \
      if (!array) \
        return JS_FALSE; \
      jsval r; \
      if (!JS_NewNumberValue(context, arg1, &r)) return JS_FALSE; \
      if (!JS_SetElement(context, array, 0, &r)) return JS_FALSE; \
      if (!JS_NewNumberValue(context, arg2, &r)) return JS_FALSE; \
      if (!JS_SetElement(context, array, 1, &r)) return JS_FALSE; \
      *retval = OBJECT_TO_JSVAL(array); \
    } \
_GJS_CAIRO_CONTEXT_DEFINE_FUNC_END

#define _GJS_CAIRO_CONTEXT_DEFINE_FUNC0AFFFF(method, cfunc) \
_GJS_CAIRO_CONTEXT_DEFINE_FUNC_BEGIN(method) \
    double arg1, arg2, arg3, arg4; \
    if (argc > 0) { \
        gjs_throw(context, "Context." #method "() requires no arguments"); \
        return JS_FALSE; \
    } \
    cr = gjs_cairo_context_get_cr(context, obj); \
    cfunc(cr, &arg1, &arg2, &arg3, &arg4); \
    { \
      JSObject *array = JS_NewArrayObject(context, 0, NULL); \
      if (!array) \
        return JS_FALSE; \
      jsval r; \
      if (!JS_NewNumberValue(context, arg1, &r)) return JS_FALSE; \
      if (!JS_SetElement(context, array, 0, &r)) return JS_FALSE; \
      if (!JS_NewNumberValue(context, arg2, &r)) return JS_FALSE; \
      if (!JS_SetElement(context, array, 1, &r)) return JS_FALSE; \
      if (!JS_NewNumberValue(context, arg3, &r)) return JS_FALSE; \
      if (!JS_SetElement(context, array, 2, &r)) return JS_FALSE; \
      if (!JS_NewNumberValue(context, arg4, &r)) return JS_FALSE; \
      if (!JS_SetElement(context, array, 3, &r)) return JS_FALSE; \
      *retval = OBJECT_TO_JSVAL(array); \
    } \
_GJS_CAIRO_CONTEXT_DEFINE_FUNC_END

#define _GJS_CAIRO_CONTEXT_DEFINE_FUNC0F(method, cfunc) \
_GJS_CAIRO_CONTEXT_DEFINE_FUNC_BEGIN(method) \
    double ret; \
    if (argc > 0) { \
        gjs_throw(context, "Context." #method "() requires no arguments"); \
        return JS_FALSE; \
    } \
    cr = gjs_cairo_context_get_cr(context, obj); \
    ret = cfunc(cr); \
    if (!JS_NewNumberValue(context, ret, retval)) \
        return JS_FALSE; \
_GJS_CAIRO_CONTEXT_DEFINE_FUNC_END

#define _GJS_CAIRO_CONTEXT_DEFINE_FUNC1(method, cfunc, fmt, t1, n1) \
_GJS_CAIRO_CONTEXT_DEFINE_FUNC_BEGIN(method) \
    t1 arg1; \
    if (!gjs_parse_args(context, #method, fmt, argc, argv, \
                        #n1, &arg1)) \
        return JS_FALSE; \
    cr = gjs_cairo_context_get_cr(context, obj); \
    cfunc(cr, arg1); \
_GJS_CAIRO_CONTEXT_DEFINE_FUNC_END

#define _GJS_CAIRO_CONTEXT_DEFINE_FUNC2(method, cfunc, fmt, t1, n1, t2, n2) \
_GJS_CAIRO_CONTEXT_DEFINE_FUNC_BEGIN(method) \
    t1 arg1; \
    t2 arg2; \
    if (!gjs_parse_args(context, #method, fmt, argc, argv, \
                        #n1, &arg1, #n2, &arg2)) \
        return JS_FALSE; \
    cr = gjs_cairo_context_get_cr(context, obj); \
    cfunc(cr, arg1, arg2); \
_GJS_CAIRO_CONTEXT_DEFINE_FUNC_END

#define _GJS_CAIRO_CONTEXT_DEFINE_FUNC3(method, cfunc, fmt, t1, n1, t2, n2, t3, n3) \
_GJS_CAIRO_CONTEXT_DEFINE_FUNC_BEGIN(method) \
    t1 arg1; \
    t2 arg2; \
    t3 arg3; \
    if (!gjs_parse_args(context, #method, fmt, argc, argv, \
                        #n1, &arg1, #n2, &arg2, #n3, &arg3)) \
        return JS_FALSE; \
    cr = gjs_cairo_context_get_cr(context, obj); \
    cfunc(cr, arg1, arg2, arg3); \
_GJS_CAIRO_CONTEXT_DEFINE_FUNC_END

#define _GJS_CAIRO_CONTEXT_DEFINE_FUNC4(method, cfunc, fmt, t1, n1, t2, n2, t3, n3, t4, n4) \
_GJS_CAIRO_CONTEXT_DEFINE_FUNC_BEGIN(method) \
    t1 arg1; \
    t2 arg2; \
    t3 arg3; \
    t4 arg4; \
    if (!gjs_parse_args(context, #method, fmt, argc, argv, \
                        #n1, &arg1, #n2, &arg2, #n3, &arg3, #n4, &arg4)) \
        return JS_FALSE; \
    cr = gjs_cairo_context_get_cr(context, obj); \
    cfunc(cr, arg1, arg2, arg3, arg4); \
_GJS_CAIRO_CONTEXT_DEFINE_FUNC_END

#define _GJS_CAIRO_CONTEXT_DEFINE_FUNC5(method, cfunc, fmt, t1, n1, t2, n2, t3, n3, t4, n4, t5, n5) \
_GJS_CAIRO_CONTEXT_DEFINE_FUNC_BEGIN(method) \
    t1 arg1; \
    t2 arg2; \
    t3 arg3; \
    t4 arg4; \
    t5 arg5; \
    if (!gjs_parse_args(context, #method, fmt, argc, argv, \
                        #n1, &arg1, #n2, &arg2, #n3, &arg3, #n4, &arg4, #n5, &arg5)) \
        return JS_FALSE; \
    cr = gjs_cairo_context_get_cr(context, obj); \
    cfunc(cr, arg1, arg2, arg3, arg4, arg5); \
_GJS_CAIRO_CONTEXT_DEFINE_FUNC_END

#define _GJS_CAIRO_CONTEXT_DEFINE_FUNC6(method, cfunc, fmt, t1, n1, t2, n2, t3, n3, t4, n4, t5, n5, t6, n6) \
_GJS_CAIRO_CONTEXT_DEFINE_FUNC_BEGIN(method) \
    t1 arg1; \
    t2 arg2; \
    t3 arg3; \
    t4 arg4; \
    t5 arg5; \
    t6 arg6; \
    if (!gjs_parse_args(context, #method, fmt, argc, argv, \
                        #n1, &arg1, #n2, &arg2, #n3, &arg3, #n4, &arg4, #n5, &arg5, #n6, &arg6)) \
        return JS_FALSE; \
    cr = gjs_cairo_context_get_cr(context, obj); \
    cfunc(cr, arg1, arg2, arg3, arg4, arg5, arg6); \
_GJS_CAIRO_CONTEXT_DEFINE_FUNC_END

typedef struct {
    void *dummy;
    JSContext  *context;
    JSObject   *object;
    cairo_t * cr;
} GjsCairoContext;

GJS_DEFINE_PROTO("CairoContext", gjs_cairo_context)

GJS_DEFINE_PRIV_FROM_JS(GjsCairoContext, gjs_cairo_context_class);

static void
_gjs_cairo_context_construct_internal(JSContext *context,
                                      JSObject *obj,
                                      cairo_t *cr)
{
    GjsCairoContext *priv;

    priv = g_slice_new0(GjsCairoContext);

    g_assert(priv_from_js(context, obj) == NULL);
    JS_SetPrivate(context, obj, priv);

    priv->context = context;
    priv->object = obj;
    priv->cr = cr;
}

static JSBool
gjs_cairo_context_constructor(JSContext *context,
                          JSObject  *obj,
                          uintN      argc,
                          jsval     *argv,
                          jsval     *retval)
{
    JSObject *surface_wrapper;
    cairo_surface_t *surface;
    cairo_t *cr;
    cairo_status_t status;

    if (!gjs_check_constructing(context))
        return JS_FALSE;

    if (!gjs_parse_args(context, "Context", "o", argc, argv,
                        "surface", &surface_wrapper))
        return JS_FALSE;

    surface = gjs_cairo_surface_get_surface(context, surface_wrapper);
    if (!surface) {
        gjs_throw(context, "first argument to Context() should be a surface");
        return JS_FALSE;
    }

    cr = cairo_create(surface);
    status = cairo_status(cr);
    if (status != CAIRO_STATUS_SUCCESS) {
        gjs_throw(context, "Could not create context: %s",
                  cairo_status_to_string(status));
        return JS_FALSE;
    }

    _gjs_cairo_context_construct_internal(context, obj, cr);

    return JS_TRUE;
}

static void
gjs_cairo_context_finalize(JSContext *context,
                           JSObject  *obj)
{
    GjsCairoContext *priv;
    priv = priv_from_js(context, obj);
    if (priv == NULL)
        return;

    cairo_destroy(priv->cr);
    g_slice_free(GjsCairoContext, priv);
}

/* Properties */
static JSPropertySpec gjs_cairo_context_proto_props[] = {
    { NULL }
};

/* Methods */

_GJS_CAIRO_CONTEXT_DEFINE_FUNC5(arc, cairo_arc, "fffff",
                                double, xc, double, yc, double, radius, double, angle1, double, angle2)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC5(arcNegative, cairo_arc_negative, "fffff",
                                double, xc, double, yc, double, radius, double, angle1, double, angle2)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC6(curveTo, cairo_curve_to, "ffffff",
                                double, x1, double, y1, double, x2, double, y2, double, x3, double, y3)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC0(clip, cairo_clip)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC0(clipPreserve, cairo_clip_preserve)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC0AFFFF(clipExtents, cairo_clip_extents)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC0(closePath, cairo_close_path)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC0(copyPage, cairo_copy_page)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC0AFF(deviceToUser, cairo_device_to_user)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC0AFF(deviceToUserDistance, cairo_device_to_user_distance)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC0(fill, cairo_fill)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC0(fillPreserve, cairo_fill_preserve)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC0AFFFF(fillExtents, cairo_fill_extents)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC0I(getAntiAlias, cairo_get_antialias)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC0AFF(getDash, cairo_get_dash)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC0I(getFillRule, cairo_get_fill_rule)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC0I(getLineCap, cairo_get_line_cap)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC0I(getLineJoin, cairo_get_line_join)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC0F(getLineWidth, cairo_get_line_width)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC0F(getMiterLimit, cairo_get_miter_limit)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC0I(getOperator, cairo_get_operator)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC0F(getTolerance, cairo_get_tolerance)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC0(identityMatrix, cairo_identity_matrix)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC2(lineTo, cairo_line_to, "ff", double, x, double, y)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC2(moveTo, cairo_move_to, "ff", double, x, double, y)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC0(newPath, cairo_new_path)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC0(newSubPath, cairo_new_sub_path)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC0(paint, cairo_paint)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC1(paintWithAlpha, cairo_paint_with_alpha, "f", double, alpha)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC0AFFFF(pathExtents, cairo_path_extents)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC0(pushGroup, cairo_push_group)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC1(pushGroupWithContent, cairo_push_group_with_content, "i", cairo_content_t, content)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC0(popGroupToSource, cairo_pop_group_to_source)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC4(rectangle, cairo_rectangle, "ffff",
                                double, x, double, y, double, width, double, height)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC6(relCurveTo, cairo_rel_curve_to, "ffffff",
                                double, dx1, double, dy1, double, dx2, double, dy2, double, dx3, double, dy3)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC2(relLineTo, cairo_rel_line_to, "ff", double, dx, double, dy)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC2(relMoveTo, cairo_rel_move_to, "ff", double, dx, double, dy)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC0(resetClip, cairo_reset_clip)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC0(restore, cairo_restore)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC1(rotate, cairo_rotate, "f", double, angle)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC0(save, cairo_save)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC2(scale, cairo_scale, "ff", double, sx, double, sy)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC3(selectFontFace, cairo_select_font_face, "sii", const char*, family,
                                cairo_font_slant_t, slant, cairo_font_weight_t, weight)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC1(setAntiAlias, cairo_set_antialias, "i", cairo_antialias_t, antialias)
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
_GJS_CAIRO_CONTEXT_DEFINE_FUNC1(showText, cairo_show_text, "s", const char*, utf8)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC0(stroke, cairo_stroke)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC0(strokePreserve, cairo_stroke_preserve)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC0AFFFF(strokeExtents, cairo_stroke_extents)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC2(translate, cairo_translate, "ff", double, tx, double, ty)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC0AFF(userToDevice, cairo_user_to_device)
_GJS_CAIRO_CONTEXT_DEFINE_FUNC0AFF(userToDeviceDistance, cairo_user_to_device_distance)

static JSFunctionSpec gjs_cairo_context_proto_funcs[] = {
    // appendPath
    { "arc", arc_func, 0, 0 },
    { "arcNegative", arcNegative_func, 0, 0 },
    { "clip", clip_func, 0, 0 },
    { "clipExtents", clipExtents_func, 0, 0 },
    { "clipPreserve", clipPreserve_func, 0, 0 },
    { "closePath", closePath_func, 0, 0 },
    { "copyPage", copyPage_func, 0, 0 },
    // copyPath
    // copyPathFlat
    { "curveTo", curveTo_func, 0, 0 },
    { "deviceToUser", deviceToUser_func, 0, 0 },
    { "deviceToUserDistance", deviceToUserDistance_func, 0, 0 },
    { "fill", fill_func, 0, 0 },
    { "fillPreserve", fillPreserve_func, 0, 0 },
    { "fillExtents", fillExtents_func, 0, 0 },
    // fontExtents
    { "getAntiAlias", getAntiAlias_func, 0, 0 },
    // getCurrentPoint
    { "getDash", getDash_func, 0, 0 },
    // getDashCount
    { "getFillRule", getFillRule_func, 0, 0 },
    // getFontFace
    // getFontMatrix
    // getFontOptions
    // getGroupTarget
    { "getLineCap", getLineCap_func, 0, 0 },
    { "getLineJoin", getLineJoin_func, 0, 0 },
    { "getLineWidth", getLineWidth_func, 0, 0 },
    // getMatrix
    { "getMiterLimit", getMiterLimit_func, 0, 0 },
    { "getOperator", getOperator_func, 0, 0 },
    // getScaledFont
    // getSource
    // getTarget
    { "getTolerance", getTolerance_func, 0, 0 },
    // glyphPath
    // glyphExtents
    // hasCurrentPoint
    { "identityMatrix", identityMatrix_func, 0, 0 },
    // inFill
    // inStroke
    { "lineTo", lineTo_func, 0, 0 },
    // mask
    // maskSurface
    { "moveTo", moveTo_func, 0, 0 },
    { "newPath", newPath_func, 0, 0 },
    { "newSubPath", newSubPath_func, 0, 0 },
    { "paint", paint_func, 0, 0 },
    { "paintWithAlpha", paintWithAlpha_func, 0, 0 },
    { "pathExtents", pathExtents_func, 0, 0 },
    // popGroup
    { "popGroupToSource", popGroupToSource_func, 0, 0 },
    { "pushGroup", pushGroup_func, 0, 0 },
    { "pushGroupWithContent", pushGroupWithContent_func, 0, 0 },
    { "rectangle", rectangle_func, 0, 0 },
    { "relCurveTo", relCurveTo_func, 0, 0 },
    { "relLineTo", relLineTo_func, 0, 0 },
    { "relMoveTo", relMoveTo_func, 0, 0 },
    { "resetClip", resetClip_func, 0, 0 },
    { "restore", restore_func, 0, 0 },
    { "rotate", rotate_func, 0, 0 },
    { "save", save_func, 0, 0 },
    { "scale", scale_func, 0, 0 },
    { "selectFontFace", selectFontFace_func, 0, 0 },
    { "setAntiAlias", setAntiAlias_func, 0, 0 },
    // setDash
    // setFontFace
    // setFontMatrix
    // setFontOptions
    { "setFontSize", setFontSize_func, 0, 0 },
    { "setFillRule", setFillRule_func, 0, 0 },
    { "setLineCap", setLineCap_func, 0, 0 },
    { "setLineJoin", setLineJoin_func, 0, 0 },
    { "setLineWidth", setLineWidth_func, 0, 0 },
    // setMatrix
    { "setMiterLimit", setMiterLimit_func, 0, 0 },
    { "setOperator", setOperator_func, 0, 0 },
    // setScaledFont
    // setSource
    { "setSourceRGB", setSourceRGB_func, 0, 0 },
    { "setSourceRGBA", setSourceRGBA_func, 0, 0 },
    // setSourceSurface
    { "setTolerance", setTolerance_func, 0, 0 },
    // showGlyphs
    { "showPage", showPage_func, 0, 0 },
    { "showText", showText_func, 0, 0 },
    // showTextGlyphs
    { "stroke", stroke_func, 0, 0 },
    { "strokeExtents", strokeExtents_func, 0, 0 },
    { "strokePreserve", strokePreserve_func, 0, 0 },
    // textPath
    // textExtends
    // transform
    { "translate", translate_func, 0, 0 },
    { "userToDevice", userToDevice_func, 0, 0 },
    { "userToDeviceDistance", userToDeviceDistance_func, 0, 0 },
    { NULL }
};

JSObject *
gjs_cairo_context_from_cr(JSContext *context,
                          cairo_t *cr)
{
    JSObject *object;

    object = JS_NewObject(context, &gjs_cairo_context_class, NULL, NULL);
    if (!object)
        return NULL;

    _gjs_cairo_context_construct_internal(context, object, cr);

    return object;
}

cairo_t *
gjs_cairo_context_get_cr(JSContext *context,
                         JSObject *object)
{
    GjsCairoContext *priv;
    priv = priv_from_js(context, object);
    if (priv == NULL)
        return NULL;

    return priv->cr;
}
