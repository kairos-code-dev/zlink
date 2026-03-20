/* SPDX-License-Identifier: MPL-2.0 */

#include "../testutil.hpp"
#include "../testutil_unity.hpp"

#include <cerrno>
#include <cstring>

#include <unity.h>

void setUp ()
{
}

void tearDown ()
{
}

namespace
{
void noop_socket_handler (const zlink_routing_id_t *,
                          zlink_msg_t *parts_,
                          size_t part_count_,
                          void *)
{
    zlink_multipart_close (parts_, part_count_);
}

void noop_spot_handler (const zlink_routing_id_t *,
                        const char *,
                        size_t,
                        zlink_msg_t *parts_,
                        size_t part_count_,
                        void *)
{
    zlink_multipart_close (parts_, part_count_);
}

void noop_send_ready_handler (void *, void *)
{
}

void test_gateway_is_recv_only ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *gateway = zlink_gateway_new (ctx, "unit-gateway");
    TEST_ASSERT_NOT_NULL (gateway);

    void *poller = zlink_poller_new ();
    TEST_ASSERT_NOT_NULL (poller);

    TEST_ASSERT_EQUAL_INT (
      -1, zlink_recv_handler (gateway, &noop_socket_handler, NULL));
    TEST_ASSERT_EQUAL_INT (ENOTSUP, zlink_errno ());

    TEST_ASSERT_EQUAL_INT (
      -1, zlink_send_ready_handler (gateway, &noop_send_ready_handler, NULL));
    TEST_ASSERT_EQUAL_INT (ENOTSUP, zlink_errno ());

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_poller_add (poller, gateway, gateway, ZLINK_POLLOUT));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_poller_remove (poller, gateway));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_poller_destroy (&poller));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&gateway));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_spot_node_callback_policy ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *node = zlink_spot_node_new (ctx, "unit-spot");
    TEST_ASSERT_NOT_NULL (node);

    void *poller = zlink_poller_new ();
    TEST_ASSERT_NOT_NULL (poller);

    TEST_ASSERT_EQUAL_INT (
      -1, zlink_send_ready_handler (node, &noop_send_ready_handler, NULL));
    TEST_ASSERT_EQUAL_INT (EBUSY, zlink_errno ());

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_poller_add (poller, node, node, ZLINK_POLLIN));

    TEST_ASSERT_EQUAL_INT (
      -1, zlink_subscribe_handler (node, &noop_spot_handler, NULL));
    TEST_ASSERT_EQUAL_INT (EBUSY, zlink_errno ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_poller_remove (poller, node));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_subscribe_handler (node, &noop_spot_handler, NULL));

    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    char topic[64];
    size_t topic_len = sizeof (topic);
    TEST_ASSERT_EQUAL_INT (
      -1, zlink_subscribe (node, &parts, &part_count, ZLINK_DONTWAIT, topic,
                           &topic_len));
    TEST_ASSERT_EQUAL_INT (EBUSY, zlink_errno ());

    TEST_ASSERT_EQUAL_INT (
      -1, zlink_poller_add (poller, node, node, ZLINK_POLLIN));
    TEST_ASSERT_EQUAL_INT (EBUSY, zlink_errno ());
    TEST_ASSERT_EQUAL_INT (
      -1, zlink_poller_add (poller, node, node, ZLINK_POLLOUT));
    TEST_ASSERT_EQUAL_INT (EBUSY, zlink_errno ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_poller_destroy (&poller));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_stream_send_ready_requires_callback_mode ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *stream = zlink_socket (ctx, ZLINK_SOCKET_STREAM);
    TEST_ASSERT_NOT_NULL (stream);

    void *poller = zlink_poller_new ();
    TEST_ASSERT_NOT_NULL (poller);

    TEST_ASSERT_EQUAL_INT (
      -1, zlink_send_ready_handler (stream, &noop_send_ready_handler, NULL));
    TEST_ASSERT_EQUAL_INT (EBUSY, zlink_errno ());

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_poller_add (poller, stream, stream, ZLINK_POLLOUT));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_poller_remove (poller, stream));

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_recv_handler (stream, &noop_socket_handler, NULL));

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_send_ready_handler (stream, &noop_send_ready_handler, NULL));

    TEST_ASSERT_EQUAL_INT (
      -1, zlink_poller_add (poller, stream, stream, ZLINK_POLLOUT));
    TEST_ASSERT_EQUAL_INT (EBUSY, zlink_errno ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_poller_destroy (&poller));
    close_zero_linger (stream);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_generic_monitor_poller_accepts_non_pollin_events ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *server = zlink_socket (ctx, ZLINK_SOCKET_PAIR);
    TEST_ASSERT_NOT_NULL (server);
    void *client = zlink_socket (ctx, ZLINK_SOCKET_PAIR);
    TEST_ASSERT_NOT_NULL (client);

    zlink_socket_monitor_open_options_t socket_monitor_opts;
    memset (&socket_monitor_opts, 0, sizeof (socket_monitor_opts));
    socket_monitor_opts.events = ZLINK_EVENT_ALL;
    void *monitor = zlink_socket_monitor_open (server, &socket_monitor_opts);
    TEST_ASSERT_NOT_NULL (monitor);

    void *poller = zlink_poller_new ();
    TEST_ASSERT_NOT_NULL (poller);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_poller_add (poller, monitor, monitor, ZLINK_POLLOUT));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_poller_modify (poller, monitor, ZLINK_POLLOUT));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_poller_remove (poller, monitor));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_poller_destroy (&poller));
    close_zero_linger (client);
    close_zero_linger (server);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_monitor_close (&monitor));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}
}

int main (void)
{
    UNITY_BEGIN ();

    setup_test_environment ();

    RUN_TEST (test_gateway_is_recv_only);
    RUN_TEST (test_spot_node_callback_policy);
    RUN_TEST (test_stream_send_ready_requires_callback_mode);
    RUN_TEST (test_generic_monitor_poller_accepts_non_pollin_events);

    return UNITY_END ();
}
