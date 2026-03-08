/* SPDX-License-Identifier: MPL-2.0 */

#include "../testutil.hpp"
#include "../testutil_unity.hpp"

#include <string.h>

SETUP_TEARDOWN_TESTCONTEXT

static void step_log (const char *msg_)
{
    if (getenv ("ZLINK_TEST_DEBUG")) {
        fprintf (stderr, "[handover] %s\n", msg_ ? msg_ : "");
        fflush (stderr);
    }
}

static void wait_gateway_ready (void *gateway,
                                const char *service_name,
                                int timeout_ms)
{
    const int sleep_ms_step = 5;
    const int attempts = timeout_ms / sleep_ms_step;
    for (int i = 0; i < attempts; ++i) {
        const int count =
          zlink_gateway_connection_count (gateway, service_name);
        if (count > 0)
            return;
        msleep (sleep_ms_step);
    }
    TEST_FAIL_MESSAGE ("gateway connection timeout");
}

static void send_gateway_with_timeout (void *gateway,
                                       const char *service_name,
                                       zlink_msg_t *parts,
                                       size_t part_count,
                                       int timeout_ms)
{
    const int sleep_ms_step = 2;
    const int attempts = timeout_ms / sleep_ms_step;
    for (int i = 0; i < attempts; ++i) {
        const int rc = zlink_gateway_send (gateway, service_name, parts,
                                           part_count, ZLINK_DONTWAIT);
        if (rc == 0)
            return;
        if (errno != EAGAIN && errno != EHOSTUNREACH)
            break;
        msleep (sleep_ms_step);
    }
    TEST_FAIL_MESSAGE ("gateway send timeout");
}

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

static bool recv_payload (void *receiver,
                          const char *expected,
                          size_t expected_len,
                          int timeout_ms)
{
    const int sleep_ms_step = 5;
    const int attempts = timeout_ms / sleep_ms_step;
    for (int i = 0; i < attempts; ++i) {
        zlink_msg_t *parts = NULL;
        size_t part_count = 0;
        if (zlink_receiver_recv (receiver, &parts, &part_count, ZLINK_DONTWAIT,
                                 NULL)
            == 0) {
            bool got = false;
            for (size_t j = 0; j < part_count; ++j) {
                if (zlink_msg_size (&parts[j]) == expected_len
                    && memcmp (zlink_msg_data (&parts[j]), expected,
                               expected_len)
                         == 0) {
                    got = true;
                    break;
                }
            }
            zlink_multipart_close (parts, part_count);
            if (got)
                return true;
        } else if (errno != EAGAIN) {
            break;
        }
        msleep (sleep_ms_step);
    }
    return false;
}

// Test: Provider restart with same routing id — gateway should deliver to new
// provider after handover.
void test_gateway_handover_provider_restart ()
{
    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);
    const char *service_name = "ho-svc";
    const int timeout_ms = 3000;

    // 1. Setup registry + discovery
    step_log ("setup registry");
    void *registry = NULL;
    setup_registry (ctx, &registry, "inproc://reg-pub-ho1",
                    "inproc://reg-router-ho1");
    msleep (100);

    void *discovery = zlink_discovery_new_typed (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_connect_registry (discovery, "inproc://reg-pub-ho1"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_subscribe (discovery, service_name));

    // 2. Provider1 with routing id "PROV-HO"
    step_log ("setup provider1");
    char ep1[256] = {0};
    snprintf (ep1, sizeof (ep1), "tcp://127.0.0.1:%d", test_port (22650));
    void *provider1 = zlink_receiver_new (ctx, NULL);
    TEST_ASSERT_NOT_NULL (provider1);
    const char prov_rid[] = "PROV-HO";
    TEST_ASSERT_SUCCESS_ERRNO (zlink_receiver_set_routing_id (
      provider1, prov_rid, sizeof (prov_rid) - 1));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_receiver_bind (provider1, ep1));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_receiver_connect_registry (provider1, "inproc://reg-router-ho1"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_receiver_register (provider1, service_name, ep1, 1));
    int rcvtimeo = timeout_ms;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_receiver_set_option (
      provider1, ZLINK_RECEIVER_OPT_RCVTIMEO, &rcvtimeo, sizeof (rcvtimeo)));
    msleep (200);

    // 3. Gateway
    step_log ("create gateway");
    void *gateway = zlink_gateway_new (ctx, discovery, NULL);
    TEST_ASSERT_NOT_NULL (gateway);
    wait_gateway_ready (gateway, service_name, timeout_ms);
    msleep (200);

    // 4. Send message -> Provider1 receives
    step_log ("send msg1 to provider1");
    {
        zlink_msg_t parts[1];
        zlink_msg_init_size (&parts[0], 4);
        memcpy (zlink_msg_data (&parts[0]), "msg1", 4);
        send_gateway_with_timeout (gateway, service_name, parts, 1,
                                   timeout_ms);
    }
    TEST_ASSERT_TRUE (recv_payload (provider1, "msg1", 4, timeout_ms));

    // 5. Unregister + destroy provider1 (simulate restart)
    step_log ("unregister + destroy provider1");
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_receiver_unregister (provider1, service_name));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_receiver_destroy (&provider1));
    msleep (300);

    // 6. Provider2 with same routing id "PROV-HO", new tcp port
    step_log ("setup provider2 (same rid)");
    char ep2[256] = {0};
    snprintf (ep2, sizeof (ep2), "tcp://127.0.0.1:%d", test_port (22651));
    void *provider2 = zlink_receiver_new (ctx, NULL);
    TEST_ASSERT_NOT_NULL (provider2);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_receiver_set_routing_id (
      provider2, prov_rid, sizeof (prov_rid) - 1));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_receiver_bind (provider2, ep2));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_receiver_connect_registry (provider2, "inproc://reg-router-ho1"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_receiver_register (provider2, service_name, ep2, 1));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_receiver_set_option (
      provider2, ZLINK_RECEIVER_OPT_RCVTIMEO, &rcvtimeo, sizeof (rcvtimeo)));
    msleep (300);
    wait_gateway_ready (gateway, service_name, timeout_ms);
    msleep (200);

    // 7. Send message -> Provider2 receives
    step_log ("send msg2 to provider2");
    {
        zlink_msg_t parts[1];
        zlink_msg_init_size (&parts[0], 4);
        memcpy (zlink_msg_data (&parts[0]), "msg2", 4);
        send_gateway_with_timeout (gateway, service_name, parts, 1,
                                   timeout_ms);
    }
    TEST_ASSERT_TRUE (recv_payload (provider2, "msg2", 4, timeout_ms));

    step_log ("cleanup");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&gateway));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_receiver_destroy (&provider2));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
}

// Test: Gateway reconnect with same routing id — provider should accept the
// new connection via handover.
void test_provider_handover_gateway_reconnect ()
{
    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);
    const char *service_name = "ho-svc2";
    const int timeout_ms = 3000;
    const char gw_rid[] = "GW-HO";

    // 1. Setup registry + discovery1
    step_log ("setup registry");
    void *registry = NULL;
    setup_registry (ctx, &registry, "inproc://reg-pub-ho2",
                    "inproc://reg-router-ho2");
    msleep (100);

    void *discovery1 = zlink_discovery_new_typed (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (discovery1);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_connect_registry (discovery1, "inproc://reg-pub-ho2"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_subscribe (discovery1, service_name));

    // 2. Provider
    step_log ("setup provider");
    char ep[256] = {0};
    snprintf (ep, sizeof (ep), "tcp://127.0.0.1:%d", test_port (22652));
    void *provider = zlink_receiver_new (ctx, NULL);
    TEST_ASSERT_NOT_NULL (provider);
    const char prov_rid[] = "PROV-HO2";
    TEST_ASSERT_SUCCESS_ERRNO (zlink_receiver_set_routing_id (
      provider, prov_rid, sizeof (prov_rid) - 1));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_receiver_bind (provider, ep));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_receiver_connect_registry (provider, "inproc://reg-router-ho2"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_receiver_register (provider, service_name, ep, 1));
    int rcvtimeo = timeout_ms;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_receiver_set_option (
      provider, ZLINK_RECEIVER_OPT_RCVTIMEO, &rcvtimeo, sizeof (rcvtimeo)));
    msleep (200);

    // 3. Gateway1 with routing id "GW-HO"
    step_log ("create gateway1");
    void *gateway1 = zlink_gateway_new (ctx, discovery1, gw_rid);
    TEST_ASSERT_NOT_NULL (gateway1);
    wait_gateway_ready (gateway1, service_name, timeout_ms);
    msleep (200);

    // 4. Send message via gateway1 -> provider receives
    step_log ("send msg1 via gateway1");
    {
        zlink_msg_t parts[1];
        zlink_msg_init_size (&parts[0], 4);
        memcpy (zlink_msg_data (&parts[0]), "gw-1", 4);
        send_gateway_with_timeout (gateway1, service_name, parts, 1,
                                   timeout_ms);
    }
    TEST_ASSERT_TRUE (recv_payload (provider, "gw-1", 4, timeout_ms));

    // 5. Destroy gateway1 + discovery1 (simulate reconnection)
    step_log ("destroy gateway1 + discovery1");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&gateway1));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery1));
    msleep (300);

    // 6. Discovery2 + Gateway2 with same routing id "GW-HO"
    step_log ("create discovery2 + gateway2");
    void *discovery2 = zlink_discovery_new_typed (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (discovery2);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_connect_registry (discovery2, "inproc://reg-pub-ho2"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_subscribe (discovery2, service_name));
    msleep (200);

    void *gateway2 = zlink_gateway_new (ctx, discovery2, gw_rid);
    TEST_ASSERT_NOT_NULL (gateway2);
    wait_gateway_ready (gateway2, service_name, timeout_ms);
    msleep (200);

    // 7. Send message via gateway2 -> provider receives
    step_log ("send msg2 via gateway2");
    {
        zlink_msg_t parts[1];
        zlink_msg_init_size (&parts[0], 4);
        memcpy (zlink_msg_data (&parts[0]), "gw-2", 4);
        send_gateway_with_timeout (gateway2, service_name, parts, 1,
                                   timeout_ms);
    }
    TEST_ASSERT_TRUE (recv_payload (provider, "gw-2", 4, timeout_ms));

    step_log ("cleanup");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&gateway2));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_receiver_destroy (&provider));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery2));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
}

int main (void)
{
    UNITY_BEGIN ();
    RUN_TEST (test_gateway_handover_provider_restart);
    RUN_TEST (test_provider_handover_gateway_reconnect);
    return UNITY_END ();
}
