/* SPDX-License-Identifier: MPL-2.0 */

#include "testutil.hpp"
#include "testutil_unity.hpp"

#include "core/ctx.hpp"

#include <unity.h>

namespace zlink
{
class ctx_termination_test_access_t
{
  public:
    static void set_terminating (ctx_t *ctx_, bool terminating_)
    {
        scoped_lock_t lock (ctx_->_slot_sync);
        ctx_->_terminating = terminating_;
    }
};
}

void setUp ()
{
}

void tearDown ()
{
}

static void receiver (void *socket_)
{
    char buffer[16];
    int rc = zlink_recv (socket_, &buffer, sizeof (buffer), 0);
    TEST_ASSERT_EQUAL_INT (-1, rc);
    TEST_ASSERT_EQUAL_INT (ETERM, errno);
}

void test_ctx_destroy ()
{
    //  Set up our context and sockets
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *socket = zlink_socket (ctx, ZLINK_SOCKET_DEALER);
    TEST_ASSERT_NOT_NULL (socket);

    // Close the socket
    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (socket));

    // Destroy the context
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_ctx_shutdown ()
{
    //  Set up our context and sockets
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *socket = zlink_socket (ctx, ZLINK_SOCKET_DEALER);
    TEST_ASSERT_NOT_NULL (socket);

    // Spawn a thread to receive on socket
    void *receiver_thread = zlink_thread_start (&receiver, socket);

    // Wait for thread to start up and block
    msleep (SETTLE_TIME);

    // Shutdown context, if we used destroy here we would deadlock.
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_shutdown (ctx));

    // Wait for thread to finish
    zlink_thread_join (receiver_thread);

    // Close the socket.
    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (socket));

    // Destroy the context, will now not hang as we have closed the socket.
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_ctx_shutdown_socket_opened_after ()
{
    //  Set up our context.
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    // Open a socket to start context, and close it immediately again.
    void *socket = zlink_socket (ctx, ZLINK_SOCKET_DEALER);
    TEST_ASSERT_NOT_NULL (socket);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (socket));

    // Shutdown context.
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_shutdown (ctx));

    // Opening socket should now fail.
    TEST_ASSERT_NULL (zlink_socket (ctx, ZLINK_SOCKET_DEALER));
    TEST_ASSERT_FAILURE_ERRNO (ETERM, -1);

    // Destroy the context.
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_ctx_shutdown_only_socket_opened_after ()
{
    //  Set up our context.
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    // Shutdown context.
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_shutdown (ctx));

    // Opening socket should now fail.
    TEST_ASSERT_NULL (zlink_socket (ctx, ZLINK_SOCKET_DEALER));
    TEST_ASSERT_FAILURE_ERRNO (ETERM, -1);

    // Destroy the context.
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_ctx_term_rearms_reaper_when_last_socket_closes_during_restart ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *socket = zlink_socket (ctx, ZLINK_SOCKET_DEALER);
    TEST_ASSERT_NOT_NULL (socket);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_shutdown (ctx));

    zlink::ctx_t *internal_ctx = static_cast<zlink::ctx_t *> (ctx);
    zlink::ctx_termination_test_access_t::set_terminating (internal_ctx, false);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (socket));
    TEST_ASSERT_SUCCESS_ERRNO (internal_ctx->wait_for_socket_count_at_most (0, 5000));

    // Reproduce the state restored by flush_pending_inproc_locked(): shutdown
    // has started, the registry is empty, but no stop reached the reaper.
    zlink::ctx_termination_test_access_t::set_terminating (internal_ctx, true);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_pending_inproc_disconnect_releases_socket_before_context_term ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *socket = zlink_socket (ctx, ZLINK_SOCKET_DEALER);
    TEST_ASSERT_NOT_NULL (socket);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (socket, "inproc://pending-disconnect"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_disconnect (socket, "inproc://pending-disconnect"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (socket));

    zlink::ctx_t *internal_ctx = static_cast<zlink::ctx_t *> (ctx);
    TEST_ASSERT_SUCCESS_ERRNO (internal_ctx->wait_for_socket_count_at_most (0, 5000));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_zlink_ctx_term_null_fails ()
{
    int rc = zlink_ctx_term (NULL);
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_INVALID_HANDLE, rc);
    TEST_ASSERT_EQUAL_INT (EFAULT, errno);
}

void test_zlink_term_null_fails ()
{
    int rc = zlink_ctx_term (NULL);
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_INVALID_HANDLE, rc);
    TEST_ASSERT_EQUAL_INT (EFAULT, errno);
}

void test_zlink_ctx_shutdown_null_fails ()
{
    int rc = zlink_ctx_shutdown (NULL);
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_INVALID_HANDLE, rc);
    TEST_ASSERT_EQUAL_INT (EFAULT, errno);
}

int main (void)
{
    setup_test_environment ();

    UNITY_BEGIN ();
    RUN_TEST (test_ctx_destroy);
    RUN_TEST (test_ctx_shutdown);
    RUN_TEST (test_ctx_shutdown_socket_opened_after);
    RUN_TEST (test_ctx_shutdown_only_socket_opened_after);
    RUN_TEST (test_ctx_term_rearms_reaper_when_last_socket_closes_during_restart);
    RUN_TEST (test_pending_inproc_disconnect_releases_socket_before_context_term);
    RUN_TEST (test_zlink_ctx_term_null_fails);
    RUN_TEST (test_zlink_term_null_fails);
    RUN_TEST (test_zlink_ctx_shutdown_null_fails);

    return UNITY_END ();
}
