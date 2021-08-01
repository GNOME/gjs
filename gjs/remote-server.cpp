// SPDX-License-Identifier: LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2017 Igalia S.L.
// SPDX-FileCopyrightText: 2021 Evan Welsh <contact@evanwelsh.com>

#include <config.h>

#include <gio/gio.h>

#include <utility>

#include "gjs/remote-server.h"
#include "gjs/socket-connection.h"
#include "gjs/socket-monitor.h"

static void trace_remote_global(JSTracer* trc, void* data) {
    auto* remote_server = static_cast<RemoteDebuggingServer*>(data);

    remote_server->trace(trc);
}

RemoteDebuggingServer::RemoteDebuggingServer(JSContext* cx,
                                             JS::HandleObject debug_global)
    : m_connection_id(0) {
    m_cx = cx;

    m_debug_global = debug_global;

    JS_AddExtraGCRootsTracer(m_cx, trace_remote_global, this);
}

RemoteDebuggingServer::~RemoteDebuggingServer() {
    if (m_service)
        g_signal_handlers_disconnect_matched(m_service, G_SIGNAL_MATCH_DATA, 0,
                                             0, nullptr, nullptr, this);

    JS_RemoveExtraGCRootsTracer(m_cx, trace_remote_global, this);

    m_debug_global = nullptr;
    m_cx = nullptr;
}

bool RemoteDebuggingServer::start(const char* address, unsigned port) {
    m_service = g_socket_service_new();
    g_signal_connect(m_service, "incoming",
                     G_CALLBACK(incomingConnectionCallback), this);

    GjsAutoUnref<GSocketAddress> socketAddress =
        (g_inet_socket_address_new_from_string(address, port));
    GError* error = nullptr;
    if (!g_socket_listener_add_address(
            G_SOCKET_LISTENER(m_service), socketAddress.get(),
            G_SOCKET_TYPE_STREAM, G_SOCKET_PROTOCOL_TCP, nullptr, nullptr,
            &error)) {
        g_warning("Failed to start remote debugging server on %s:%u: %s\n",
                  address, port, error->message);
        g_error_free(error);
        return false;
    }

    return true;
}

void RemoteDebuggingServer::trace(JSTracer* trc) {
    JS::TraceEdge(trc, &m_debug_global, "Debug Global");
}

void RemoteDebuggingServer::triggerReadCallback(int32_t connection_id,
                                                std::string content) {
    JSAutoRealm ar(m_cx, m_debug_global);
    JS::RootedObject global(m_cx, m_debug_global);

    JS::RootedValue ignore_rval(m_cx);
    JS::RootedValueArray<2> args(m_cx);
    args[0].setInt32(connection_id);
    if (!gjs_string_from_utf8_n(m_cx, content.data(), content.size(), args[1]))
        return;

    if (!JS_CallFunctionName(m_cx, global, "onReadMessage", args,
                             &ignore_rval)) {
        gjs_log_exception_uncaught(m_cx);
    }
}

void RemoteDebuggingServer::triggerConnectionCallback(int32_t connection_id) {
    JSAutoRealm ar(m_cx, m_debug_global);
    JS::RootedObject global(m_cx, m_debug_global);

    JS::RootedValue ignore_rval(m_cx);
    JS::RootedValueArray<1> args(m_cx);
    args[0].setInt32(connection_id);

    if (!JS_CallFunctionName(m_cx, global, "onConnection", args,
                             &ignore_rval)) {
        gjs_log_exception_uncaught(m_cx);
    }
}

gboolean RemoteDebuggingServer::incomingConnectionCallback(
    GSocketService*, GSocketConnection* connection, GObject*, void* user_data) {
    auto* debuggingServer = static_cast<RemoteDebuggingServer*>(user_data);

    debuggingServer->incomingConnection(connection);
    return true;
}

void RemoteDebuggingServer::incomingConnection(GSocketConnection* connection) {
    // Increment connection id...
    m_connection_id++;

    std::shared_ptr<SocketConnection> socket_connection =
        SocketConnection::create(m_connection_id, connection, this);

    int32_t id = socket_connection->id();
    m_connections.insert_or_assign(id, std::move(socket_connection));

    triggerConnectionCallback(id);
}

void RemoteDebuggingServer::connectionDidClose(
    std::shared_ptr<SocketConnection> clientConnection) {
    m_connections.erase(clientConnection->id());
}

bool RemoteDebuggingServer::sendMessage(int32_t connection_id,
                                        const char* message,
                                        size_t message_len) {
    ConnectionMap::const_iterator connection =
        m_connections.find(connection_id);
    if (connection == m_connections.end())
        return false;

    connection->second->sendMessage(message, message_len);
    return true;
}

bool gjs_socket_connection_write_message(JSContext* cx, unsigned argc,
                                         JS::Value* vp) {
    g_assert(gjs_global_is_type(cx, GjsGlobalType::DEBUGGER) &&
             "Global is debugger");

    auto server = static_cast<RemoteDebuggingServer*>(
        gjs_get_global_slot(JS::CurrentGlobalOrNull(cx),
                            GjsDebuggerGlobalSlot::REMOTE_SERVER)
            .toPrivate());
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);

    int32_t connection_id;
    JS::UniqueChars message;
    if (!gjs_parse_call_args(cx, "writeMessage", args, "is", "connection_id",
                             &connection_id, "message", &message))
        return false;

    server->sendMessage(connection_id, message.get(), strlen(message.get()));
    return true;
}

bool gjs_start_remote_debugging(JSContext* cx, unsigned argc, JS::Value* vp) {
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
    g_assert(gjs_global_is_type(cx, GjsGlobalType::DEBUGGER) &&
             "Global is debugger");
    auto server = static_cast<RemoteDebuggingServer*>(
        gjs_get_global_slot(JS::CurrentGlobalOrNull(cx),
                            GjsDebuggerGlobalSlot::REMOTE_SERVER)
            .toPrivate());

    uint32_t port;
    if (!gjs_parse_call_args(cx, "start", args, "u", "port", &port))
        return false;

    return server->start("0.0.0.0", port);
}
