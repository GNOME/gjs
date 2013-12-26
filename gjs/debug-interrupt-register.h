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
#ifndef GJS_DEBUG_INTERRUPT_REGISTER_H
#define GJS_DEBUG_INTERRUPT_REGISTER_H

#include <glib-object.h>

G_BEGIN_DECLS

#define GJS_TYPE_DEBUG_INTERRUPT_REGISTER gjs_debug_interrupt_register_get_type()

#define GJS_DEBUG_INTERRUPT_REGISTER(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), \
     GJS_TYPE_DEBUG_INTERRUPT_REGISTER, GjsDebugInterruptRegister))

#define GJS_DEBUG_INTERRUPT_REGISTER_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass), \
     GJS_TYPE_DEBUG_INTERRUPT_REGISTER, GjsDebugInterruptRegisterClass))

#define GJS_IS_DEBUG_INTERRUPT_REGISTER(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
     GJS_TYPE_DEBUG_INTERRUPT_REGISTER))

#define GJS_IS_DEBUG_INTERRUPT_REGISTER_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE ((klass), \
     GJS_TYPE_DEBUG_INTERRUPT_REGISTER))

#define GJS_DEBUG_INTERRUPT_REGISTER_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS ((obj), \
     GJS_TYPE_DEBUG_INTERRUPT_REGISTER, GjsDebugInterruptRegisterClass))

typedef struct _GjsDebugInterruptRegister GjsDebugInterruptRegister;
typedef struct _GjsDebugInterruptRegisterClass GjsDebugInterruptRegisterClass;
typedef struct _GjsDebugInterruptRegisterPrivate GjsDebugInterruptRegisterPrivate;

typedef struct _GjsContext GjsContext;

struct _GjsDebugInterruptRegisterClass
{
    GObjectClass parent_class;
};

struct _GjsDebugInterruptRegister
{
    GObject parent;

    /*< private >*/
    GjsDebugInterruptRegisterPrivate *priv;
};

GType gjs_debug_interrupt_register_get_type(void);

GjsDebugInterruptRegister * gjs_debug_interrupt_register_new(GjsContext *context);

G_END_DECLS

#endif
