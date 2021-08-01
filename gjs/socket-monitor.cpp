// SPDX-License-Identifier: LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2015 Igalia S.L.
// SPDX-FileCopyrightText: 2021 Evan Welsh <contact@evanwelsh.com>

#include <config.h>

#include <gio/gio.h>
#include "gjs/socket-monitor.h"

#include <utility>

GSocketMonitor::~GSocketMonitor() { stop(); }

gboolean GSocketMonitor::socketSourceCallback(GSocket*, GIOCondition condition,
                                              GSocketMonitor* monitor) {
    if (monitor->m_cancellable &&
        g_cancellable_is_cancelled(monitor->m_cancellable))
        return G_SOURCE_REMOVE;
    return monitor->m_callback(condition);
}

void GSocketMonitor::start(GSocket* socket, GIOCondition condition,
                           std::function<bool(GIOCondition)>&& callback) {
    m_cancellable = g_cancellable_new();
    m_source = g_socket_create_source(socket, condition, m_cancellable);
    g_source_set_name(m_source, "[gjs] Socket monitor");

    m_callback = std::move(callback);
    g_source_set_callback(
        m_source,
        reinterpret_cast<GSourceFunc>(
            reinterpret_cast<GCallback>(socketSourceCallback)),
        this, nullptr);
    g_source_set_priority(m_source, G_PRIORITY_HIGH);
    g_source_attach(m_source, nullptr);
}

void GSocketMonitor::stop() {
    if (!m_source)
        return;

    g_cancellable_cancel(m_cancellable);
    m_cancellable = nullptr;
    g_source_destroy(m_source);
    m_source = nullptr;
    m_callback = nullptr;
}
