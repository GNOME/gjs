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
 * Authored By: Sam Spilsbury <sam.spilsbury@canonical.com>
 */
#include <gjs/interrupt-register.h>

static void gjs_interrupt_register_interface_default_init(GjsInterruptRegisterInterface *settings_interface);

G_DEFINE_INTERFACE(GjsInterruptRegister, gjs_interrupt_register_interface, G_TYPE_OBJECT);

static void
gjs_interrupt_register_interface_default_init(GjsInterruptRegisterInterface *settings_interface)
{
}

GjsDebugConnection *
gjs_interrupt_register_add_breakpoint(GjsInterruptRegister *reg,
                                      const gchar          *filename,
                                      guint                line,
                                      GjsInterruptCallback callback,
                                      gpointer             user_data)
{
    g_return_val_if_fail (reg, NULL);
    g_return_val_if_fail (filename, NULL);
    g_return_val_if_fail (callback, NULL);

    return GJS_INTERRUPT_REGISTER_GET_INTERFACE(reg)->add_breakpoint(reg,
                                                                     filename,
                                                                     line,
                                                                     callback,
                                                                    user_data);
}

GjsDebugConnection *
gjs_interrupt_register_start_singlestep(GjsInterruptRegister *reg,
                                        GjsInterruptCallback callback,
                                        gpointer             user_data)
{
  return GJS_INTERRUPT_REGISTER_GET_INTERFACE(reg)->start_singlestep(reg,
                                                                     callback,
                                                                     user_data);
}

GjsDebugConnection *
gjs_interrupt_register_connect_to_script_load(GjsInterruptRegister *reg,
                                              GjsInfoCallback      callback,
                                              gpointer             user_data)
{
  return GJS_INTERRUPT_REGISTER_GET_INTERFACE(reg)->connect_to_script_load(reg,
                                                                           callback,
                                                                           user_data);
}

GjsDebugConnection *
gjs_interrupt_register_connect_to_function_calls_and_execution(GjsInterruptRegister *reg,
                                                               GjsFrameCallback     callback,
                                                               gpointer             user_data)
{
  return GJS_INTERRUPT_REGISTER_GET_INTERFACE(reg)->connect_to_function_calls_and_execution(reg,
                                                                                            callback,
                                                                                            user_data);
}
