/* SPDX-License-Identifier: MPL-2.0 */

#include "testutil.hpp"
#include "testutil_unity.hpp"

#include <unity.h>

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
    // TODO which error is expected here? use TEST_ASSERT_FAILURE_ERRNO instead
    TEST_ASSERT_EQUAL_INT (-1, rc);
}

void test_ctx_destroy ()
{
    //  Set up our context and sockets
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *socket = zlink_socket (ctx, ZLINK_DEALER);
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

    void *socket = zlink_socket (ctx, ZLINK_DEALER);
    TEST_ASSERT_NOT_NULL (socket);

    // Spawn a thread to receive on socket
    void *receiver_thread = zlink_threadstart (&receiver, socket);

    // Wait for thread to start up and block
    msleep (SETTLE_TIME);

    // Shutdown context, if we used destroy here we would deadlock.
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_shutdown (ctx));

    // Wait for thread to finish
    zlink_threadclose (receiver_thread);

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
    void *socket = zlink_socket (ctx, ZLINK_DEALER);
    TEST_ASSERT_NOT_NULL (socket);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (socket));

    // Shutdown context.
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_shutdown (ctx));

    // Opening socket should now fail.
    TEST_ASSERT_NULL (zlink_socket (ctx, ZLINK_DEALER));
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
    TEST_ASSERT_NULL (zlink_socket (ctx, ZLINK_DEALER));
    TEST_ASSERT_FAILURE_ERRNO (ETERM, -1);

    // Destroy the context.
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_zlink_ctx_term_null_fails ()
{
    int rc = zlink_ctx_term (NULL);
    TEST_ASSERT_EQUAL_INT (-1, rc);
    TEST_ASSERT_EQUAL_INT (EFAULT, errno);
}

void test_zlink_term_null_fails ()
{
    int rc = zlink_ctx_term (NULL);
    TEST_ASSERT_EQUAL_INT (-1, rc);
    TEST_ASSERT_EQUAL_INT (EFAULT, errno);
}

void test_zlink_ctx_shutdown_null_fails ()
{
    int rc = zlink_ctx_shutdown (NULL);
    TEST_ASSERT_EQUAL_INT (-1, rc);
    TEST_ASSERT_EQUAL_INT (EFAULT, errno);
}

#ifdef ZLINK_HAVE_POLLER
struct poller_test_data_t
{
    int socket_type;
    void *ctx;
    void *counter;
};

void run_poller (void *data_)
{
    const poller_test_data_t *const poller_test_data =
      static_cast<const poller_test_data_t *> (data_);

    void *socket =
      zlink_socket (poller_test_data->ctx, poller_test_data->socket_type);
    TEST_ASSERT_NOT_NULL (socket);

    void *poller = zlink_poller_new ();
    TEST_ASSERT_NOT_NULL (poller);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_poller_add (poller, socket, NULL, ZLINK_POLLIN));

    zlink_atomic_counter_set (poller_test_data->counter, 1);

    zlink_poller_event_t event;
    TEST_ASSERT_FAILURE_ERRNO (ETERM, zlink_poller_wait (poller, &event, -1));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_poller_destroy (&poller));

    // Close the socket
    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (socket));
}
#endif

void test_poller_exists_with_socket_on_zlink_ctx_term (const int socket_type_)
{
#ifdef ZLINK_HAVE_POLLER
    struct poller_test_data_t poller_test_data;

    poller_test_data.socket_type = socket_type_;

    //  Set up our context and sockets
    poller_test_data.ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (poller_test_data.ctx);

    poller_test_data.counter = zlink_atomic_counter_new ();
    TEST_ASSERT_NOT_NULL (poller_test_data.counter);

    void *thread = zlink_threadstart (run_poller, &poller_test_data);
    TEST_ASSERT_NOT_NULL (thread);

    while (zlink_atomic_counter_value (poller_test_data.counter) == 0) {
        msleep (10);
    }

    // Destroy the context
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (poller_test_data.ctx));

    zlink_threadclose (thread);

    zlink_atomic_counter_destroy (&poller_test_data.counter);
#else
    TEST_IGNORE_MESSAGE ("libzlink without zlink_poller_* support, ignoring test");
#endif
}

void test_poller_exists_with_socket_on_zlink_ctx_term_non_thread_safe_socket ()
{
    test_poller_exists_with_socket_on_zlink_ctx_term (ZLINK_DEALER);
}

int main (void)
{
    setup_test_environment ();

    UNITY_BEGIN ();
    RUN_TEST (test_ctx_destroy);
    RUN_TEST (test_ctx_shutdown);
    RUN_TEST (test_ctx_shutdown_socket_opened_after);
    RUN_TEST (test_ctx_shutdown_only_socket_opened_after);
    RUN_TEST (test_zlink_ctx_term_null_fails);
    RUN_TEST (test_zlink_term_null_fails);
    RUN_TEST (test_zlink_ctx_shutdown_null_fails);

    RUN_TEST (
      test_poller_exists_with_socket_on_zlink_ctx_term_non_thread_safe_socket);

    return UNITY_END ();
}
