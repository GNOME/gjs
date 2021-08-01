// SPDX-License-Identifier: LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2019 Igalia, S.L.
// SPDX-FileCopyrightText: 2021 Evan Welsh <contact@evanwelsh.com>

#ifndef GJS_SOCKET_CONNECTION_H_
#define GJS_SOCKET_CONNECTION_H_

#include <config.h>  // for HAVE_READLINE_READLINE_H, HAVE_UNISTD_H

#include <stdint.h>
#include <stdio.h>  // for feof, fflush, fgets, stdin, stdout

#ifdef HAVE_READLINE_READLINE_H
#    include <readline/history.h>
#    include <readline/readline.h>
#endif

#ifdef HAVE_UNISTD_H
#    include <unistd.h>  // for isatty, STDIN_FILENO
#elif defined(_WIN32)
#    include <io.h>
#    ifndef STDIN_FILENO
#        define STDIN_FILENO 0
#    endif
#endif

#include <gio/gio.h>
#include <glib.h>
#include <js/CallArgs.h>
#include <js/PropertySpec.h>
#include <js/RootingAPI.h>
#include <js/TypeDecls.h>
#include <js/Utility.h>  // for UniqueChars
#include <js/Value.h>
#include <jsapi.h>  // for JS_DefineFunctions, JS_NewStringCopyZ
#include <functional>
#include <memory>
#include <vector>
#include "gjs/atoms.h"
#include "gjs/context-private.h"
#include "gjs/context.h"
#include "gjs/global.h"
#include "gjs/jsapi-util-args.h"
#include "gjs/jsapi-util.h"
#include "gjs/macros.h"
#include "gjs/socket-monitor.h"

class RemoteDebuggingServer;

class SocketConnection : std::enable_shared_from_this<SocketConnection> {
 public:
    typedef void (*MessageCallback)(SocketConnection&, const char*, size_t,
                                    gpointer);
    static std::shared_ptr<SocketConnection> create(
        int32_t id, GSocketConnection* connection,
        RemoteDebuggingServer* debug_server) {
        return std::make_shared<SocketConnection>(id, connection, debug_server);
    }
    std::vector<std::shared_ptr<SocketConnection>> m_keep_alive;
    ~SocketConnection();

    int32_t id() { return m_id; }

    void sendMessage(const char*, size_t);

    bool isClosed() const { return !m_connection; }
    void close();

    std::shared_ptr<SocketConnection> ref() { return shared_from_this(); }

 public:
    SocketConnection(int32_t, GSocketConnection*, RemoteDebuggingServer*);

 private:
    static gboolean idle_stop(void* user_data);
    bool read();
    bool readMessage();
    void write();
    void waitForSocketWritability();
    void didClose();

    int32_t m_id;
    RemoteDebuggingServer* m_server;
    GSocketConnection* m_connection;
    std::vector<char> m_readBuffer;
    GSocketMonitor m_readMonitor;
    std::vector<char> m_writeBuffer;
    GSocketMonitor m_writeMonitor;
};

#endif  // GJS_SOCKET_CONNECTION_H_
