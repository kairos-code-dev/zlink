/* SPDX-License-Identifier: MPL-2.0 */

#include "../testutil.hpp"

#include "api/socket/request_timeout_scheduler_internal.hpp"

#include <atomic>
#include <chrono>
#include <thread>

#include <unity.h>

namespace
{
struct callback_state_t
{
    callback_state_t () : entered (0), finished (0), cleanup_count (0) {}

    std::atomic<int> entered;
    std::atomic<int> finished;
    std::atomic<int> cleanup_count;
};

void timeout_handler (void *userdata_)
{
    callback_state_t *state = static_cast<callback_state_t *> (userdata_);
    state->entered.fetch_add (1, std::memory_order_relaxed);
    std::this_thread::sleep_for (std::chrono::milliseconds (30));
    state->finished.fetch_add (1, std::memory_order_relaxed);
}

void cleanup_handler (void *userdata_)
{
    callback_state_t *state = static_cast<callback_state_t *> (userdata_);
    state->cleanup_count.fetch_add (1, std::memory_order_relaxed);
}

void wait_until_entered (callback_state_t *state_)
{
    //  zlink_stopwatch_intermediate() reports microseconds, so the bound must
    //  be expressed in the same unit or the intended multi-second allowance
    //  silently shrinks to a few milliseconds.
    const unsigned long timeout_us = static_cast<unsigned long> (SETTLE_TIME) * 20 * 1000;
    void *watch = zlink_stopwatch_start ();
    while (state_->entered.load (std::memory_order_relaxed) == 0) {
        msleep (1);
        TEST_ASSERT_LESS_OR_EQUAL_MESSAGE (timeout_us, zlink_stopwatch_intermediate (watch),
                                           "Timeout waiting for request timeout handler");
    }
    zlink_stopwatch_stop (watch);
}
}

void setUp ()
{
}

void tearDown ()
{
}

void test_cancel_before_deadline_prevents_handler_and_runs_cleanup ()
{
    callback_state_t state;

    std::shared_ptr<zlink::request_timeout::task_t> task =
      zlink::request_timeout::schedule (100, timeout_handler, &state, cleanup_handler);
    TEST_ASSERT_TRUE (task.get () != NULL);

    zlink::request_timeout::cancel (task);
    task.reset ();

    msleep (150);

    TEST_ASSERT_EQUAL_INT (0, state.entered.load (std::memory_order_relaxed));
    TEST_ASSERT_EQUAL_INT (0, state.finished.load (std::memory_order_relaxed));
    TEST_ASSERT_EQUAL_INT (1, state.cleanup_count.load (std::memory_order_relaxed));
}

void test_cancel_while_handler_is_firing_waits_for_handler_completion ()
{
    callback_state_t state;

    std::shared_ptr<zlink::request_timeout::task_t> task =
      zlink::request_timeout::schedule (1, timeout_handler, &state, cleanup_handler);
    TEST_ASSERT_TRUE (task.get () != NULL);

    wait_until_entered (&state);

    zlink::request_timeout::cancel (task);
    task.reset ();

    TEST_ASSERT_EQUAL_INT (1, state.entered.load (std::memory_order_relaxed));
    TEST_ASSERT_EQUAL_INT (1, state.finished.load (std::memory_order_relaxed));
    TEST_ASSERT_EQUAL_INT (0, state.cleanup_count.load (std::memory_order_relaxed));
}

int main (void)
{
    UNITY_BEGIN ();

    setup_test_environment ();

    RUN_TEST (test_cancel_before_deadline_prevents_handler_and_runs_cleanup);
    RUN_TEST (test_cancel_while_handler_is_firing_waits_for_handler_completion);

    return UNITY_END ();
}
