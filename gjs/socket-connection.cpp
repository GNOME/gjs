// SPDX-License-Identifier: LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2019 Igalia, S.L.
// SPDX-FileCopyrightText: 2021 Evan Welsh <contact@evanwelsh.com>

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
#include <string>
#include <utility>
#include <vector>

#include "gjs/atoms.h"
#include "gjs/context-private.h"
#include "gjs/context.h"
#include "gjs/global.h"
#include "gjs/jsapi-util-args.h"
#include "gjs/jsapi-util.h"
#include "gjs/macros.h"
#include "gjs/remote-server.h"
#include "gjs/socket-connection.h"

static constexpr unsigned kDefaultBufferSize = 4096;

SocketConnection::SocketConnection(int32_t id, GSocketConnection* connection,
                                   RemoteDebuggingServer* server)
    : m_id(id), m_server(server), m_connection(connection) {
    g_object_ref(m_connection);

    m_readBuffer.reserve(kDefaultBufferSize);
    m_writeBuffer.reserve(kDefaultBufferSize);

    GSocket* socket = g_socket_connection_get_socket(m_connection);
    g_socket_set_blocking(socket, FALSE);

    m_readMonitor.start(socket, G_IO_IN,
                        [this](GIOCondition condition) -> bool {
                            if (isClosed())
                                return G_SOURCE_REMOVE;

                            if (condition & G_IO_HUP || condition & G_IO_ERR ||
                                condition & G_IO_NVAL) {
                                didClose();
                                return G_SOURCE_REMOVE;
                            }

                            g_assert(condition & G_IO_IN);
                            return read();
                        });
}

SocketConnection::~SocketConnection() {
    m_server = nullptr;

    g_clear_object(&m_connection);
}

bool SocketConnection::read() {
    while (true) {
        size_t previousBufferSize = m_readBuffer.size();
        if (m_readBuffer.capacity() - previousBufferSize <= 0)
            m_readBuffer.reserve(m_readBuffer.capacity() + kDefaultBufferSize);

        GError* error = nullptr;
        char bytes[kDefaultBufferSize];
        auto bytesRead =
            g_socket_receive(g_socket_connection_get_socket(m_connection),
                             bytes, kDefaultBufferSize, nullptr, &error);

        if (bytesRead == -1) {
            if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_WOULD_BLOCK)) {
                g_error_free(error);
                m_readBuffer.shrink_to_fit();
                break;
            }

            g_warning("Error reading from socket connection: %s\n",
                      error->message);
            g_error_free(error);
            // didClose();
            return G_SOURCE_CONTINUE;
        }

        if (!bytesRead) {
            didClose();
            return G_SOURCE_REMOVE;
        }

        std::move(bytes, bytes + bytesRead, std::back_inserter(m_readBuffer));

        m_readBuffer.shrink_to_fit();

        readMessage();
        if (isClosed())
            return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

bool SocketConnection::readMessage() {
    if (m_readBuffer.size() == 0)
        return false;

    std::string content;
    content.reserve(m_readBuffer.size());
    content = std::string(m_readBuffer.begin(), m_readBuffer.end());

    m_readBuffer.erase(m_readBuffer.begin(),
                       m_readBuffer.begin() + content.size());
    m_readBuffer.shrink_to_fit();

    m_server->triggerReadCallback(m_id, content);
    return true;
}

void SocketConnection::sendMessage(const char* bytes, size_t bytes_len) {
    size_t previousBufferSize = m_writeBuffer.size();

    m_writeBuffer.reserve(previousBufferSize + bytes_len);

    std::move(bytes, bytes + bytes_len, std::back_inserter(m_writeBuffer));

    write();
}

void SocketConnection::write() {
    if (isClosed()) {
        printf("write abort\n");
        return;
    }

    GError* error = nullptr;
    auto bytesWritten = g_socket_send(
        g_socket_connection_get_socket(m_connection), m_writeBuffer.data(),
        m_writeBuffer.size(), nullptr, &error);

    if (bytesWritten == -1) {
        if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_WOULD_BLOCK)) {
            waitForSocketWritability();
            g_error_free(error);
            return;
        }

        g_warning("Error sending message on socket connection: %s\n",
                  error->message);
        g_error_free(error);
        didClose();
        return;
    }

    m_writeBuffer.erase(m_writeBuffer.begin(),
                        m_writeBuffer.begin() + bytesWritten);
    m_writeBuffer.shrink_to_fit();

    if (!m_writeBuffer.empty())
        waitForSocketWritability();
}

void SocketConnection::waitForSocketWritability() {
    if (m_writeMonitor.isActive())
        return;

    m_writeMonitor.start(
        g_socket_connection_get_socket(m_connection), G_IO_OUT,
        [this, protectedThis = this->ref()](GIOCondition condition) -> bool {
            if (condition & G_IO_OUT) {
                // We can't stop the monitor from this lambda,
                // because stop destroys the lambda.
                // TODO(ewlsh): Keep alive...
                g_idle_add(
                    [](void* user_data) -> gboolean {
                        auto self =
                            reinterpret_cast<SocketConnection*>(user_data);
                        self->m_writeMonitor.stop();
                        self->write();
                        return false;
                    },
                    this);
            }
            return G_SOURCE_REMOVE;
        });
}

void SocketConnection::close() {
    m_readMonitor.stop();
    m_writeMonitor.stop();
    m_connection = nullptr;
}

void SocketConnection::didClose() {
    if (isClosed())
        return;

    close();
}
