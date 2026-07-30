/* SPDX-License-Identifier: MPL-2.0 */

#include "../testutil.hpp"

#include "api/socket/request_completion_queue_internal.hpp"

#include <unity.h>

void test_completion_reservations_have_a_finite_admission_limit ()
{
    zlink::request_completion::queue_state_t state;
    for (size_t i = 0;
         i < zlink::request_completion::max_pending_completions; ++i)
        TEST_ASSERT_TRUE (zlink::request_completion::try_reserve (&state));

    errno = 0;
    TEST_ASSERT_FALSE (zlink::request_completion::try_reserve (&state));
    TEST_ASSERT_EQUAL_INT (EAGAIN, errno);

    for (size_t i = 0;
         i < zlink::request_completion::max_pending_completions; ++i)
        zlink::request_completion::release_reservation (&state);

    TEST_ASSERT_TRUE (zlink::request_completion::try_reserve (&state));
    zlink::request_completion::release_reservation (&state);
}

int main ()
{
    UNITY_BEGIN ();
    RUN_TEST (test_completion_reservations_have_a_finite_admission_limit);
    return UNITY_END ();
}
