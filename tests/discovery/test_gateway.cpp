/* SPDX-License-Identifier: MPL-2.0 */

#include "testutil.hpp"
#include "testutil_unity.hpp"
#include "../../src/discovery/protocol.hpp"
#ifndef ZLINK_BUILD_TESTS
#define ZLINK_BUILD_TESTS 1
#endif
#include "../../src/discovery/gateway.hpp"
#include "../../src/core/msg.hpp"

#include <string.h>

SETUP_TEARDOWN_TESTCONTEXT

// Helper function for debug logging
static void step_log (const char *msg_)
{
    if (getenv ("ZLINK_TEST_DEBUG")) {
        fprintf (stderr, "[gateway] %s\n", msg_ ? msg_ : "");
        fflush (stderr);
    }
}

// Helper function for printing errno
static void print_errno (const char *label_)
{
    if (getenv ("ZLINK_TEST_DEBUG")) {
        fprintf (stderr, "[gateway] %s errno=%d (%s)\n", label_, errno,
                 strerror (errno));
        fflush (stderr);
    }
}

// Helper to receive a message with timeout
static void recv_one_with_timeout (void *sock, zlink_msg_t *msg, int timeout_ms)
{
    step_log ("recv wait");
    zlink_pollitem_t items[1];
    items[0].socket = sock;
    items[0].fd = 0;
    items[0].events = ZLINK_POLLIN;
    items[0].revents = 0;
    int rc = zlink_poll (items, 1, timeout_ms);
    if (rc <= 0) {
        if (getenv ("ZLINK_TEST_DEBUG")) {
            fprintf (stderr, "[gateway] poll rc=%d errno=%d\n", rc, errno);
            fflush (stderr);
        }
    }
    TEST_ASSERT_TRUE_MESSAGE (rc > 0, "recv timeout");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_recv (msg, sock, 0));
}

// Setup registry with given pub/router endpoints
static void setup_registry (void *ctx,
                            void **registry_out,
                            const char *pub_ep,
                            const char *router_ep)
{
    void *registry = zlink_registry_new (ctx);
    TEST_ASSERT_NOT_NULL (registry);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_registry_set_endpoints (registry, pub_ep, router_ep));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_start (registry));
    *registry_out = registry;
}

// Test: Single service with TCP transport (refactored from test_router_fixed_endpoint_send)
void test_gateway_single_service_tcp ()
{
    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);
    const char *service_name = "svc";

    // Setup registry
    void *registry = NULL;
    step_log ("setup registry");
    setup_registry (ctx, &registry, "inproc://reg-pub-gateway1",
                    "inproc://reg-router-gateway1");
    // inproc requires bind-before-connect; give registry worker time to bind
    msleep (100);

    // Setup discovery client
    void *discovery = zlink_discovery_new (ctx);
    TEST_ASSERT_NOT_NULL (discovery);
    step_log ("connect discovery");
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_connect_registry (discovery, "inproc://reg-pub-gateway1"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_subscribe (discovery, service_name));

    // Setup provider with TCP endpoint
    step_log ("bind provider router");
    const char provider_rid[] = "PROV1";
    char advertise_ep[256] = {0};
    void *provider = zlink_provider_new (ctx);
    TEST_ASSERT_NOT_NULL (provider);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_provider_bind (provider, "tcp://127.0.0.1:*"));
    void *provider_router = zlink_provider_router (provider);
    TEST_ASSERT_NOT_NULL (provider_router);
    int probe = 1;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (provider_router, ZLINK_PROBE_ROUTER, &probe,
                        sizeof (probe)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (provider_router, ZLINK_ROUTING_ID, provider_rid,
                        sizeof (provider_rid) - 1));
    size_t advertise_len = sizeof (advertise_ep);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_getsockopt (provider_router, ZLINK_LAST_ENDPOINT, advertise_ep,
                        &advertise_len));

    step_log ("connect provider dealer");
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_provider_connect_registry (provider, "inproc://reg-router-gateway1"));
    step_log ("register service");
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_provider_register (provider, service_name, advertise_ep, 1));

    msleep (200);

    // Verify discovery sees the provider
    zlink_provider_info_t provider_info;
    memset (&provider_info, 0, sizeof (provider_info));
    size_t count = 1;
    step_log ("get providers");
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_get_providers (discovery, service_name, &provider_info, &count));
    TEST_ASSERT_EQUAL_INT (1, (int) count);
    TEST_ASSERT_EQUAL_STRING (advertise_ep, provider_info.endpoint);
    TEST_ASSERT_TRUE (provider_info.routing_id.size > 0);

    // Create gateway
    step_log ("create gateway socket");
    void *gateway = zlink_gateway_new (ctx, discovery);
    TEST_ASSERT_NOT_NULL (gateway);
    zlink::gateway_t *gateway_impl = static_cast<zlink::gateway_t *> (gateway);
    zlink::socket_base_t *gateway_socket =
      gateway_impl->get_router_socket (service_name);
    TEST_ASSERT_NOT_NULL (gateway_socket);

    step_log ("verify gateway connection");
    void *client = gateway_socket;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_connection_count (gateway, service_name));

    int timeout_ms = 2000;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (client, ZLINK_SNDTIMEO, &timeout_ms,
                        sizeof (timeout_ms)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (provider_router, ZLINK_RCVTIMEO, &timeout_ms,
                        sizeof (timeout_ms)));
    msleep (200);

    // Send message via gateway
    step_log ("send payload");
    zlink_msg_t payload;
    zlink_msg_init_size (&payload, 5);
    memcpy (zlink_msg_data (&payload), "hello", 5);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_gateway_connection_count (gateway, service_name));
    msleep (200);

    zlink_msg_t parts[1];
    parts[0] = payload;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_gateway_send (gateway, service_name, parts, 1, 0));

    // Provider receives the message
    void *router = provider_router;
    TEST_ASSERT_NOT_NULL (router);

    zlink_msg_t frame;
    zlink_msg_t payload_msg;
    zlink_msg_init (&frame);
    zlink_msg_init (&payload_msg);
    bool got_payload = false;
    for (int i = 0; i < 3 && !got_payload; ++i) {
        recv_one_with_timeout (router, &frame, timeout_ms);
        if (zlink_msg_size (&frame) == 5
            && memcmp (zlink_msg_data (&frame), "hello", 5) == 0) {
            payload_msg = frame;
            zlink_msg_init (&frame);
            got_payload = true;
            break;
        }
        if (!zlink_msg_more (&frame)) {
            TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_close (&frame));
            zlink_msg_init (&frame);
            continue;
        }
        recv_one_with_timeout (router, &payload_msg, timeout_ms);
        if (zlink_msg_size (&payload_msg) == 5
            && memcmp (zlink_msg_data (&payload_msg), "hello", 5) == 0) {
            got_payload = true;
            break;
        }
        TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_close (&frame));
        TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_close (&payload_msg));
        zlink_msg_init (&frame);
        zlink_msg_init (&payload_msg);
    }
    TEST_ASSERT_TRUE (got_payload);
    TEST_ASSERT_EQUAL_MEMORY ("hello", zlink_msg_data (&payload_msg), 5);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_close (&frame));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_close (&payload_msg));

    step_log ("cleanup");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&gateway));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_provider_destroy (&provider));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
}

// Test: Multiple services - verify gateway routes to correct provider
void test_gateway_multi_service_tcp ()
{
    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);

    // Setup registry
    void *registry = NULL;
    step_log ("setup registry");
    setup_registry (ctx, &registry, "inproc://reg-pub-gateway2",
                    "inproc://reg-router-gateway2");
    msleep (100);

    // Setup discovery client
    void *discovery = zlink_discovery_new (ctx);
    TEST_ASSERT_NOT_NULL (discovery);
    step_log ("connect discovery");
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_connect_registry (discovery, "inproc://reg-pub-gateway2"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_subscribe (discovery, "svc-A"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_subscribe (discovery, "svc-B"));

    // Setup provider A
    step_log ("setup provider A");
    char ep_a[256] = {0};
    void *provider_a = zlink_provider_new (ctx);
    TEST_ASSERT_NOT_NULL (provider_a);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_provider_bind (provider_a, "tcp://127.0.0.1:*"));
    void *router_a = zlink_provider_router (provider_a);
    TEST_ASSERT_NOT_NULL (router_a);
    int probe = 1;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (router_a, ZLINK_PROBE_ROUTER, &probe, sizeof (probe)));
    const char rid_a[] = "PROVA";
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (router_a, ZLINK_ROUTING_ID, rid_a, sizeof (rid_a) - 1));
    size_t len_a = sizeof (ep_a);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_getsockopt (router_a, ZLINK_LAST_ENDPOINT, ep_a, &len_a));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_provider_connect_registry (provider_a, "inproc://reg-router-gateway2"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_provider_register (provider_a, "svc-A", ep_a, 1));

    // Setup provider B
    step_log ("setup provider B");
    char ep_b[256] = {0};
    void *provider_b = zlink_provider_new (ctx);
    TEST_ASSERT_NOT_NULL (provider_b);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_provider_bind (provider_b, "tcp://127.0.0.1:*"));
    void *router_b = zlink_provider_router (provider_b);
    TEST_ASSERT_NOT_NULL (router_b);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (router_b, ZLINK_PROBE_ROUTER, &probe, sizeof (probe)));
    const char rid_b[] = "PROVB";
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (router_b, ZLINK_ROUTING_ID, rid_b, sizeof (rid_b) - 1));
    size_t len_b = sizeof (ep_b);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_getsockopt (router_b, ZLINK_LAST_ENDPOINT, ep_b, &len_b));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_provider_connect_registry (provider_b, "inproc://reg-router-gateway2"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_provider_register (provider_b, "svc-B", ep_b, 1));

    msleep (200);

    // Create gateway
    step_log ("create gateway");
    void *gateway = zlink_gateway_new (ctx, discovery);
    TEST_ASSERT_NOT_NULL (gateway);

    int timeout_ms = 2000;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (router_a, ZLINK_RCVTIMEO, &timeout_ms, sizeof (timeout_ms)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (router_b, ZLINK_RCVTIMEO, &timeout_ms, sizeof (timeout_ms)));
    msleep (200);

    // Send message to service A
    step_log ("send to svc-A");
    zlink_msg_t msg_a;
    zlink_msg_init_size (&msg_a, 6);
    memcpy (zlink_msg_data (&msg_a), "msg-to-A", 6);
    zlink_msg_t parts_a[1];
    parts_a[0] = msg_a;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_gateway_send (gateway, "svc-A", parts_a, 1, 0));

    // Provider A receives
    zlink_msg_t frame_a;
    zlink_msg_t payload_a;
    zlink_msg_init (&frame_a);
    zlink_msg_init (&payload_a);
    bool got_a = false;
    for (int i = 0; i < 3 && !got_a; ++i) {
        recv_one_with_timeout (router_a, &frame_a, timeout_ms);
        if (zlink_msg_size (&frame_a) == 6
            && memcmp (zlink_msg_data (&frame_a), "msg-to-A", 6) == 0) {
            payload_a = frame_a;
            zlink_msg_init (&frame_a);
            got_a = true;
            break;
        }
        if (!zlink_msg_more (&frame_a)) {
            TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_close (&frame_a));
            zlink_msg_init (&frame_a);
            continue;
        }
        recv_one_with_timeout (router_a, &payload_a, timeout_ms);
        if (zlink_msg_size (&payload_a) == 6
            && memcmp (zlink_msg_data (&payload_a), "msg-to-A", 6) == 0) {
            got_a = true;
            break;
        }
        TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_close (&frame_a));
        TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_close (&payload_a));
        zlink_msg_init (&frame_a);
        zlink_msg_init (&payload_a);
    }
    TEST_ASSERT_TRUE (got_a);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_close (&frame_a));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_close (&payload_a));

    // Send message to service B
    step_log ("send to svc-B");
    zlink_msg_t msg_b;
    zlink_msg_init_size (&msg_b, 8);
    memcpy (zlink_msg_data (&msg_b), "msg-to-B", 8);
    zlink_msg_t parts_b[1];
    parts_b[0] = msg_b;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_gateway_send (gateway, "svc-B", parts_b, 1, 0));

    // Provider B receives
    zlink_msg_t frame_b;
    zlink_msg_t payload_b;
    zlink_msg_init (&frame_b);
    zlink_msg_init (&payload_b);
    bool got_b = false;
    for (int i = 0; i < 3 && !got_b; ++i) {
        recv_one_with_timeout (router_b, &frame_b, timeout_ms);
        if (zlink_msg_size (&frame_b) == 8
            && memcmp (zlink_msg_data (&frame_b), "msg-to-B", 8) == 0) {
            payload_b = frame_b;
            zlink_msg_init (&frame_b);
            got_b = true;
            break;
        }
        if (!zlink_msg_more (&frame_b)) {
            TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_close (&frame_b));
            zlink_msg_init (&frame_b);
            continue;
        }
        recv_one_with_timeout (router_b, &payload_b, timeout_ms);
        if (zlink_msg_size (&payload_b) == 8
            && memcmp (zlink_msg_data (&payload_b), "msg-to-B", 8) == 0) {
            got_b = true;
            break;
        }
        TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_close (&frame_b));
        TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_close (&payload_b));
        zlink_msg_init (&frame_b);
        zlink_msg_init (&payload_b);
    }
    TEST_ASSERT_TRUE (got_b);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_close (&frame_b));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_close (&payload_b));

    step_log ("cleanup");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&gateway));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_provider_destroy (&provider_a));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_provider_destroy (&provider_b));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
}

// Test: WebSocket transport
void test_gateway_protocol_ws ()
{
    // Check if WebSocket is available
    if (!zlink_has ("ws")) {
        TEST_IGNORE_MESSAGE ("WebSocket not available");
        return;
    }

    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);
    const char *service_name = "svc-ws";

    // Setup registry
    void *registry = NULL;
    step_log ("setup registry");
    setup_registry (ctx, &registry, "inproc://reg-pub-gateway-ws",
                    "inproc://reg-router-gateway-ws");
    msleep (100);

    // Setup discovery client
    void *discovery = zlink_discovery_new (ctx);
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_connect_registry (discovery, "inproc://reg-pub-gateway-ws"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_subscribe (discovery, service_name));

    // Setup provider with WebSocket endpoint
    step_log ("bind provider with ws://");
    char advertise_ep[256] = {0};
    void *provider = zlink_provider_new (ctx);
    TEST_ASSERT_NOT_NULL (provider);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_provider_bind (provider, "ws://127.0.0.1:*"));
    void *provider_router = zlink_provider_router (provider);
    TEST_ASSERT_NOT_NULL (provider_router);
    int probe = 1;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (provider_router, ZLINK_PROBE_ROUTER, &probe, sizeof (probe)));
    const char rid[] = "PROVWS";
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (provider_router, ZLINK_ROUTING_ID, rid, sizeof (rid) - 1));
    size_t len = sizeof (advertise_ep);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_getsockopt (provider_router, ZLINK_LAST_ENDPOINT, advertise_ep, &len));

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_provider_connect_registry (provider, "inproc://reg-router-gateway-ws"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_provider_register (provider, service_name, advertise_ep, 1));

    msleep (200);

    // Create gateway
    step_log ("create gateway");
    void *gateway = zlink_gateway_new (ctx, discovery);
    TEST_ASSERT_NOT_NULL (gateway);

    int timeout_ms = 2000;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (provider_router, ZLINK_RCVTIMEO, &timeout_ms, sizeof (timeout_ms)));
    msleep (200);

    // Send message via gateway
    step_log ("send payload");
    zlink_msg_t payload;
    zlink_msg_init_size (&payload, 7);
    memcpy (zlink_msg_data (&payload), "ws-test", 7);
    zlink_msg_t parts[1];
    parts[0] = payload;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_gateway_send (gateway, service_name, parts, 1, 0));

    // Provider receives
    zlink_msg_t frame;
    zlink_msg_t payload_msg;
    zlink_msg_init (&frame);
    zlink_msg_init (&payload_msg);
    bool got_payload = false;
    for (int i = 0; i < 3 && !got_payload; ++i) {
        recv_one_with_timeout (provider_router, &frame, timeout_ms);
        if (zlink_msg_size (&frame) == 7
            && memcmp (zlink_msg_data (&frame), "ws-test", 7) == 0) {
            payload_msg = frame;
            zlink_msg_init (&frame);
            got_payload = true;
            break;
        }
        if (!zlink_msg_more (&frame)) {
            TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_close (&frame));
            zlink_msg_init (&frame);
            continue;
        }
        recv_one_with_timeout (provider_router, &payload_msg, timeout_ms);
        if (zlink_msg_size (&payload_msg) == 7
            && memcmp (zlink_msg_data (&payload_msg), "ws-test", 7) == 0) {
            got_payload = true;
            break;
        }
        TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_close (&frame));
        TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_close (&payload_msg));
        zlink_msg_init (&frame);
        zlink_msg_init (&payload_msg);
    }
    TEST_ASSERT_TRUE (got_payload);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_close (&frame));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_close (&payload_msg));

    step_log ("cleanup");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&gateway));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_provider_destroy (&provider));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
}

// Test: TLS transport
// Note: Full TLS communication requires certificates.
// This test verifies TLS availability only.
void test_gateway_protocol_tls ()
{
    // Check if TLS is available
    if (!zlink_has ("tls")) {
        TEST_IGNORE_MESSAGE ("TLS not available");
        return;
    }

    // TLS communication requires certificates - skip actual test
    TEST_IGNORE_MESSAGE ("TLS available (requires certificates for full test)");
}

// Test: WebSocket with TLS (wss://)
// Note: Full WSS communication requires certificates.
// This test verifies WSS availability only.
void test_gateway_protocol_wss ()
{
    // Check if WSS is available
    if (!zlink_has ("wss")) {
        TEST_IGNORE_MESSAGE ("WSS not available");
        return;
    }

    // WSS communication requires certificates - skip actual test
    TEST_IGNORE_MESSAGE ("WSS available (requires certificates for full test)");
}

int main (void)
{
    UNITY_BEGIN ();
    RUN_TEST (test_gateway_single_service_tcp);
    RUN_TEST (test_gateway_multi_service_tcp);
    RUN_TEST (test_gateway_protocol_ws);
    RUN_TEST (test_gateway_protocol_tls);
    RUN_TEST (test_gateway_protocol_wss);
    return UNITY_END ();
}
