// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: 2021 Evan Welsh <contact@evanwelsh.com>

#pragma once

#include <config.h>

#include <memory>

#include <glib.h>

#include <js/TypeDecls.h>

#include "gjs/jsapi-util.h"

class GjsContextPrivate;

using GjsAutoMainContext =
    GjsAutoPointer<GMainContext, GMainContext, g_main_context_unref,
                   g_main_context_ref>;

namespace Gjs {

/**
 * @brief A class which wraps a custom GSource and handles associating it with a
 * GMainContext. While it is running, it will attach the source to the main
 * context so that promise jobs are run at the appropriate time.
 */
class PromiseJobDispatcher {
    class Source;
    // The thread-default GMainContext
    GjsAutoMainContext m_main_context;
    // The custom source.
    std::unique_ptr<Source> m_source;

 public:
    explicit PromiseJobDispatcher(GjsContextPrivate*);
    ~PromiseJobDispatcher();

    /**
     * @brief Start (or resume) dispatching jobs from the promise job queue
     */
    void start();

    /**
     * @brief Stop dispatching
     */
    void stop();

    /**
     * @brief Whether the dispatcher is currently running
     */
    bool is_running();
};

};  // namespace Gjs

bool gjs_define_native_promise_stuff(JSContext* cx,
                                     JS::MutableHandleObject module);
