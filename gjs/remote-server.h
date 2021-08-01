// SPDX-License-Identifier: LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2017 Igalia S.L.
// SPDX-FileCopyrightText: 2021 Evan Welsh <contact@evanwelsh.com>

#ifndef GJS_REMOTE_SERVER_H_
#define GJS_REMOTE_SERVER_H_

#include <stdint.h>
#include <algorithm>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "gjs/socket-connection.h"
#include "gjs/socket-monitor.h"

typedef struct _GSocketConnection GSocketConnection;
typedef struct _GSocketService GSocketService;

class SocketConnection;

using ConnectionMap =
    std::unordered_map<int32_t, std::shared_ptr<SocketConnection>>;

class RemoteDebuggingServer {
    int32_t m_connection_id;

 public:
    RemoteDebuggingServer(JSContext* cx, JS::HandleObject debug_global);
    ~RemoteDebuggingServer();

    bool start(const char* address, unsigned port);

 private:
    static gboolean incomingConnectionCallback(GSocketService*,
                                               GSocketConnection*, GObject*,
                                               void*);
    void incomingConnection(GSocketConnection* connection);

    void connectionDidClose(std::shared_ptr<SocketConnection> clientConnection);

    JSContext* m_cx;
    GSocketService* m_service;
    JS::Heap<JSObject*> m_debug_global;
    ConnectionMap m_connections;

 public:
    void trace(JSTracer* trc);
    void triggerReadCallback(int32_t connection_id, std::string content);
    void triggerConnectionCallback(int32_t connection_id);
    bool sendMessage(int32_t connection_id, const char* message,
                     size_t message_len);
    bool isRunning() const { return m_service != nullptr; }
};

bool gjs_socket_connection_on_read_message(JSContext* cx, unsigned argc,
                                           JS::Value* vp);

bool gjs_socket_connection_write_message(JSContext* cx, unsigned argc,
                                         JS::Value* vp);
bool gjs_start_remote_debugging(JSContext* cx, unsigned argc, JS::Value* vp);

bool gjs_socket_connection_on_connection(JSContext* cx, unsigned argc,
                                         JS::Value* vp);

#endif  // GJS_REMOTE_SERVER_H_
