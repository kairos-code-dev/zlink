/* SPDX-License-Identifier: MPL-2.0 */

#include "../../testutil.hpp"
#include "../../testutil_unity.hpp"

#include <string.h>

SETUP_TEARDOWN_TESTCONTEXT

namespace
{
void noop_gateway_handler (const zlink_routing_id_t *,
                           zlink_msg_t *parts_,
                           size_t part_count_,
                           void *)
{
    zlink_multipart_close (parts_, part_count_);
}

void noop_send_ready_handler (void *, void *)
{
}

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

void send_gateway_text_rid (void *gateway_,
                            const zlink_routing_id_t *rid_,
                            const char *text_)
{
    zlink_msg_t part;
    init_text_part (&part, text_);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_gateway_send_rid (gateway_, rid_, &part, 1, 0));
}

void test_gateway_recv_handler_blocks_recv_and_pollin ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    void *gateway = zlink_gateway_new (ctx, "gw-handler-contract");
    TEST_ASSERT_NOT_NULL (gateway);

    void *poller = zlink_poller_new ();
    TEST_ASSERT_NOT_NULL (poller);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_recv_handler (gateway, &noop_gateway_handler, NULL));

    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    TEST_ASSERT_EQUAL_INT (
      -1, zlink_gateway_recv (gateway, NULL, &parts, &part_count,
                              ZLINK_DONTWAIT));
    TEST_ASSERT_EQUAL_INT (EBUSY, zlink_errno ());
    TEST_ASSERT_EQUAL_INT (
      -1, zlink_recv (gateway, NULL, &parts, &part_count, ZLINK_DONTWAIT));
    TEST_ASSERT_EQUAL_INT (EBUSY, zlink_errno ());

    TEST_ASSERT_EQUAL_INT (
      -1, zlink_poller_add (poller, gateway, gateway, ZLINK_POLLIN));
    TEST_ASSERT_EQUAL_INT (EBUSY, zlink_errno ());
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_poller_add (poller, gateway, gateway, ZLINK_POLLOUT));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_poller_remove (poller, gateway));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_poller_destroy (&poller));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&gateway));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_gateway_send_ready_handler_blocks_pollout_only ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    void *gateway = zlink_gateway_new (ctx, "gw-ready-contract");
    TEST_ASSERT_NOT_NULL (gateway);

    void *poller = zlink_poller_new ();
    TEST_ASSERT_NOT_NULL (poller);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_send_ready_handler (gateway, &noop_send_ready_handler, NULL));

    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    TEST_ASSERT_EQUAL_INT (
      -1, zlink_gateway_recv (gateway, NULL, &parts, &part_count,
                              ZLINK_DONTWAIT));
    TEST_ASSERT_NOT_EQUAL (EBUSY, zlink_errno ());

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_poller_add (poller, gateway, gateway, ZLINK_POLLIN));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_poller_remove (poller, gateway));
    TEST_ASSERT_EQUAL_INT (
      -1, zlink_poller_add (poller, gateway, gateway, ZLINK_POLLOUT));
    TEST_ASSERT_EQUAL_INT (EBUSY, zlink_errno ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_poller_destroy (&poller));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&gateway));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_gateway_recv_mode_request_reply_with_discovery ()
{
    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);

    char registry_pub[MAX_SOCKET_STRING];
    char registry_router[MAX_SOCKET_STRING];
    int registry_seed = 24600;
    void *registry = NULL;
    for (int attempt = 0; attempt < 32; ++attempt) {
        registry = zlink_registry_new (ctx);
        TEST_ASSERT_NOT_NULL (registry);
        snprintf (registry_pub, sizeof (registry_pub), "tcp://127.0.0.1:%d",
                  test_port (registry_seed));
        snprintf (registry_router, sizeof (registry_router),
                  "tcp://127.0.0.1:%d", test_port (registry_seed + 1));
        if (zlink_registry_set_broadcast_interval (registry, 50) == 0
            && zlink_registry_bind (registry, registry_pub, registry_router)
                 == 0) {
            break;
        }
        zlink_registry_destroy (&registry);
        registry = NULL;
        registry_seed += 2;
    }
    TEST_ASSERT_NOT_NULL (registry);

    void *server_discovery =
      zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    void *client_discovery =
      zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (server_discovery);
    TEST_ASSERT_NOT_NULL (client_discovery);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_connect_registry (server_discovery, registry_router));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_connect_registry (client_discovery, registry_router));

    void *server = zlink_gateway_new (ctx, "gw-discovery-contract");
    void *client = zlink_gateway_new (ctx, "gw-discovery-contract");
    TEST_ASSERT_NOT_NULL (server);
    TEST_ASSERT_NOT_NULL (client);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_gateway_attach_discovery (server, server_discovery));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_gateway_attach_discovery (client, client_discovery));

    char endpoint[MAX_SOCKET_STRING];
    int bind_seed = 24610;
    bind_gateway_with_port_seed (server, &bind_seed, endpoint, sizeof (endpoint));
    TEST_ASSERT_TRUE (wait_for_ready_routes (client, 1, 5000));

    send_gateway_text (client, "ping");

    zlink_routing_id_t source_rid;
    memset (&source_rid, 0, sizeof (source_rid));
    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_gateway_recv (server, &source_rid, &parts, &part_count, 0));
    TEST_ASSERT_EQUAL_UINT64 (1, part_count);
    TEST_ASSERT_EQUAL_MEMORY ("ping", zlink_msg_data (&parts[0]), 4);
    zlink_multipart_close (parts, part_count);
    free (parts);

    send_gateway_text_rid (server, &source_rid, "pong");

    zlink_routing_id_t reply_rid;
    memset (&reply_rid, 0, sizeof (reply_rid));
    zlink_msg_t *reply_parts = NULL;
    size_t reply_part_count = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_gateway_recv (client, &reply_rid, &reply_parts, &reply_part_count,
                          0));
    TEST_ASSERT_EQUAL_UINT64 (1, reply_part_count);
    TEST_ASSERT_EQUAL_MEMORY ("pong", zlink_msg_data (&reply_parts[0]), 4);
    zlink_multipart_close (reply_parts, reply_part_count);
    free (reply_parts);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&client));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&server));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&client_discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&server_discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
}
}

int main (int, char **)
{
    setup_test_environment ();

    UNITY_BEGIN ();
    RUN_TEST (test_gateway_recv_handler_blocks_recv_and_pollin);
    RUN_TEST (test_gateway_send_ready_handler_blocks_pollout_only);
    RUN_TEST (test_gateway_recv_mode_request_reply_with_discovery);
    return UNITY_END ();
}
