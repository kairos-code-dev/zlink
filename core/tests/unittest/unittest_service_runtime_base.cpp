/* SPDX-License-Identifier: MPL-2.0 */

#include "../testutil.hpp"
#include "../testutil_unity.hpp"

#include "core/ctx.hpp"
#include "services/common/service_runtime_base.hpp"

#include <unity.h>

void setUp ()
{
}

void tearDown ()
{
}

static zlink::ctx_t *create_ctx ()
{
    zlink::ctx_t *ctx = new zlink::ctx_t ();
    TEST_ASSERT_NOT_NULL (ctx);
    return ctx;
}

static zlink::socket_base_t *create_socket (zlink::ctx_t *ctx_)
{
    TEST_ASSERT_NOT_NULL (ctx_);
    zlink::socket_base_t *socket = ctx_->create_socket (ZLINK_SOCKET_PAIR);
    TEST_ASSERT_NOT_NULL (socket);
    return socket;
}

void test_wait_drained_timeout_preserves_closing_tracking ()
{
    zlink::ctx_t *ctx = create_ctx ();
    zlink::service_runtime_base_t lifecycle (ctx);
    zlink::socket_base_t *socket = create_socket (ctx);

    lifecycle.register_socket (socket);
    TEST_ASSERT_EQUAL_UINT (1u, lifecycle.owned_socket_count ());

    TEST_ASSERT_SUCCESS_ERRNO (lifecycle.close_socket (socket, 1000));
    TEST_ASSERT_NULL (socket);

    TEST_ASSERT_EQUAL_INT (-1, lifecycle.wait_drained (0));
    TEST_ASSERT_EQUAL_INT (ETIMEDOUT, errno);
    TEST_ASSERT_EQUAL_UINT (1u, lifecycle.owned_socket_count ());

    TEST_ASSERT_SUCCESS_ERRNO (lifecycle.wait_drained (2000));
    TEST_ASSERT_EQUAL_UINT (0u, lifecycle.owned_socket_count ());

    TEST_ASSERT_SUCCESS_ERRNO (ctx->terminate ());
}

void test_force_wait_remaining_recovers_after_wait_timeout ()
{
    zlink::ctx_t *ctx = create_ctx ();
    zlink::service_runtime_base_t lifecycle (ctx);
    zlink::socket_base_t *socket = create_socket (ctx);

    lifecycle.register_socket (socket);
    TEST_ASSERT_SUCCESS_ERRNO (lifecycle.close_socket (socket, 1000));
    TEST_ASSERT_NULL (socket);

    TEST_ASSERT_EQUAL_INT (-1, lifecycle.wait_drained (0));
    TEST_ASSERT_EQUAL_INT (ETIMEDOUT, errno);
    TEST_ASSERT_EQUAL_UINT (1u, lifecycle.owned_socket_count ());

    TEST_ASSERT_SUCCESS_ERRNO (lifecycle.force_wait_remaining (2000));
    TEST_ASSERT_EQUAL_UINT (0u, lifecycle.owned_socket_count ());

    TEST_ASSERT_SUCCESS_ERRNO (ctx->terminate ());
}

int main (void)
{
    setup_test_environment ();

    UNITY_BEGIN ();
    RUN_TEST (test_wait_drained_timeout_preserves_closing_tracking);
    RUN_TEST (test_force_wait_remaining_recovers_after_wait_timeout);
    return UNITY_END ();
}
