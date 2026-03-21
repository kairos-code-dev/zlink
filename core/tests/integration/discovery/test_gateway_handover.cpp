/* SPDX-License-Identifier: MPL-2.0 */

#include "../../testutil.hpp"
#include "../../testutil_unity.hpp"

#include <string.h>

SETUP_TEARDOWN_TESTCONTEXT

namespace
{
bool read_gateway_snapshot (void *gateway_, zlink_monitor_snapshot_t *out_)
{
    if (!gateway_ || !out_)
        return false;

    zlink_service_monitor_open_options_t opts;
    memset (&opts, 0, sizeof (opts));
    opts.events = ZLINK_GATEWAY_MONITOR_EVENT_READY_CHANGED
                  | ZLINK_GATEWAY_MONITOR_EVENT_SEND_READY_CHANGED
                  | ZLINK_GATEWAY_ROUTE_UP | ZLINK_GATEWAY_ROUTE_DOWN
                  | ZLINK_GATEWAY_MONITOR_EVENT_ERROR;
    void *monitor = zlink_service_monitor_open (gateway_, &opts);
    if (!monitor)
        return false;

    const int rc = zlink_monitor_snapshot (monitor, out_);
    zlink_monitor_close (&monitor);
    return rc == 0;
}

bool wait_for_ready_routes (void *gateway_, int expected_, int timeout_ms_)
{
    const int step_ms = 10;
    const int attempts = timeout_ms_ / step_ms;
    for (int i = 0; i < attempts; ++i) {
        zlink_monitor_snapshot_t snapshot;
        memset (&snapshot, 0, sizeof (snapshot));
        if (read_gateway_snapshot (gateway_, &snapshot)
            && static_cast<int> (snapshot.ready_count) >= expected_) {
            return true;
        }
        msleep (step_ms);
    }

    zlink_monitor_snapshot_t snapshot;
    memset (&snapshot, 0, sizeof (snapshot));
    return read_gateway_snapshot (gateway_, &snapshot)
           && static_cast<int> (snapshot.ready_count) >= expected_;
}

bool wait_for_ready_routes_exact (void *gateway_, int expected_, int timeout_ms_)
{
    const int step_ms = 10;
    const int attempts = timeout_ms_ / step_ms;
    for (int i = 0; i < attempts; ++i) {
        zlink_monitor_snapshot_t snapshot;
        memset (&snapshot, 0, sizeof (snapshot));
        if (read_gateway_snapshot (gateway_, &snapshot)
            && static_cast<int> (snapshot.ready_count) == expected_) {
            return true;
        }
        msleep (step_ms);
    }

    zlink_monitor_snapshot_t snapshot;
    memset (&snapshot, 0, sizeof (snapshot));
    return read_gateway_snapshot (gateway_, &snapshot)
           && static_cast<int> (snapshot.ready_count) == expected_;
}

void init_text_part (zlink_msg_t *part_, const char *text_)
{
    const size_t size = strlen (text_);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (part_, size));
    memcpy (zlink_msg_data (part_), text_, size);
}

void send_gateway_text (void *gateway_, const char *text_)
{
    zlink_msg_t part;
    init_text_part (&part, text_);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_send (gateway_, &part, 1, 0));
}

void bind_gateway_with_port_seed (void *gateway_,
                                  int *seed_,
                                  char *endpoint_out_,
                                  size_t endpoint_size_)
{
    for (int attempt = 0; attempt < 64; ++attempt) {
        snprintf (endpoint_out_, endpoint_size_, "tcp://127.0.0.1:%d",
                  test_port (*seed_));
        if (zlink_gateway_bind (gateway_, endpoint_out_) == 0) {
            ++(*seed_);
            return;
        }
        if (errno != EADDRINUSE)
            TEST_FAIL_MESSAGE ("gateway bind failed");
        ++(*seed_);
    }

    TEST_FAIL_MESSAGE ("gateway bind seed exhausted");
}

void bind_registry_with_port_seed (void *registry_,
                                   int *seed_,
                                   char *pub_endpoint_out_,
                                   size_t pub_endpoint_size_,
                                   char *router_endpoint_out_,
                                   size_t router_endpoint_size_)
{
    for (int attempt = 0; attempt < 64; ++attempt) {
        snprintf (pub_endpoint_out_, pub_endpoint_size_, "tcp://127.0.0.1:%d",
                  test_port (*seed_));
        snprintf (router_endpoint_out_, router_endpoint_size_,
                  "tcp://127.0.0.1:%d", test_port (*seed_ + 1));
        if (zlink_registry_bind (registry_, pub_endpoint_out_, router_endpoint_out_)
            == 0) {
            *seed_ += 2;
            return;
        }
        if (errno != EADDRINUSE)
            TEST_FAIL_MESSAGE ("registry bind failed");
        *seed_ += 2;
    }

    TEST_FAIL_MESSAGE ("registry bind seed exhausted");
}

void recv_gateway_text (void *gateway_,
                        char *payload_out_,
                        size_t payload_size_,
                        zlink_routing_id_t *source_rid_out_)
{
    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_gateway_recv (gateway_, source_rid_out_, &parts, &part_count, 0));
    TEST_ASSERT_EQUAL_UINT64 (1, part_count);
    const size_t size = zlink_msg_size (&parts[0]);
    const size_t copy_size =
      size < payload_size_ - 1 ? size : payload_size_ - 1;
    memcpy (payload_out_, zlink_msg_data (&parts[0]), copy_size);
    payload_out_[copy_size] = '\0';
    zlink_multipart_close (parts, part_count);
    free (parts);
}

void test_gateway_handover_provider_restart ()
{
    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);

    char registry_pub[MAX_SOCKET_STRING];
    char registry_router[MAX_SOCKET_STRING];
    int registry_seed = 25800;
    void *registry = zlink_registry_new (ctx);
    TEST_ASSERT_NOT_NULL (registry);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_registry_set_broadcast_interval (registry, 50));
    bind_registry_with_port_seed (registry, &registry_seed, registry_pub,
                                  sizeof (registry_pub), registry_router,
                                  sizeof (registry_router));

    void *client_discovery =
      zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (client_discovery);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_connect_registry (client_discovery, registry_router));
    void *client = zlink_gateway_new (ctx, "ho-svc");
    TEST_ASSERT_NOT_NULL (client);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_gateway_attach_discovery (client, client_discovery));

    void *server1_discovery =
      zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (server1_discovery);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_connect_registry (server1_discovery, registry_router));
    void *server1 = zlink_gateway_new (ctx, "ho-svc");
    TEST_ASSERT_NOT_NULL (server1);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_gateway_attach_discovery (server1, server1_discovery));

    char endpoint1[MAX_SOCKET_STRING];
    int bind_seed = 25810;
    bind_gateway_with_port_seed (server1, &bind_seed, endpoint1,
                                 sizeof (endpoint1));
    TEST_ASSERT_TRUE (wait_for_ready_routes (client, 1, 5000));

    send_gateway_text (client, "msg1");

    zlink_routing_id_t source_rid;
    memset (&source_rid, 0, sizeof (source_rid));
    char payload[64];
    recv_gateway_text (server1, payload, sizeof (payload), &source_rid);
    TEST_ASSERT_EQUAL_STRING ("msg1", payload);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&server1));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&server1_discovery));

    void *server2_discovery =
      zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (server2_discovery);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_connect_registry (server2_discovery, registry_router));
    void *server2 = zlink_gateway_new (ctx, "ho-svc");
    TEST_ASSERT_NOT_NULL (server2);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_gateway_attach_discovery (server2, server2_discovery));

    char endpoint2[MAX_SOCKET_STRING];
    bind_gateway_with_port_seed (server2, &bind_seed, endpoint2,
                                 sizeof (endpoint2));
    TEST_ASSERT_TRUE (wait_for_ready_routes (client, 1, 5000));

    memset (&source_rid, 0, sizeof (source_rid));
    send_gateway_text (client, "msg2");
    recv_gateway_text (server2, payload, sizeof (payload), &source_rid);
    TEST_ASSERT_EQUAL_STRING ("msg2", payload);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&server2));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&server2_discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&client));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&client_discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
}

void test_provider_handover_gateway_reconnect ()
{
    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *client = zlink_gateway_new (ctx, "manual-ho");
    void *server1 = zlink_gateway_new (ctx, "manual-ho");
    void *server2 = zlink_gateway_new (ctx, "manual-ho");
    TEST_ASSERT_NOT_NULL (client);
    TEST_ASSERT_NOT_NULL (server1);
    TEST_ASSERT_NOT_NULL (server2);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_routing_id (server1, "srv1", strlen ("srv1")));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_routing_id (server2, "srv2", strlen ("srv2")));

    char endpoint1[MAX_SOCKET_STRING];
    char endpoint2[MAX_SOCKET_STRING];
    int bind_seed = 25830;
    bind_gateway_with_port_seed (server1, &bind_seed, endpoint1,
                                 sizeof (endpoint1));
    bind_gateway_with_port_seed (server2, &bind_seed, endpoint2,
                                 sizeof (endpoint2));

    zlink_routing_id_t rid1;
    zlink_routing_id_t rid2;
    memset (&rid1, 0, sizeof (rid1));
    memset (&rid2, 0, sizeof (rid2));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_routing_id (server1, &rid1));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_routing_id (server2, &rid2));

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_gateway_connect (client, endpoint1, &rid1));
    TEST_ASSERT_TRUE (wait_for_ready_routes (client, 1, 5000));
    send_gateway_text (client, "gw-1");

    zlink_routing_id_t source_rid;
    memset (&source_rid, 0, sizeof (source_rid));
    char payload[64];
    recv_gateway_text (server1, payload, sizeof (payload), &source_rid);
    TEST_ASSERT_EQUAL_STRING ("gw-1", payload);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_disconnect (client, endpoint1));
    TEST_ASSERT_TRUE (wait_for_ready_routes_exact (client, 0, 5000));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_gateway_connect (client, endpoint2, &rid2));
    TEST_ASSERT_TRUE (wait_for_ready_routes (client, 1, 5000));
    send_gateway_text (client, "gw-2");

    memset (&source_rid, 0, sizeof (source_rid));
    recv_gateway_text (server2, payload, sizeof (payload), &source_rid);
    TEST_ASSERT_EQUAL_STRING ("gw-2", payload);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&server2));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&server1));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&client));
}
}

int main (void)
{
    setup_test_environment ();

    UNITY_BEGIN ();
    RUN_TEST (test_gateway_handover_provider_restart);
    RUN_TEST (test_provider_handover_gateway_reconnect);
    return UNITY_END ();
}
