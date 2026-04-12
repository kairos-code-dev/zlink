/* SPDX-License-Identifier: MPL-2.0 */

#include "testutil.hpp"
#include "testutil_unity.hpp"

#include <chrono>
#include <atomic>
#include <condition_variable>
#include <mutex>

namespace
{
struct timer_callback_probe_t
{
    timer_callback_probe_t () : fire_count (0), called (false) {}

    std::mutex mutex;
    std::condition_variable cv;
    uint64_t fire_count;
    bool called;
};

struct spot_dispatch_probe_t
{
    spot_dispatch_probe_t () : event (0), called (false) {}

    std::mutex mutex;
    std::condition_variable cv;
    int event;
    bool called;
};

void on_timer (void *, uint64_t fire_count_, void *userdata_)
{
    timer_callback_probe_t *probe =
      static_cast<timer_callback_probe_t *> (userdata_);
    std::lock_guard<std::mutex> lock (probe->mutex);
    probe->fire_count = fire_count_;
    probe->called = true;
    probe->cv.notify_all ();
}

void on_spot_dispatch_event (void *,
                             zlink_spot_dispatch_event_t event_,
                             void *userdata_)
{
    spot_dispatch_probe_t *probe =
      static_cast<spot_dispatch_probe_t *> (userdata_);
    std::lock_guard<std::mutex> lock (probe->mutex);
    probe->event = static_cast<int> (event_);
    probe->called = true;
    probe->cv.notify_all ();
}

void test_timer_one_shot_recv ()
{
    void *timer = zlink_timer_new ();
    TEST_ASSERT_NOT_NULL (timer);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_timer_start (timer, 20 * 1000 * 1000ULL, 1));

    uint64_t fire_count = 0;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_timer_recv (timer, &fire_count, 0));
    TEST_ASSERT_EQUAL_UINT64 (1, fire_count);

    TEST_ASSERT_EQUAL_INT (-1, zlink_timer_recv (timer, &fire_count, ZLINK_DONTWAIT));
    TEST_ASSERT_EQUAL_INT (EAGAIN, zlink_errno ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_timer_destroy (&timer));
}

void test_timer_repeat_recv_sequence ()
{
    void *timer = zlink_timer_new ();
    TEST_ASSERT_NOT_NULL (timer);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_timer_start (timer, 10 * 1000 * 1000ULL, 3));

    uint64_t fire_count = 0;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_timer_recv (timer, &fire_count, 0));
    TEST_ASSERT_EQUAL_UINT64 (1, fire_count);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_timer_recv (timer, &fire_count, 0));
    TEST_ASSERT_EQUAL_UINT64 (2, fire_count);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_timer_recv (timer, &fire_count, 0));
    TEST_ASSERT_EQUAL_UINT64 (3, fire_count);

    TEST_ASSERT_EQUAL_INT (-1, zlink_timer_recv (timer, &fire_count, ZLINK_DONTWAIT));
    TEST_ASSERT_EQUAL_INT (EAGAIN, zlink_errno ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_timer_destroy (&timer));
}

void test_timer_poller_and_recv ()
{
    void *poller = zlink_poller_new ();
    void *timer = zlink_timer_new ();
    TEST_ASSERT_NOT_NULL (poller);
    TEST_ASSERT_NOT_NULL (timer);

    int user_tag = 7;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_poller_add_timer (poller, timer, &user_tag));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_timer_start (timer, 20 * 1000 * 1000ULL, 2));

    zlink_poller_event_t ev;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_poller_wait (poller, &ev, 500, NULL));
    TEST_ASSERT_EQUAL_INT (ZLINK_POLLER_SOURCE_TIMER, ev.source_kind);
    TEST_ASSERT_NULL (ev.socket);
    TEST_ASSERT_EQUAL_PTR (timer, ev.timer);
    TEST_ASSERT_EQUAL_PTR (&user_tag, ev.user_data);
    TEST_ASSERT_EQUAL_INT (ZLINK_POLLIN, ev.events);

    uint64_t fire_count = 0;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_timer_recv (timer, &fire_count, 0));
    TEST_ASSERT_EQUAL_UINT64 (1, fire_count);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_poller_remove_timer (poller, timer));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_timer_stop (timer));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_timer_destroy (&timer));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_poller_destroy (&poller));
}

void test_timer_callback_conflicts_and_destroy_busy ()
{
    void *timer_callback = zlink_timer_new ();
    void *timer_poller = zlink_timer_new ();
    void *poller = zlink_poller_new ();
    TEST_ASSERT_NOT_NULL (timer_callback);
    TEST_ASSERT_NOT_NULL (timer_poller);
    TEST_ASSERT_NOT_NULL (poller);

    timer_callback_probe_t probe;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_timer_handler (timer_callback, &on_timer, &probe));

    uint64_t fire_count = 0;
    TEST_ASSERT_EQUAL_INT (-1,
                           zlink_timer_recv (timer_callback, &fire_count,
                                             ZLINK_DONTWAIT));
    TEST_ASSERT_EQUAL_INT (EBUSY, zlink_errno ());
    TEST_ASSERT_EQUAL_INT (-1,
                           zlink_poller_add_timer (poller, timer_callback, NULL));
    TEST_ASSERT_EQUAL_INT (EBUSY, zlink_errno ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_timer_start (timer_callback,
                                                  20 * 1000 * 1000ULL, 1));
    {
        std::unique_lock<std::mutex> lock (probe.mutex);
        const bool fired = probe.cv.wait_for (
          lock, std::chrono::milliseconds (500),
          [&probe]() { return probe.called; });
        TEST_ASSERT_TRUE (fired);
        TEST_ASSERT_EQUAL_UINT64 (1, probe.fire_count);
    }

    TEST_ASSERT_SUCCESS_ERRNO (zlink_poller_add_timer (poller, timer_poller, NULL));
    TEST_ASSERT_EQUAL_INT (-1, zlink_timer_handler (timer_poller, &on_timer, &probe));
    TEST_ASSERT_EQUAL_INT (EBUSY, zlink_errno ());
    TEST_ASSERT_EQUAL_INT (-1, zlink_timer_destroy (&timer_poller));
    TEST_ASSERT_EQUAL_INT (EBUSY, zlink_errno ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_poller_remove_timer (poller, timer_poller));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_timer_destroy (&timer_poller));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_timer_stop (timer_callback));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_timer_destroy (&timer_callback));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_poller_destroy (&poller));
}

void test_spot_timer_dispatch_event_and_recv ()
{
    void *ctx = zlink_ctx_new ();
    void *node = zlink_spot_node_new (ctx);
    void *spot = zlink_spot_new (node);
    void *timer = zlink_spot_timer_new (spot);
    TEST_ASSERT_NOT_NULL (ctx);
    TEST_ASSERT_NOT_NULL (node);
    TEST_ASSERT_NOT_NULL (spot);
    TEST_ASSERT_NOT_NULL (timer);

    spot_dispatch_probe_t probe;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_dispatch_event_handler (spot, &on_spot_dispatch_event, &probe));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_timer_start (timer, 20 * 1000 * 1000ULL, 1));

    {
        std::unique_lock<std::mutex> lock (probe.mutex);
        const bool fired = probe.cv.wait_for (
          lock, std::chrono::milliseconds (500),
          [&probe]() { return probe.called; });
        TEST_ASSERT_TRUE (fired);
        TEST_ASSERT_EQUAL_INT (ZLINK_SPOT_DISPATCH_EVENT_TIMER_READABLE,
                               probe.event);
    }

    uint64_t fire_count = 0;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_timer_recv (timer, &fire_count, 0));
    TEST_ASSERT_EQUAL_UINT64 (1, fire_count);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_timer_destroy (&timer));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}
}

SETUP_TEARDOWN_TESTCONTEXT

int main (void)
{
    setup_test_environment (60);

    UNITY_BEGIN ();
    RUN_TEST (test_timer_one_shot_recv);
    RUN_TEST (test_timer_repeat_recv_sequence);
    RUN_TEST (test_timer_poller_and_recv);
    RUN_TEST (test_timer_callback_conflicts_and_destroy_busy);
    RUN_TEST (test_spot_timer_dispatch_event_and_recv);
    return UNITY_END ();
}
