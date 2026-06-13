/* SPDX-License-Identifier: MPL-2.0 */

#include "../testutil.hpp"
#include "../testutil_unity.hpp"

#include "../src/api/spot/dispatch/service_spot_dispatch_surface_internal.hpp"

#include <unity.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

void setUp ()
{
}

void tearDown ()
{
}

namespace
{
struct dispatch_seriality_probe_t
{
    std::atomic<int> running{0};
    std::atomic<int> max_running{0};
    std::atomic<int> completed{0};
    std::mutex mutex;
    std::condition_variable changed;
};

void update_max_running (std::atomic<int> &max_running_, int running_)
{
    int observed = max_running_.load (std::memory_order_acquire);
    while (observed < running_
           && !max_running_.compare_exchange_weak (observed, running_, std::memory_order_acq_rel,
                                                   std::memory_order_acquire)) {
    }
}

void seriality_dispatch_handler (void *,
                                 const zlink_spot_dispatch_info_t *,
                                 void *userdata_)
{
    dispatch_seriality_probe_t *probe = static_cast<dispatch_seriality_probe_t *> (userdata_);
    if (!probe)
        return;

    const int running = probe->running.fetch_add (1, std::memory_order_acq_rel) + 1;
    update_max_running (probe->max_running, running);
    std::this_thread::sleep_for (std::chrono::milliseconds (2));
    probe->running.fetch_sub (1, std::memory_order_acq_rel);
    probe->completed.fetch_add (1, std::memory_order_acq_rel);
    probe->changed.notify_all ();
}

void test_same_native_spot_dispatch_handler_is_not_invoked_concurrently ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *node = zlink_spot_node_new (ctx, NULL);
    TEST_ASSERT_NOT_NULL (node);
    void *spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (spot);

    dispatch_seriality_probe_t probe;
    TEST_ASSERT_EQUAL_INT (
      ZLINK_HANDLER_OK, zlink_spot_dispatch_event_handler (spot, &seriality_dispatch_handler, &probe));

    constexpr int event_count = 32;
    std::vector<int> subjects (event_count);
    std::vector<std::thread> callers;
    callers.reserve (event_count);
    for (int i = 0; i < event_count; ++i) {
        subjects[i] = i;
        callers.emplace_back ([spot, subject = &subjects[i]] {
            zlink_spot_notify_dispatch_info (spot, ZLINK_SPOT_DISPATCH_EVENT_TIMER_READABLE,
                                             ZLINK_SPOT_DISPATCH_SUBJECT_TIMER, subject);
        });
    }
    for (std::thread &caller : callers) {
        caller.join ();
    }

    {
        std::unique_lock<std::mutex> lock (probe.mutex);
        TEST_ASSERT_TRUE (probe.changed.wait_for (
          lock, std::chrono::seconds (2), [&probe] {
              return probe.completed.load (std::memory_order_acquire) == event_count;
          }));
    }
    TEST_ASSERT_EQUAL_INT (event_count, probe.completed.load (std::memory_order_acquire));
    TEST_ASSERT_EQUAL_INT (1, probe.max_running.load (std::memory_order_acquire));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}
} // namespace

int main ()
{
    setup_test_environment ();

    UNITY_BEGIN ();
    RUN_TEST (test_same_native_spot_dispatch_handler_is_not_invoked_concurrently);
    return UNITY_END ();
}
