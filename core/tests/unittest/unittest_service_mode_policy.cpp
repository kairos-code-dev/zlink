/* SPDX-License-Identifier: MPL-2.0 */

#include "../testutil.hpp"
#include "../testutil_unity.hpp"

#include <cerrno>
#include <cstring>

#include <utils/ip.hpp>
#include <unity.h>

void setUp ()
{
}

void tearDown ()
{
}

static void noop_gateway_handler (const zlink_routing_id_t *,
                                  zlink_msg_t *,
                                  size_t,
                                  void *)
{
}

static void noop_spot_handler (const zlink_routing_id_t *,
                               const char *,
                               size_t,
                               zlink_msg_t *,
                               size_t,
                               void *)
{
}

static int bind_gateway_with_port_seed (void *gateway_, int *seed_)
{
    char endpoint[MAX_SOCKET_STRING];
    for (int attempt = 0; attempt < 32; ++attempt) {
        snprintf (endpoint, sizeof (endpoint), "tcp://127.0.0.1:%d",
                  test_port (*seed_));
        if (zlink_gateway_bind (gateway_, endpoint) == 0)
            return 0;
        if (errno != EADDRINUSE && errno != EAGAIN)
            return -1;
        ++(*seed_);
    }
    errno = EADDRINUSE;
    return -1;
}

static void test_gateway_mode_policy_and_routing_id_lock ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *gateway = zlink_gateway_new (ctx, "unit-gateway");
    TEST_ASSERT_NOT_NULL (gateway);

    void *poller = zlink_poller_new ();
    TEST_ASSERT_NOT_NULL (poller);

    TEST_ASSERT_EQUAL_INT (
      -1, zlink_gateway_send_ready_handler (gateway, NULL, NULL));
    TEST_ASSERT_EQUAL_INT (EBUSY, zlink_errno ());

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_gateway_set_routing_id (gateway, "unit-gateway-rid", 16));

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_poller_add_gateway (poller, gateway, gateway, ZLINK_POLLOUT));

    TEST_ASSERT_EQUAL_INT (-1,
                           zlink_recv_handler (gateway, &noop_gateway_handler,
                                               NULL));
    TEST_ASSERT_EQUAL_INT (EBUSY, zlink_errno ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_poller_remove_gateway (poller, gateway));

    int bind_seed = 28010;
    TEST_ASSERT_SUCCESS_ERRNO (bind_gateway_with_port_seed (gateway, &bind_seed));

    TEST_ASSERT_EQUAL_INT (
      -1, zlink_gateway_set_routing_id (gateway, "late-rid", 8));
    TEST_ASSERT_EQUAL_INT (EFSM, zlink_errno ());

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_recv_handler (gateway, &noop_gateway_handler, NULL));

    zlink_routing_id_t source_rid;
    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    TEST_ASSERT_EQUAL_INT (
      -1, zlink_gateway_recv (gateway, &source_rid, &parts, &part_count,
                              ZLINK_DONTWAIT));
    TEST_ASSERT_EQUAL_INT (EBUSY, zlink_errno ());

    TEST_ASSERT_EQUAL_INT (
      -1, zlink_poller_add_gateway (poller, gateway, gateway, ZLINK_POLLIN));
    TEST_ASSERT_EQUAL_INT (EBUSY, zlink_errno ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_poller_destroy (&poller));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&gateway));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

static void test_spot_mode_policy ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *node = zlink_spot_node_new (ctx, "unit-spot");
    TEST_ASSERT_NOT_NULL (node);

    void *spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (spot);

    void *poller = zlink_poller_new ();
    TEST_ASSERT_NOT_NULL (poller);

    TEST_ASSERT_EQUAL_INT (-1,
                           zlink_spot_send_ready_handler (spot, NULL, NULL));
    TEST_ASSERT_EQUAL_INT (EBUSY, zlink_errno ());

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_poller_add_spot_sub (poller, spot, spot, ZLINK_POLLIN));

    TEST_ASSERT_EQUAL_INT (
      -1, zlink_recv_spot_handler (spot, &noop_spot_handler, NULL));
    TEST_ASSERT_EQUAL_INT (EBUSY, zlink_errno ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_poller_remove_spot_sub (poller, spot));

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_recv_spot_handler (spot, &noop_spot_handler, NULL));

    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    char topic[64];
    size_t topic_len = sizeof (topic);
    TEST_ASSERT_EQUAL_INT (
      -1, zlink_spot_sub_recv (spot, &parts, &part_count, ZLINK_DONTWAIT,
                               topic, &topic_len));
    TEST_ASSERT_EQUAL_INT (EBUSY, zlink_errno ());

    TEST_ASSERT_EQUAL_INT (
      -1, zlink_poller_add_spot_sub (poller, spot, spot, ZLINK_POLLIN));
    TEST_ASSERT_EQUAL_INT (EBUSY, zlink_errno ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_poller_destroy (&poller));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

static void test_spot_node_mode_policy_and_monitor_polling_independence ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *node = zlink_spot_node_new (ctx, "unit-spot-node");
    TEST_ASSERT_NOT_NULL (node);

    void *poller = zlink_poller_new ();
    TEST_ASSERT_NOT_NULL (poller);

    TEST_ASSERT_EQUAL_INT (
      -1, zlink_spot_node_send_ready_handler (node, NULL, NULL));
    TEST_ASSERT_EQUAL_INT (EBUSY, zlink_errno ());

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_poller_add_spot_sub (poller, node, node, ZLINK_POLLIN));

    TEST_ASSERT_EQUAL_INT (
      -1, zlink_recv_spot_handler (node, &noop_spot_handler, NULL));
    TEST_ASSERT_EQUAL_INT (EBUSY, zlink_errno ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_poller_remove_spot_sub (poller, node));

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_recv_spot_handler (node, &noop_spot_handler, NULL));

    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    char topic[64];
    size_t topic_len = sizeof (topic);
    TEST_ASSERT_EQUAL_INT (
      -1, zlink_spot_node_recv (node, &parts, &part_count, ZLINK_DONTWAIT,
                                topic, &topic_len));
    TEST_ASSERT_EQUAL_INT (EBUSY, zlink_errno ());

    TEST_ASSERT_EQUAL_INT (
      -1, zlink_poller_add_spot_sub (poller, node, node, ZLINK_POLLIN));
    TEST_ASSERT_EQUAL_INT (EBUSY, zlink_errno ());

    void *monitor = zlink_spot_node_monitor_open (
      node, ZLINK_SPOT_ROLE_SUB, ZLINK_MONITOR_EVENT_CLOSED,
      &zlink_service_monitor_ignore_handler, NULL);
    TEST_ASSERT_NOT_NULL (monitor);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_poller_add_monitor (poller, monitor, monitor, ZLINK_POLLIN));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_poller_remove_monitor (poller, monitor));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (monitor));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_poller_destroy (&poller));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

int main (void)
{
    UNITY_BEGIN ();

    zlink::initialize_network ();
    setup_test_environment ();

    RUN_TEST (test_gateway_mode_policy_and_routing_id_lock);
    RUN_TEST (test_spot_mode_policy);
    RUN_TEST (test_spot_node_mode_policy_and_monitor_polling_independence);

    zlink::shutdown_network ();

    return UNITY_END ();
}
