/* SPDX-License-Identifier: MPL-2.0 */

#include "testutil.hpp"
#include "testutil_unity.hpp"

#include <chrono>
#include <string.h>

SETUP_TEARDOWN_TESTCONTEXT

namespace
{
void set_router_rid (void *router_, const char *rid_)
{
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (router_, rid_, strlen (rid_)));
}

void set_router_probe (void *router_)
{
    int probe = 1;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_router_option (router_, ZLINK_ROUTER_OPT_PROBE, &probe, sizeof (probe)));
}

void set_router_mandatory (void *router_)
{
    int mandatory = 1;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_router_option (router_, ZLINK_ROUTER_OPT_MANDATORY, &mandatory,
                               sizeof (mandatory)));
}

void connect_router_to_peer_rid (void *router_, const char *endpoint_, const char *peer_rid_)
{
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_router_option (router_, ZLINK_ROUTER_OPT_CONNECT_ROUTING_ID, peer_rid_,
                               strlen (peer_rid_)));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (router_, endpoint_));
}

void send_routed_text (void *router_, const char *target_rid_, const char *text_)
{
    send_string_expect_success (router_, target_rid_, ZLINK_SNDMORE);
    send_string_expect_success (router_, text_, 0);
}

int try_send_routed_text (void *router_, const char *target_rid_, const char *text_)
{
    const int rc = zlink_send (router_, target_rid_, strlen (target_rid_), ZLINK_SNDMORE);
    if (rc != 0)
        return rc;
    return zlink_send (router_, text_, strlen (text_), ZLINK_DONTWAIT);
}

bool wait_for_probe_identity (void *router_, const zlink_routing_id_t *expected_source_)
{
    const auto deadline = std::chrono::steady_clock::now () + std::chrono::milliseconds (3000);
    while (std::chrono::steady_clock::now () < deadline) {
        unsigned char source[256];
        const int source_size = zlink_recv (router_, source, sizeof (source), ZLINK_DONTWAIT);
        if (source_size >= 0) {
            TEST_ASSERT_EQUAL_UINT (expected_source_->size, source_size);
            TEST_ASSERT_EQUAL_MEMORY (expected_source_->data, source, expected_source_->size);
            unsigned char payload[1];
            const int payload_size = zlink_recv (router_, payload, sizeof (payload), 0);
            TEST_ASSERT_EQUAL_INT (0, payload_size);
            return true;
        }
        TEST_ASSERT_EQUAL_INT (EAGAIN, zlink_errno ());
        msleep (10);
    }
    return false;
}

void recv_routed_text (void *router_, const char *expected_source_, const char *expected_text_)
{
    const auto deadline = std::chrono::steady_clock::now () + std::chrono::milliseconds (3000);
    while (std::chrono::steady_clock::now () < deadline) {
        unsigned char source[256];
        const int source_size = zlink_recv (router_, source, sizeof (source), ZLINK_DONTWAIT);
        if (source_size >= 0) {
            if (source_size == 0)
                continue;
            TEST_ASSERT_EQUAL_UINT (strlen (expected_source_), source_size);
            TEST_ASSERT_EQUAL_MEMORY (expected_source_, source, strlen (expected_source_));
            unsigned char payload[256];
            const int payload_size = zlink_recv (router_, payload, sizeof (payload), 0);
            TEST_ASSERT_GREATER_OR_EQUAL_INT (0, payload_size);
            if (payload_size == 0)
                continue;
            TEST_ASSERT_EQUAL_UINT (strlen (expected_text_), payload_size);
            TEST_ASSERT_EQUAL_MEMORY (expected_text_, payload, strlen (expected_text_));
            return;
        }
        TEST_ASSERT_EQUAL_INT (EAGAIN, zlink_errno ());
        msleep (10);
    }
    TEST_FAIL_MESSAGE ("timed out waiting for routed text");
}

void test_router_probe_identity_supports_reverse_rid_send_cleanup_and_relearn ()
{
    void *server_b = test_context_socket (ZLINK_SOCKET_ROUTER);
    void *client_a = test_context_socket (ZLINK_SOCKET_ROUTER);
    TEST_ASSERT_NOT_NULL (server_b);
    TEST_ASSERT_NOT_NULL (client_a);

    set_router_rid (server_b, "inbound-node-b");
    set_router_rid (client_a, "inbound-node-a");
    set_router_probe (client_a);
    set_router_mandatory (server_b);

    const char *endpoint = "inproc://spot-data-plane-inbound-identity";
    TEST_ASSERT_SUCCESS_ERRNO (zlink_bind (server_b, endpoint));
    connect_router_to_peer_rid (client_a, endpoint, "inbound-node-b");
    msleep (SETTLE_TIME);

    send_routed_text (client_a, "inbound-node-b", "a-to-b");
    recv_routed_text (server_b, "inbound-node-a", "a-to-b");

    send_routed_text (server_b, "inbound-node-a", "b-to-a");
    recv_routed_text (client_a, "inbound-node-b", "b-to-a");

    TEST_ASSERT_EQUAL_INT (ZLINK_CONNECT_OK, zlink_disconnect (client_a, endpoint));
    msleep (SETTLE_TIME * 2);
    TEST_ASSERT_NOT_EQUAL (0, try_send_routed_text (server_b, "inbound-node-a", "after-disconnect"));

    connect_router_to_peer_rid (client_a, endpoint, "inbound-node-b");
    msleep (SETTLE_TIME);
    send_routed_text (server_b, "inbound-node-a", "after-reconnect");
    recv_routed_text (client_a, "inbound-node-b", "after-reconnect");

    test_context_socket_close_zero_linger (client_a);
    test_context_socket_close_zero_linger (server_b);
}

void test_spot_connect_peer_rid_defaults_probe_for_reverse_identity_learning ()
{
    void *server_b = test_context_socket (ZLINK_SOCKET_ROUTER);
    TEST_ASSERT_NOT_NULL (server_b);

    set_router_rid (server_b, "spot-inbound-node-b");
    set_router_mandatory (server_b);

    const char *server_endpoint = "inproc://spot-connect-peer-rid-inbound-identity-b";
    TEST_ASSERT_SUCCESS_ERRNO (zlink_bind (server_b, server_endpoint));

    zlink_spot_node_options_t options;
    memset (&options, 0, sizeof (options));
    options.mode = ZLINK_SPOT_NODE_MODE_ROUTED;
    void *client_a = zlink_spot_node_new (get_test_context (), &options);
    TEST_ASSERT_NOT_NULL (client_a);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_set_router_bind (client_a,
                                       "inproc://spot-connect-peer-rid-inbound-identity-a"));

    zlink_spot_node_status_t client_status;
    memset (&client_status, 0, sizeof (client_status));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_status (client_a, &client_status));
    TEST_ASSERT_TRUE (client_status.node_routing_id.size > 0);

    zlink_routing_id_t server_rid;
    memset (&server_rid, 0, sizeof (server_rid));
    server_rid.size = static_cast<uint8_t> (strlen ("spot-inbound-node-b"));
    memcpy (server_rid.data, "spot-inbound-node-b", server_rid.size);

    TEST_ASSERT_EQUAL_INT (ZLINK_CONNECT_OK,
                           zlink_spot_node_connect_peer_rid (client_a, &server_rid,
                                                             server_endpoint));
    TEST_ASSERT_TRUE_MESSAGE (wait_for_probe_identity (server_b, &client_status.node_routing_id),
                              "spot connect_peer_rid did not send an inbound identity probe");

    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_spot_node_destroy (&client_a));
    test_context_socket_close_zero_linger (server_b);
}
}

int main ()
{
    setup_test_environment ();

    UNITY_BEGIN ();

    RUN_TEST (test_router_probe_identity_supports_reverse_rid_send_cleanup_and_relearn);
    RUN_TEST (test_spot_connect_peer_rid_defaults_probe_for_reverse_identity_learning);

    return UNITY_END ();
}
