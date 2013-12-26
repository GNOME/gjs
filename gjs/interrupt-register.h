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
#ifndef _GJS_INTERRUPT_REGISTER_H
#define _GJS_INTERRUPT_REGISTER_H

#include <glib-object.h>

G_BEGIN_DECLS

#define GJS_INTERRUPT_REGISTER_INTERFACE(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
                                               GJS_TYPE_INTERRUPT_REGISTER_INTERFACE, \
                                               GjsInterruptRegister))
#define GJS_INTERRUPT_REGISTER_GET_INTERFACE(obj) (G_TYPE_INSTANCE_GET_INTERFACE(obj, \
                                                                                 GJS_TYPE_INTERRUPT_REGISTER_INTERFACE, \
                                                                                 GjsInterruptRegisterInterface))
#define GJS_TYPE_INTERRUPT_REGISTER_INTERFACE (gjs_interrupt_register_interface_get_type())

typedef struct _GjsInterruptRegisterInterface GjsInterruptRegisterInterface;
typedef struct _GjsInterruptRegister GjsInterruptRegister;

typedef struct _GjsDebugConnection GjsDebugConnection;
typedef struct _GjsContext GjsContext;

typedef struct _GjsDebugScriptInfo
{
    const gchar *filename;
    guint       *executable_lines;
    guint       n_executable_lines;
} GjsDebugScriptInfo;

typedef struct _GjsInterruptInfo
{
    const gchar *filename;
    guint       line;
    const gchar *functionName;
} GjsInterruptInfo;

typedef enum _GjsFrameState
{
    GJS_INTERRUPT_FRAME_BEFORE = 0,
    GJS_INTERRUPT_FRAME_AFTER = 1
} GjsFrameState;

typedef struct _GjsFrameInfo
{
    GjsInterruptInfo interrupt;
    GjsFrameState    frame_state;
} GjsFrameInfo;

typedef void (*GjsFrameCallback)(GjsInterruptRegister *reg,
                                 GjsContext           *context,
                                 GjsFrameInfo         *info,
                                 gpointer             user_data);

typedef void (*GjsInterruptCallback)(GjsInterruptRegister *reg,
                                     GjsContext           *context,
                                     GjsInterruptInfo     *info,
                                     gpointer             user_data);

typedef void (*GjsInfoCallback) (GjsInterruptRegister *reg,
                                 GjsContext           *context,
                                 GjsDebugScriptInfo   *info,
                                 gpointer             user_data);

struct _GjsInterruptRegisterInterface
{
    GTypeInterface parent;

    GjsDebugConnection * (*add_breakpoint)(GjsInterruptRegister *reg,
                                            const gchar          *filename,
                                            guint                line,
                                            GjsInterruptCallback callback,
                                            gpointer             user_data);
    GjsDebugConnection * (*start_singlestep)(GjsInterruptRegister *reg,
                                             GjsInterruptCallback callback,
                                             gpointer             user_data);
    GjsDebugConnection * (*connect_to_script_load)(GjsInterruptRegister *reg,
                                                   GjsInfoCallback      callback,
                                                   gpointer             user_data);
    GjsDebugConnection * (*connect_to_function_calls_and_execution)(GjsInterruptRegister *reg,
                                                                    GjsFrameCallback     callback,
                                                                    gpointer             user_data);
};

GjsDebugConnection *
gjs_interrupt_register_add_breakpoint(GjsInterruptRegister *reg,
                                      const gchar          *filename,
                                      guint                line,
                                      GjsInterruptCallback callback,
                                      gpointer             user_data);

GjsDebugConnection *
gjs_interrupt_register_start_singlestep(GjsInterruptRegister *reg,
                                        GjsInterruptCallback callback,
                                        gpointer             user_data);

GjsDebugConnection *
gjs_interrupt_register_connect_to_script_load(GjsInterruptRegister *reg,
                                              GjsInfoCallback      callback,
                                              gpointer             user_data);

GjsDebugConnection *
gjs_interrupt_register_connect_to_function_calls_and_execution(GjsInterruptRegister *reg,
                                                               GjsFrameCallback     callback,
                                                               gpointer             user_data);

GType gjs_interrupt_register_interface_get_type(void);

G_END_DECLS

#endif
