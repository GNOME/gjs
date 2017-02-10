#include "gjs/jsapi-util.h"
#include "gjs/jsapi-util-root.h"
#include "gjs-test-utils.h"

static GMutex gc_lock;
static GCond gc_finished;
static volatile int gc_counter;

#define PARENT(fx) ((GjsUnitTestFixture *)fx)
typedef struct _GjsRootingFixture GjsRootingFixture;
struct _GjsRootingFixture {
    GjsUnitTestFixture parent;

    bool finalized;
    bool notify_called;
};

static void
test_obj_finalize(JSFreeOp *fop,
                  JSObject *obj)
{
    bool *finalized_p = static_cast<bool *>(JS_GetPrivate(obj));
    g_assert_false(*finalized_p);
    *finalized_p = true;
}

static JSClass test_obj_class = {
    "TestObj",
    JSCLASS_HAS_PRIVATE,
    JS_PropertyStub,
    JS_DeletePropertyStub,
    JS_PropertyStub,
    JS_StrictPropertyStub,
    JS_EnumerateStub,
    JS_ResolveStub,
    JS_ConvertStub,
    test_obj_finalize
};

static JSObject *
test_obj_new(GjsRootingFixture *fx)
{
    JSObject *retval = JS_NewObject(PARENT(fx)->cx, &test_obj_class,
                                    JS::NullPtr(), JS::NullPtr());
    JS_SetPrivate(retval, &fx->finalized);
    return retval;
}

static void
on_gc(JSRuntime *rt,
      JSGCStatus status,
      void      *data)
{
    if (status != JSGC_END)
        return;

    g_mutex_lock(&gc_lock);
    g_atomic_int_inc(&gc_counter);
    g_cond_broadcast(&gc_finished);
    g_mutex_unlock(&gc_lock);
}

static void
setup(GjsRootingFixture *fx,
      gconstpointer      unused)
{
    gjs_unit_test_fixture_setup(PARENT(fx), unused);
    JS_SetGCCallback(JS_GetRuntime(PARENT(fx)->cx), on_gc, fx);
}

static void
teardown(GjsRootingFixture *fx,
         gconstpointer      unused)
{
    gjs_unit_test_fixture_teardown(PARENT(fx), unused);
}

static void
wait_for_gc(GjsRootingFixture *fx)
{
    int count = g_atomic_int_get(&gc_counter);

    JS_GC(JS_GetRuntime(PARENT(fx)->cx));

    g_mutex_lock(&gc_lock);
    while (count == g_atomic_int_get(&gc_counter)) {
        g_cond_wait(&gc_finished, &gc_lock);
    }
    g_mutex_unlock(&gc_lock);
}

static void
test_maybe_owned_rooted_keeps_alive_across_gc(GjsRootingFixture *fx,
                                              gconstpointer      unused)
{
    auto obj = new GjsMaybeOwned<JSObject *>();
    obj->root(PARENT(fx)->cx, test_obj_new(fx));

    wait_for_gc(fx);
    g_assert_false(fx->finalized);

    delete obj;
    wait_for_gc(fx);
    g_assert_true(fx->finalized);
}

static void
test_maybe_owned_rooted_is_collected_after_reset(GjsRootingFixture *fx,
                                                 gconstpointer      unused)
{
    auto obj = new GjsMaybeOwned<JSObject *>();
    obj->root(PARENT(fx)->cx, test_obj_new(fx));
    obj->reset();

    wait_for_gc(fx);
    g_assert_true(fx->finalized);
    delete obj;
}

static void
test_maybe_owned_weak_pointer_is_collected_by_gc(GjsRootingFixture *fx,
                                                 gconstpointer      unused)
{
    auto obj = new GjsMaybeOwned<JSObject *>();
    *obj = test_obj_new(fx);

    wait_for_gc(fx);
    g_assert_true(fx->finalized);
    delete obj;
}

static void
test_maybe_owned_heap_rooted_keeps_alive_across_gc(GjsRootingFixture *fx,
                                                   gconstpointer      unused)
{
    auto obj = new GjsMaybeOwned<JSObject *>();
    obj->root(PARENT(fx)->cx, test_obj_new(fx));

    wait_for_gc(fx);
    g_assert_false(fx->finalized);

    delete obj;
    wait_for_gc(fx);
    g_assert_true(fx->finalized);
}

static void
context_destroyed(JS::HandleObject obj,
                  void            *data)
{
    auto fx = static_cast<GjsRootingFixture *>(data);
    g_assert_false(fx->notify_called);
    g_assert_false(fx->finalized);
    fx->notify_called = true;
}

static void
teardown_context_already_destroyed(GjsRootingFixture *fx,
                                   gconstpointer      unused)
{
    gjs_unit_test_teardown_context_already_destroyed(PARENT(fx));
}

static void
test_maybe_owned_notify_callback_called_on_context_destroy(GjsRootingFixture *fx,
                                                           gconstpointer      unused)
{
    auto obj = new GjsMaybeOwned<JSObject *>();
    obj->root(PARENT(fx)->cx, test_obj_new(fx), context_destroyed, fx);

    gjs_unit_test_destroy_context(PARENT(fx));
    g_assert_true(fx->notify_called);
    delete obj;
}

static void
test_maybe_owned_object_destroyed_after_notify(GjsRootingFixture *fx,
                                               gconstpointer      unused)
{
    auto obj = new GjsMaybeOwned<JSObject *>();
    obj->root(PARENT(fx)->cx, test_obj_new(fx), context_destroyed, fx);

    gjs_unit_test_destroy_context(PARENT(fx));
    g_assert_true(fx->finalized);
    delete obj;
}

void
gjs_test_add_tests_for_rooting(void)
{
#define ADD_ROOTING_TEST(path, f) \
    g_test_add("/rooting/" path, GjsRootingFixture, NULL, setup, f,  teardown);

    ADD_ROOTING_TEST("maybe-owned/rooted-keeps-alive-across-gc",
                     test_maybe_owned_rooted_keeps_alive_across_gc);
    ADD_ROOTING_TEST("maybe-owned/rooted-is-collected-after-reset",
                     test_maybe_owned_rooted_is_collected_after_reset);
    ADD_ROOTING_TEST("maybe-owned/weak-pointer-is-collected-by-gc",
                     test_maybe_owned_weak_pointer_is_collected_by_gc);
    ADD_ROOTING_TEST("maybe-owned/heap-rooted-keeps-alive-across-gc",
                     test_maybe_owned_heap_rooted_keeps_alive_across_gc);

#undef ADD_ROOTING_TEST

#define ADD_CONTEXT_DESTROY_TEST(path, f) \
    g_test_add("/rooting/" path, GjsRootingFixture, NULL, setup, f, \
               teardown_context_already_destroyed);

    ADD_CONTEXT_DESTROY_TEST("maybe-owned/notify-callback-called-on-context-destroy",
                             test_maybe_owned_notify_callback_called_on_context_destroy);
    ADD_CONTEXT_DESTROY_TEST("maybe-owned/object-destroyed-after-notify",
                             test_maybe_owned_object_destroyed_after_notify);

#undef ADD_CONTEXT_DESTROY_TEST
}
