// SPDX-License-Identifier: LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2015 Igalia S.L.
// SPDX-FileCopyrightText: 2021 Evan Welsh <contact@evanwelsh.com>

#ifndef GJS_SOCKET_MONITOR_H_
#define GJS_SOCKET_MONITOR_H_

#include <gio/gio.h>
#include <glib.h>
#include <functional>
#include "gjs/jsapi-util.h"

typedef struct _GSocket GSocket;

class GSocketMonitor {
 public:
    GSocketMonitor() = default;
    ~GSocketMonitor();

    void start(GSocket*, GIOCondition, std::function<bool(GIOCondition)>&&);
    void stop();
    bool isActive() const { return !!m_source; }

 private:
    static gboolean socketSourceCallback(GSocket*, GIOCondition,
                                         GSocketMonitor*);

    GSource* m_source;
    GCancellable* m_cancellable;
    std::function<bool(GIOCondition)> m_callback;
};

#endif  // GJS_SOCKET_MONITOR_H_
