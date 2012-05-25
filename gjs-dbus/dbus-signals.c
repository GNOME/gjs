/* -*- mode: C; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/* Copyright 2008 litl, LLC. */

#include <config.h>

#include "dbus-private.h"
#include "util/log.h"

#include <string.h>

#define INVALID_SIGNAL_ID (-1)

typedef struct {
    DBusBusType            bus_type;
    int                    refcount;
    char                  *sender;
    char                  *path;
    char                  *iface;
    char                  *name;
    GjsDBusSignalHandler   handler;
    void                  *data;
    GDestroyNotify         data_dnotify;
    int                    id;
    unsigned int           matching : 1;
    unsigned int           destroyed : 1;
} GjsSignalWatcher;

static GSList *pending_signal_watchers = NULL;

static void signal_watcher_remove (DBusConnection   *connection,
                                   GjsDBusInfo      *info,
                                   GjsSignalWatcher *watcher);


static int global_handler_id = 0;

static GjsSignalWatcher*
signal_watcher_new(DBusBusType                  bus_type,
                   const char                  *sender,
                   const char                  *path,
                   const char                  *iface,
                   const char                  *name,
                   GjsDBusSignalHandler         handler,
                   void                        *data,
                   GDestroyNotify               data_dnotify)
{
    GjsSignalWatcher *watcher;

    watcher = g_slice_new0(GjsSignalWatcher);

    watcher->refcount = 1;

    watcher->bus_type = bus_type;
    watcher->sender = g_strdup(sender);
    watcher->path = g_strdup(path);
    watcher->iface = g_strdup(iface);
    watcher->name = g_strdup(name);
    watcher->handler = handler;
    watcher->id = global_handler_id++;
    watcher->data = data;
    watcher->data_dnotify = data_dnotify;

    return watcher;
}

static void
signal_watcher_dnotify(GjsSignalWatcher *watcher)
{
    if (watcher->data_dnotify != NULL) {
        (* watcher->data_dnotify) (watcher->data);
        watcher->data_dnotify = NULL;
    }
    watcher->destroyed = TRUE;
}

static void
signal_watcher_ref(GjsSignalWatcher *watcher)
{
    watcher->refcount += 1;
}

static void
signal_watcher_unref(GjsSignalWatcher *watcher)
{
    watcher->refcount -= 1;

    if (watcher->refcount == 0) {
        signal_watcher_dnotify(watcher);

        g_free(watcher->sender);
        g_free(watcher->path);
        g_free(watcher->iface);
        g_free(watcher->name);

        g_slice_free(GjsSignalWatcher, watcher);
    }
}

static char*
signal_watcher_build_match_rule(GjsSignalWatcher *watcher)
{
    GString *s;

    s = g_string_new("type='signal'");

    if (watcher->sender) {
        g_string_append_printf(s, ",sender='%s'", watcher->sender);
    }

    if (watcher->path) {
        g_string_append_printf(s, ",path='%s'", watcher->path);
    }

    if (watcher->iface) {
        g_string_append_printf(s, ",interface='%s'", watcher->iface);
    }

    if (watcher->name) {
        g_string_append_printf(s, ",member='%s'", watcher->name);
    }

    return g_string_free(s, FALSE);
}


static GSList*
signal_watcher_table_lookup(GHashTable *table,
                            const char *key)
{
    if (table == NULL) {
        return NULL;
    }

    return g_hash_table_lookup(table, key);
}

static void
signal_watcher_list_free(void *data)
{
    GSList *l = data;
    while (l != NULL) {
        GSList *next = l->next;
        signal_watcher_unref(l->data);
        g_slist_free_1(l);
        l = next;
    }
}

static void
signal_watcher_table_add(GHashTable      **table_p,
                         const char       *key,
                         GjsSignalWatcher *watcher)
{
    GSList *list;
    char *original_key;

    if (*table_p == NULL) {
        list = NULL;
        original_key = g_strdup(key);
        *table_p = g_hash_table_new_full(g_str_hash,
                                         g_str_equal,
                                         g_free,
                                         signal_watcher_list_free);
    } else {
        gpointer k, v;
        if (g_hash_table_lookup_extended(*table_p,
                                          key,
                                          &k, &v)) {
            // do this assignment as separate step so as to avoid
            // passing a type-punned pointer into g_hash_table_lookup_extended
            original_key = (char *) k;
            list = (GSList *) v;
        } else {
            original_key = g_strdup(key);
            list = NULL;
        }
    }

    list = g_slist_prepend(list, watcher);
    signal_watcher_ref(watcher);

    g_hash_table_steal(*table_p, key);
    g_hash_table_insert(*table_p, original_key, list);
}

static void
signal_watcher_table_remove(GHashTable       *table,
                            const char       *key,
                            GjsSignalWatcher *watcher)
{
    GSList *list;
    GSList *l;
    char *original_key;
    gpointer k, v;

    if (table == NULL)
        return; /* Never lazily-created the table, nothing ever added */

    if (!g_hash_table_lookup_extended(table,
                                      key,
                                      &k, &v)) {
        return;
    }
    // do this assignment as separate step so as to avoid
    // passing a type-punned pointer into g_hash_table_lookup_extended
    original_key = (char *) k;
    list = (GSList *) v;

    l = g_slist_find(list, watcher);
    if (!l)
        return; /* we don't want to unref if we weren't in this table */

    list = g_slist_delete_link(list, l);

    g_hash_table_steal(table, key);
    if (list != NULL) {
        g_hash_table_insert(table, original_key, list);
    } else {
        g_free(original_key);
    }

    signal_watcher_unref(watcher);
}

static void
signal_emitter_name_appeared(DBusConnection *connection,
                             const char     *name,
                             const char     *new_owner_unique_name,
                             void           *data)
{
    /* We don't need to do anything here, we installed a name watch so
     * we could call gjs_dbus_get_watched_name_owner() to dispatch
     * signals, and to get destroy notification on unique names.
     */
}

static void
signal_emitter_name_vanished(DBusConnection *connection,
                             const char     *name,
                             const char     *old_owner_unique_name,
                             void           *data)
{
  gjs_debug(GJS_DEBUG_DBUS, "Signal emitter '%s' is now gone",
            name);

    /* If a watcher is matching on a unique name sender, once the unique
     * name goes away, the watcher can never see anything so nuke it.
     */
    if (*name == ':') {
        GSList *list;
        GjsDBusInfo *info;

        info = _gjs_dbus_ensure_info(connection);

        list = signal_watcher_table_lookup(info->signal_watchers_by_unique_sender,
                                           name);

        if (list == NULL)
            return;

        /* copy the list since we're about to remove stuff from it
         * in signal_watcher_remove
         */
        list = g_slist_copy(list);
        while (list != NULL) {
            signal_watcher_remove(connection, info, list->data);
            list = g_slist_delete_link(list, list);
        }
    }
}

static GjsDBusWatchNameFuncs signal_emitter_name_funcs = {
    signal_emitter_name_appeared,
    signal_emitter_name_vanished
};

static void
signal_watcher_set_matching(DBusConnection   *connection,
                            GjsSignalWatcher *watcher,
                            gboolean          matching)
{
    char *rule;

    if (watcher->matching == (matching != FALSE)) {
        return;
    }

    /* Never add match on a destroyed signal watcher */
    if (watcher->destroyed && matching)
        return;

    /* We can't affect match rules if not connected */
    if (!dbus_connection_get_is_connected(connection)) {
        return;
    }

    watcher->matching = matching != FALSE;

    rule = signal_watcher_build_match_rule(watcher);

    if (matching)
        dbus_bus_add_match(connection,
                           rule, NULL); /* asking for error would make this block */
    else
        dbus_bus_remove_match(connection, rule, NULL);

    g_free(rule);

    if (watcher->sender) {
        /* If the signal is from a well-known name, we have to add
         * a name watch to know who owns that name.
         *
         * If the signal is from a unique name, we want to destroy
         * the watcher if the unique name goes away
         */
        if (matching) {
            gjs_dbus_watch_name(watcher->bus_type,
                                watcher->sender,
                                0,
                                &signal_emitter_name_funcs,
                                NULL);
        } else {
            gjs_dbus_unwatch_name(watcher->bus_type,
                                  watcher->sender,
                                  &signal_emitter_name_funcs,
                                  NULL);
        }
    }
}

static void
signal_watcher_add(DBusConnection   *connection,
                   GjsDBusInfo      *info,
                   GjsSignalWatcher *watcher)
{
    gboolean in_some_table;

    signal_watcher_set_matching(connection, watcher, TRUE);

    info->all_signal_watchers = g_slist_prepend(info->all_signal_watchers, watcher);
    signal_watcher_ref(watcher);

    in_some_table = FALSE;

    if (watcher->sender && *(watcher->sender) == ':') {
        signal_watcher_table_add(&info->signal_watchers_by_unique_sender,
                                 watcher->sender,
                                 watcher);
        in_some_table = TRUE;
    }

    if (watcher->path) {
        signal_watcher_table_add(&info->signal_watchers_by_path,
                                 watcher->path,
                                 watcher);
        in_some_table = TRUE;
    }

    if (watcher->iface) {
        signal_watcher_table_add(&info->signal_watchers_by_iface,
                                 watcher->iface,
                                 watcher);
        in_some_table = TRUE;
    }

    if (watcher->name) {
        signal_watcher_table_add(&info->signal_watchers_by_signal,
                                 watcher->name,
                                 watcher);
        in_some_table = TRUE;
    }

    if (!in_some_table) {
        info->signal_watchers_in_no_table =
            g_slist_prepend(info->signal_watchers_in_no_table,
                            watcher);
        signal_watcher_ref(watcher);
    }
}

static void
signal_watcher_remove(DBusConnection   *connection,
                      GjsDBusInfo      *info,
                      GjsSignalWatcher *watcher)
{
    gboolean in_some_table;

    signal_watcher_set_matching(connection, watcher, FALSE);

    info->all_signal_watchers = g_slist_remove(info->all_signal_watchers, watcher);

    in_some_table = FALSE;

    if (watcher->sender && *(watcher->sender) == ':') {
        signal_watcher_table_remove(info->signal_watchers_by_unique_sender,
                                    watcher->sender,
                                    watcher);
        in_some_table = TRUE;
    }

    if (watcher->path) {
        signal_watcher_table_remove(info->signal_watchers_by_path,
                                    watcher->path,
                                    watcher);
        in_some_table = TRUE;
    }

    if (watcher->iface) {
        signal_watcher_table_remove(info->signal_watchers_by_iface,
                                    watcher->iface,
                                    watcher);
        in_some_table = TRUE;
    }

    if (watcher->name) {
        signal_watcher_table_remove(info->signal_watchers_by_signal,
                                    watcher->name,
                                    watcher);
        in_some_table = TRUE;
    }

    if (!in_some_table) {
        info->signal_watchers_in_no_table =
            g_slist_remove(info->signal_watchers_in_no_table,
                           watcher);
        signal_watcher_unref(watcher);
    }

    /* Destroy-notify before dropping last ref for a little more safety
     * (avoids "resurrection" issues), and to ensure we call the destroy
     * notifier even if we don't finish finalizing just yet.
     */
    signal_watcher_dnotify(watcher);

    signal_watcher_unref(watcher);
}

/* This is called before we notify the app that the connection is open,
 * to add match rules. It must add the match rules, but MUST NOT
 * invoke application callbacks since the "connection opened"
 * callback needs to be first.
 */
void
_gjs_dbus_process_pending_signal_watchers(DBusConnection *connection,
                                          GjsDBusInfo    *info)
{
    GSList *remaining;

    remaining = NULL;
    while (pending_signal_watchers) {
        GjsSignalWatcher *watcher = pending_signal_watchers->data;
        pending_signal_watchers = g_slist_delete_link(pending_signal_watchers,
                                                      pending_signal_watchers);

        if (watcher->bus_type == info->bus_type) {
            /* Transfer to the non-pending GjsDBusInfo */
            signal_watcher_add(connection, info, watcher);
            signal_watcher_unref(watcher);
        } else {
            remaining = g_slist_prepend(remaining, watcher);
        }
    }

    /* keep the order deterministic by reversing, though I don't know
     * of a reason it matters.
     */
    pending_signal_watchers = g_slist_reverse(remaining);
}

static void
signal_watchers_disconnected(DBusConnection *connection,
                             GjsDBusInfo    *info)
{
    /* None should be pending on this bus, because at start of
     * _gjs_dbus_signal_watch_filter_message() we process all the pending ones.
     * However there could be stuff in pending_signal_watchers for
     * another bus. Anyway bottom line we can ignore pending_signal_watchers
     * in here.
     */
    GSList *list;
    GSList *destroyed;

    /* Build a separate list to destroy to avoid re-entrancy as we are
     * walking the list
     */
    destroyed = NULL;
    for (list = info->all_signal_watchers;
         list != NULL;
         list = list->next) {
        GjsSignalWatcher *watcher = list->data;
        if (watcher->sender && *(watcher->sender) == ':') {
            destroyed = g_slist_prepend(destroyed,
                                        watcher);
            signal_watcher_ref(watcher);
        }
    }

    while (destroyed != NULL) {
        GjsSignalWatcher *watcher = destroyed->data;
        destroyed = g_slist_delete_link(destroyed, destroyed);

        signal_watcher_remove(connection, info, watcher);
        signal_watcher_unref(watcher);
    }
}

static void
concat_candidates(GSList    **candidates_p,
                  GHashTable *table,
                  const char *key)
{
    GSList *list;

    list = signal_watcher_table_lookup(table, key);
    if (list == NULL)
        return;

    *candidates_p = g_slist_concat(*candidates_p,
                                   g_slist_copy(list));
}

static int
direct_cmp(gconstpointer a,
           gconstpointer b)
{
    /* gcc dislikes pointer math on void* so cast */
    return ((const char*)a) - ((const char*)b);
}

static gboolean
signal_watcher_watches(GjsDBusInfo      *info,
                       GjsSignalWatcher *watcher,
                       const char       *sender,
                       const char       *path,
                       const char       *iface,
                       const char       *name)
{
    if (watcher->path &&
        strcmp(watcher->path, path) != 0)
        return FALSE;

    if (watcher->iface &&
        strcmp(watcher->iface, iface) != 0)
        return FALSE;

    if (watcher->name &&
        strcmp(watcher->name, name) != 0)
        return FALSE;

    /* "sender" from message is always the unique name, but
     * watcher may or may not be.
     */

    if (watcher->sender == NULL)
        return TRUE;


    if (* (watcher->sender) == ':') {
        return strcmp(watcher->sender, sender) == 0;
    } else {
        const char *owner;

        owner = gjs_dbus_get_watched_name_owner(info->bus_type,
                                                watcher->sender);

        if (owner != NULL &&
            strcmp(sender, owner) == 0)
            return TRUE;
        else
            return FALSE;
    }
}

DBusHandlerResult
_gjs_dbus_signal_watch_filter_message(DBusConnection *connection,
                                      DBusMessage    *message,
                                      void           *data)
{
    /* Two things we're looking for
     * 1) signals
     * 2) if the sender of a signal watcher is a unique name,
     *    we want to destroy notify when it vanishes or
     *    when the bus disconnects.
     */
    GjsDBusInfo *info;
    const char *sender;
    const char *path;
    const char *iface;
    const char *name;
    GSList *candidates;
    GjsSignalWatcher *previous;

    info = _gjs_dbus_ensure_info(connection);

    /* Be sure they are all in the lookup tables */
    _gjs_dbus_process_pending_signal_watchers(connection, info);

    if (dbus_message_get_type(message) != DBUS_MESSAGE_TYPE_SIGNAL)
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    sender = dbus_message_get_sender(message);
    path = dbus_message_get_path(message);
    iface = dbus_message_get_interface(message);
    name = dbus_message_get_member(message);

    /* libdbus requires path, iface, name. The bus daemon
     * will always set a sender but some locally-generated
     * messages (e.g. disconnected) may not have one.
     */
    g_assert(path != NULL);
    g_assert(iface != NULL);
    g_assert(name != NULL);

    gjs_debug(GJS_DEBUG_DBUS, "Signal from %s %s.%s sender %s",
            path, iface, name, sender ? sender : "(none)");

    candidates = NULL;

    if (sender != NULL) {
        concat_candidates(&candidates,
                          info->signal_watchers_by_unique_sender,
                          sender);
    }
    concat_candidates(&candidates,
                      info->signal_watchers_by_path,
                      path);
    concat_candidates(&candidates,
                      info->signal_watchers_by_iface,
                      iface);
    concat_candidates(&candidates,
                      info->signal_watchers_by_signal,
                      name);
    candidates = g_slist_concat(candidates,
                                g_slist_copy(info->signal_watchers_in_no_table));

    /* Sort so we can find dups */
    candidates = g_slist_sort(candidates, direct_cmp);

    /* Ref everything so that calling a handler doesn't unref another
     * and possibly leave it dangling in our candidates list */
    g_slist_foreach(candidates, (GFunc)signal_watcher_ref, NULL);

    previous = NULL;
    while (candidates != NULL) {
        GjsSignalWatcher *watcher;

        watcher = candidates->data;
        candidates = g_slist_delete_link(candidates, candidates);

        if (previous == watcher)
            goto end_while; /* watcher was in more than one table */

        previous = watcher;

        if (!signal_watcher_watches(info,
                                    watcher,
                                    sender, path, iface, name))
            goto end_while;

        /* destroyed would happen if e.g. removed while we are going
         * through here.
         */
        if (watcher->destroyed)
            goto end_while;

        /* Invoke the watcher */

        (* watcher->handler) (connection,
                              message,
                              watcher->data);

    end_while:
        signal_watcher_unref(watcher);
    }

    /* Note that signal watchers can also listen to the disconnected
     * signal, so we do our special handling of it last
     */
    if (dbus_message_is_signal(message, DBUS_INTERFACE_LOCAL, "Disconnected")) {
        gjs_debug(GJS_DEBUG_DBUS, "Disconnected in %s", G_STRFUNC);

        signal_watchers_disconnected(connection, info);
    }

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

int
gjs_dbus_watch_signal(DBusBusType                  bus_type,
                      const char                  *sender,
                      const char                  *path,
                      const char                  *iface,
                      const char                  *name,
                      GjsDBusSignalHandler         handler,
                      void                        *data,
                      GDestroyNotify               data_dnotify)
{
    GjsSignalWatcher *watcher;
    DBusConnection *weak;

    watcher = signal_watcher_new(bus_type, sender, path,
                                 iface, name, handler,
                                 data, data_dnotify);

    /* If we're already connected, it's essential to get the
     * match rule added right away. Otherwise the race-free pattern
     * is not possible:
     * 1. Add match rule to monitor state of remote object
     * 2. Get current state of remote object
     *
     * Using the pending_signal_watchers codepath, there's no
     * notification when the match rule is added so you can't
     * be sure you get current state *after* that.
     *
     * Since we add our match rule here immediately if connected,
     * then apps can rely on first watching the signal, then
     * getting current state.
     *
     * In the connect idle, we process pending signal watchers
     * before calling any other app callbacks, so if someone
     * gets current state on connect, that will be after
     * all their match rules are added.
     */
    weak = gjs_dbus_get_weak_ref(bus_type);
    if (weak != NULL) {
        signal_watcher_add(weak, _gjs_dbus_ensure_info(weak), watcher);
        signal_watcher_unref(watcher);
    } else {
        pending_signal_watchers = g_slist_prepend(pending_signal_watchers, watcher);
        _gjs_dbus_ensure_connect_idle(bus_type);
    }

    return watcher->id;
}

/* Does the watcher match a removal request? */
static gboolean
signal_watcher_matches(GjsSignalWatcher     *watcher,
                       DBusBusType           bus_type,
                       const char           *sender,
                       const char           *path,
                       const char           *iface,
                       const char           *name,
                       int                   id,
                       GjsDBusSignalHandler  handler,
                       void                 *data)
{
    /* If we have an ID, check that first. If it matches, we are
     * done
     */
    if (id != INVALID_SIGNAL_ID && watcher->id == id)
        return TRUE;

    /* Start with data, most likely thing to not match */
    if (watcher->data != data)
        return FALSE;

    /* Second most likely non-match */
    if (watcher->handler != handler)
        return FALSE;

    /* Then third, do the more expensive checks */

    if (watcher->bus_type != bus_type)
        return FALSE;

    if (g_strcmp0(watcher->sender, sender) != 0)
        return FALSE;

    if (g_strcmp0(watcher->path, path) != 0)
        return FALSE;

    if (g_strcmp0(watcher->iface, iface) != 0)
        return FALSE;

    if (g_strcmp0(watcher->name, name) != 0)
        return FALSE;

    return TRUE;
}

static void
unwatch_signal(DBusBusType                  bus_type,
               const char                  *sender,
               const char                  *path,
               const char                  *iface,
               const char                  *name,
               int                          id,
               GjsDBusSignalHandler         handler,
               void                        *data)
{
    GSList *list;
    DBusConnection *weak;
    GjsDBusInfo *info;

    /* Always remove only ONE watcher (the first one we find) */

    weak = gjs_dbus_get_weak_ref(bus_type);

    /* First see if it's still pending */
    for (list = pending_signal_watchers;
         list != NULL;
         list = list->next) {
        if (signal_watcher_matches(list->data,
                                   bus_type,
                                   sender,
                                   path,
                                   iface,
                                   name,
                                   id,
                                   handler,
                                   data)) {
            GjsSignalWatcher *watcher = list->data;
            pending_signal_watchers = g_slist_delete_link(pending_signal_watchers,
                                                          list);

            if (weak != NULL)
                signal_watcher_set_matching(weak, watcher, FALSE);

            signal_watcher_dnotify(watcher); /* destroy even if we don't finalize */
            signal_watcher_unref(watcher);
            return;
        }
    }

    /* If not pending, and no bus connection, it can't exist */
    if (weak == NULL) {
        /* don't warn on nonexistent, since a vanishing bus name could
         * have nuked it outside the app's control.
         */
        return;
    }

    info = _gjs_dbus_ensure_info(weak);

    for (list = info->all_signal_watchers;
         list != NULL;
         list = list->next) {
        if (signal_watcher_matches(list->data,
                                   bus_type,
                                   sender,
                                   path,
                                   iface,
                                   name,
                                   id,
                                   handler,
                                   data)) {
            signal_watcher_remove(weak, info, list->data);
            /* note that "list" node is now invalid */
            return;
        }
    }

    /* don't warn on nonexistent, since a vanishing bus name could
     * have nuked it outside the app's control. Just do nothing.
     */
}

void
gjs_dbus_unwatch_signal(DBusBusType                  bus_type,
                        const char                  *sender,
                        const char                  *path,
                        const char                  *iface,
                        const char                  *name,
                        GjsDBusSignalHandler         handler,
                        void                        *data)
{
    unwatch_signal(bus_type,
                   sender,
                   path,
                   iface,
                   name,
                   INVALID_SIGNAL_ID,
                   handler,
                   data);
}

void
gjs_dbus_unwatch_signal_by_id(DBusBusType                  bus_type,
                              int                          id)
{
    unwatch_signal(bus_type,
                   NULL,
                   NULL,
                   NULL,
                   NULL,
                   id,
                   (GjsDBusSignalHandler)NULL,
                   NULL);
}

#if GJS_BUILD_TESTS

#include "dbus-proxy.h"

#include <sys/types.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/wait.h>

static pid_t test_service_pid = 0;
static GjsDBusProxy *test_service_proxy = NULL;

static GMainLoop *outer_loop = NULL;
static GMainLoop *inner_loop = NULL;

static int n_running_children = 0;

typedef struct {
    const char *sender;
    const char *path;
    const char *iface;
    const char *member;
} SignalWatchTest;

static SignalWatchTest watch_tests[] = {
    { NULL, NULL, NULL, NULL },
    { "com.litl.TestService", NULL, NULL, NULL },
    { NULL, "/com/litl/test/object42", NULL, NULL },
    { NULL, NULL, "com.litl.TestIface", NULL },
    { NULL, NULL, NULL, "TheSignal" }
};

static void do_test_service_child (void);

/* quit when all children are gone */
static void
another_child_down(void)
{
    g_assert(n_running_children > 0);
    n_running_children -= 1;

    if (n_running_children == 0) {
        g_main_loop_quit(outer_loop);
    }
}

/* This test function doesn't really test anything, just sets up
 * for the following one
 */
static void
fork_test_signal_service(void)
{
    pid_t child_pid;

    /* it would break to fork after we already connected */
    g_assert(gjs_dbus_get_weak_ref(DBUS_BUS_SESSION) == NULL);
    g_assert(gjs_dbus_get_weak_ref(DBUS_BUS_SYSTEM) == NULL);
    g_assert(test_service_pid == 0);

    child_pid = fork();

    if (child_pid == -1) {
        g_error("Failed to fork dbus service");
    } else if (child_pid > 0) {
        /* We are the parent */
        test_service_pid = child_pid;
        n_running_children += 1;

        return;
    }

    /* we are the child, set up a service for main test process to talk to */

    do_test_service_child();
}

static void
kill_child(void)
{
    if (kill(test_service_pid, SIGTERM) < 0) {
        g_error("Test service was no longer around... it must have failed somehow (%s)",
                strerror(errno));
    }

    /* We will quit main loop when we see the child go away */
}

static int signal_received_count = 0;
static int destroy_notify_count = 0;

static void
the_destroy_notifier(void *data)
{
    gjs_debug(GJS_DEBUG_DBUS, "got destroy notification on signal watch");
    destroy_notify_count += 1;
}

static void
the_destroy_notifier_that_quits(void *data)
{
    the_destroy_notifier(data);
    g_main_loop_quit(inner_loop);
}

static void
expect_receive_signal_handler(DBusConnection *connection,
                              DBusMessage    *message,
                              void           *data)
{
    gjs_debug(GJS_DEBUG_DBUS, "dbus signal watch handler called");

    g_assert(dbus_message_is_signal(message,
                                    "com.litl.TestIface",
                                    "TheSignal"));

    signal_received_count += 1;

    g_main_loop_quit(inner_loop);
}

static void
test_match_combo(const char *sender,
                 const char *path,
                 const char *iface,
                 const char *member)
{
    signal_received_count = 0;
    destroy_notify_count = 0;

    gjs_debug(GJS_DEBUG_DBUS, "Watching %s %s %s %s",
              sender,
              path,
              iface,
              member);

    gjs_dbus_watch_signal(DBUS_BUS_SESSION,
                          sender,
                          path,
                          iface,
                          member,
                          expect_receive_signal_handler,
                          GINT_TO_POINTER(1),
                          the_destroy_notifier);

    gjs_dbus_proxy_call_json_async(test_service_proxy,
                                   "emitTheSignal",
                                   NULL,
                                   NULL,
                                   NULL,
                                   NULL);
    g_main_loop_run(inner_loop);

    g_assert(signal_received_count == 1);
    g_assert(destroy_notify_count == 0);

    gjs_dbus_unwatch_signal(DBUS_BUS_SESSION,
                            sender,
                            path,
                            iface,
                            member,
                            expect_receive_signal_handler,
                            GINT_TO_POINTER(1));

    g_assert(destroy_notify_count == 1);
}

static gboolean
run_signal_tests_idle(void *data)
{
    int i;
    const char *unique_name;

    for (i = 0; i < (int) G_N_ELEMENTS(watch_tests); ++i) {
        SignalWatchTest *test = &watch_tests[i];

        test_match_combo(test->sender,
                         test->path,
                         test->iface,
                         test->member);
    }

    /* Now try on the unique bus name */

    unique_name = gjs_dbus_proxy_get_bus_name(test_service_proxy);

    test_match_combo(unique_name,
                     NULL, NULL, NULL);

    /* Now test we get destroy notify when the unique name disappears
     * on killing the child.
     */
    signal_received_count = 0;
    destroy_notify_count = 0;

    gjs_debug(GJS_DEBUG_DBUS, "Watching unique name %s",
              unique_name);

    gjs_dbus_watch_signal(DBUS_BUS_SESSION,
                          unique_name,
                          NULL, NULL, NULL,
                          expect_receive_signal_handler,
                          GINT_TO_POINTER(1),
                          the_destroy_notifier_that_quits);

    /* kill owner of unique_name */
    kill_child();

    /* wait for destroy notify */
    g_main_loop_run(inner_loop);

    g_assert(signal_received_count == 0);
    /* roundabout way to write == 1 that gives more info on fail */
    g_assert(destroy_notify_count > 0);
    g_assert(destroy_notify_count < 2);

    gjs_dbus_unwatch_signal(DBUS_BUS_SESSION,
                            unique_name,
                            NULL, NULL, NULL,
                            expect_receive_signal_handler,
                            GINT_TO_POINTER(1));

    g_assert(signal_received_count == 0);
    g_assert(destroy_notify_count == 1);

    /* remove idle */
    return FALSE;
}

static void
on_test_service_appeared(DBusConnection *connection,
                         const char     *name,
                         const char     *new_owner_unique_name,
                         void           *data)
{
    gjs_debug(GJS_DEBUG_DBUS, "%s appeared",
              name);

    inner_loop = g_main_loop_new(NULL, FALSE);

    test_service_proxy =
        gjs_dbus_proxy_new(connection, new_owner_unique_name,
                           "/com/litl/test/object42",
                           "com.litl.TestIface");

    g_idle_add(run_signal_tests_idle, NULL);
}

static void
on_test_service_vanished(DBusConnection *connection,
                         const char     *name,
                         const char     *old_owner_unique_name,
                         void           *data)
{
    gjs_debug(GJS_DEBUG_DBUS, "%s vanished", name);

    another_child_down();
}

static GjsDBusWatchNameFuncs watch_test_service_funcs = {
    on_test_service_appeared,
    on_test_service_vanished
};

void
bigtest_test_func_util_dbus_signals_client(void)
{
    pid_t result;
    int status;

    /* See comment in dbus.c above the g_test_trap_fork()
     * there on why we have to do this.
     */
    if (!g_test_trap_fork(0, 0)) {
        /* We are the parent */
        g_test_trap_assert_passed();
        return;
    }

    /* All this code runs in a child process */

    fork_test_signal_service();

    g_type_init();

    /* We rely on the child-forking test functions being called first */
    g_assert(test_service_pid != 0);

    gjs_dbus_watch_name(DBUS_BUS_SESSION,
                        "com.litl.TestService",
                        0,
                        &watch_test_service_funcs,
                        NULL);

    outer_loop = g_main_loop_new(NULL, FALSE);

    g_main_loop_run(outer_loop);

    if (test_service_proxy != NULL)
        g_object_unref(test_service_proxy);

    gjs_debug(GJS_DEBUG_DBUS,
              "waitpid() for first child");

    result = waitpid(test_service_pid, &status, 0);
    if (result < 0) {
        g_error("Failed to waitpid() for forked child: %s", strerror(errno));
    }

    if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
        g_error("Forked dbus service child exited with error code %d", WEXITSTATUS(status));
    }

    if (WIFSIGNALED(status) && WTERMSIG(status) != SIGTERM) {
        g_error("Forked dbus service child exited on wrong signal number %d", WTERMSIG(status));
    }

    gjs_debug(GJS_DEBUG_DBUS, "dbus signals test completed");

    /* We want to kill dbus so the weak refs are NULL to start the
     * next dbus-related test, which allows those tests
     * to fork new child processes.
     */
    _gjs_dbus_dispose_info(gjs_dbus_get_weak_ref(DBUS_BUS_SESSION));
    dbus_shutdown();

    gjs_debug(GJS_DEBUG_DBUS, "dbus shut down");

    /* FIXME this is only here because we've forked */
    exit(0);
}

/*
 * Child service that emits signals
 */

static gboolean currently_have_test_service = FALSE;
static GObject *test_service_object = NULL;

static void
test_service_emit_the_signal(DBusConnection  *connection,
                             DBusMessage     *message,
                             DBusMessageIter *in_iter,
                             DBusMessageIter *out_iter,
                             void            *data,
                             DBusError       *error)
{
    DBusMessage *signal;

    signal = dbus_message_new_signal("/com/litl/test/object42",
                                     "com.litl.TestIface",
                                     "TheSignal");
    dbus_connection_send(connection, signal, NULL);
    dbus_message_unref(signal);
}

static GjsDBusJsonMethod test_service_methods[] = {
    { "emitTheSignal", test_service_emit_the_signal, NULL }
};

static void
on_test_service_acquired(DBusConnection *connection,
                         const char     *name,
                         void           *data)
{
    g_assert(!currently_have_test_service);
    currently_have_test_service = TRUE;

    gjs_debug(GJS_DEBUG_DBUS, "com.litl.TestService acquired by child");

    gjs_dbus_register_json(connection,
                           "com.litl.TestIface",
                           test_service_methods,
                           G_N_ELEMENTS(test_service_methods));

    test_service_object = g_object_new(G_TYPE_OBJECT, NULL);

    gjs_dbus_register_g_object(connection,
                               "/com/litl/test/object42",
                               test_service_object,
                               "com.litl.TestIface");
}

static void
on_test_service_lost(DBusConnection *connection,
                     const char     *name,
                     void           *data)
{
    g_assert(currently_have_test_service);
    currently_have_test_service = FALSE;

    gjs_debug(GJS_DEBUG_DBUS, "com.litl.TestService lost by child");

    gjs_dbus_unregister_g_object(connection,
                                 "/com/litl/test/object42");

    gjs_dbus_unregister_json(connection,
                             "com.litl.TestIface");
}

static GjsDBusNameOwnerFuncs test_service_funcs = {
    "com.litl.TestService",
    DBUS_BUS_SESSION,
    on_test_service_acquired,
    on_test_service_lost
};

static void
do_test_service_child(void)
{
    GMainLoop *loop;

    g_type_init();

    loop = g_main_loop_new(NULL, FALSE);

    gjs_dbus_acquire_name(DBUS_BUS_SESSION,
                          &test_service_funcs,
                          NULL);

    g_main_loop_run(loop);

    /* Don't return to the test program main() */
    exit(0);
}

#endif /* GJS_BUILD_TESTS */
