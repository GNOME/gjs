// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2010 litl, LLC.

/* exported Antialias, Content, Extend, FillRule, Filter, FontSlant, FontWeight,
Format, LineCap, LineJoin, Operator, PatternType, SurfaceType */

var Antialias = {
    DEFAULT: 0,
    NONE: 1,
    GRAY: 2,
    SUBPIXEL: 3,
};

var Content = {
    COLOR: 0x1000,
    ALPHA: 0x2000,
    COLOR_ALPHA: 0x3000,
};

var Extend = {
    NONE: 0,
    REPEAT: 1,
    REFLECT: 2,
    PAD: 3,
};

var FillRule = {
    WINDING: 0,
    EVEN_ODD: 1,
};

var Filter = {
    FAST: 0,
    GOOD: 1,
    BEST: 2,
    NEAREST: 3,
    BILINEAR: 4,
    GAUSSIAN: 5,
};

var FontSlant = {
    NORMAL: 0,
    ITALIC: 1,
    OBLIQUE: 2,
};

var FontWeight = {
    NORMAL: 0,
    BOLD: 1,
};

var Format = {
    ARGB32: 0,
    RGB24: 1,
    A8: 2,
    A1: 3,
    RGB16_565: 4,
};

var LineCap = {
    BUTT: 0,
    ROUND: 1,
    SQUARE: 2,
    /** @deprecated Historical typo of {@link LineCap.Square}, kept for compatibility reasons */
    SQUASH: 2,
};

var LineJoin = {
    MITER: 0,
    ROUND: 1,
    BEVEL: 2,
};

var Operator = {
    CLEAR: 0,
    SOURCE: 1,
    OVER: 2,
    IN: 3,
    OUT: 4,
    ATOP: 5,
    DEST: 6,
    DEST_OVER: 7,
    DEST_IN: 8,
    DEST_OUT: 9,
    DEST_ATOP: 10,
    XOR: 11,
    ADD: 12,
    SATURATE: 13,
    MULTIPLY: 14,
    SCREEN: 15,
    OVERLAY: 16,
    DARKEN: 17,
    LIGHTEN: 18,
    COLOR_DODGE: 19,
    COLOR_BURN: 20,
    HARD_LIGHT: 21,
    SOFT_LIGHT: 22,
    DIFFERENCE: 23,
    EXCLUSION: 24,
    HSL_HUE: 25,
    HSL_SATURATION: 26,
    HSL_COLOR: 27,
    HSL_LUMINOSITY: 28,
};

var PatternType = {
    SOLID: 0,
    SURFACE: 1,
    LINEAR: 2,
    RADIAL: 3,
};

var SurfaceType = {
    IMAGE: 0,
    PDF: 1,
    PS: 2,
    XLIB: 3,
    XCB: 4,
    GLITZ: 5,
    QUARTZ: 6,
    WIN32: 7,
    BEOS: 8,
    DIRECTFB: 9,
    SVG: 10,
    OS2: 11,
    WIN32_PRINTING: 12,
    QUARTZ_IMAGE: 13,
};

