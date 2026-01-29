/* SPDX-License-Identifier: MPL-2.0 */

#include "../testutil_unity.hpp"
#include "../testutil.hpp"

#include <string.h>

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

static void setup_provider (void *ctx,
                            void **provider_out,
                            const char *bind_ep,
                            const char *registry_router,
                            const char *service_name)
{
    void *provider = zlink_provider_new (ctx);
    TEST_ASSERT_NOT_NULL (provider);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_provider_bind (provider, bind_ep));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_provider_connect_registry (provider, registry_router));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_provider_register (provider, service_name, bind_ep, 1));
    *provider_out = provider;
}

static void test_discovery_get_providers ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *registry = NULL;
    setup_registry (ctx, &registry, "inproc://reg-pub", "inproc://reg-router");
    void *discovery = zlink_discovery_new (ctx);
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_connect_registry (discovery, "inproc://reg-pub"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_subscribe (discovery, "svc"));
    void *provider = NULL;
    setup_provider (ctx, &provider, "inproc://svc1", "inproc://reg-router",
                    "svc");
    msleep (200);
    zlink_provider_info_t providers[4];
    size_t count = 4;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_get_providers (discovery, "svc", providers, &count));
    TEST_ASSERT_EQUAL_INT (1, (int) count);
    TEST_ASSERT_EQUAL_STRING ("svc", providers[0].service_name);
    TEST_ASSERT_EQUAL_STRING ("inproc://svc1", providers[0].endpoint);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_provider_destroy (&provider));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

static void test_gateway_send_recv ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *registry = NULL;
    setup_registry (ctx, &registry, "inproc://reg-pub2", "inproc://reg-router2");
    void *discovery = zlink_discovery_new (ctx);
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_connect_registry (discovery, "inproc://reg-pub2"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_subscribe (discovery, "svc"));
    void *provider = NULL;
    setup_provider (ctx, &provider, "inproc://svc2", "inproc://reg-router2",
                    "svc");
    msleep (200);
    void *gateway = zlink_gateway_new (ctx, discovery);
    TEST_ASSERT_NOT_NULL (gateway);

    zlink_msg_t req;
    zlink_msg_init_size (&req, 5);
    memcpy (zlink_msg_data (&req), "hello", 5);

    uint64_t request_id = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_gateway_send (gateway, "svc", &req, 1, 0, &request_id));
    TEST_ASSERT_TRUE (request_id != 0);

    void *router = zlink_provider_threadsafe_router (provider);
    TEST_ASSERT_NOT_NULL (router);

    zlink_msg_t rid;
    zlink_msg_t reqid;
    zlink_msg_t payload;
    zlink_msg_init (&rid);
    zlink_msg_init (&reqid);
    zlink_msg_init (&payload);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_recv (&rid, router, 0));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_recv (&reqid, router, 0));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_recv (&payload, router, 0));

    TEST_ASSERT_EQUAL_INT (5, (int) zlink_msg_size (&payload));
    TEST_ASSERT_EQUAL_MEMORY ("hello", zlink_msg_data (&payload), 5);

    zlink_msg_t reply;
    zlink_msg_init_size (&reply, 5);
    memcpy (zlink_msg_data (&reply), "world", 5);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_send (&rid, router, ZLINK_SNDMORE));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_send (&reqid, router, ZLINK_SNDMORE));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_send (&reply, router, 0));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_close (&rid));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_close (&reqid));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_close (&payload));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_close (&reply));

    zlink_msg_t *reply_parts = NULL;
    size_t reply_count = 0;
    char service_name[256];
    uint64_t recv_id = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_gateway_recv (gateway, &reply_parts, &reply_count, 0, service_name,
                        &recv_id));
    TEST_ASSERT_EQUAL_UINT64 (request_id, recv_id);
    TEST_ASSERT_EQUAL_STRING ("svc", service_name);
    TEST_ASSERT_EQUAL_INT (1, (int) reply_count);
    TEST_ASSERT_EQUAL_INT (5, (int) zlink_msg_size (&reply_parts[0]));
    TEST_ASSERT_EQUAL_MEMORY ("world", zlink_msg_data (&reply_parts[0]), 5);
    zlink_msgv_close (reply_parts, reply_count);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&gateway));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_provider_destroy (&provider));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

static void test_registry_peer_sync ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *registry_a = NULL;
    void *registry_b = NULL;
    setup_registry (ctx, &registry_a, "inproc://regA-pub",
                    "inproc://regA-router");
    setup_registry (ctx, &registry_b, "inproc://regB-pub",
                    "inproc://regB-router");

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_registry_add_peer (registry_b, "inproc://regA-pub"));

    void *discovery = zlink_discovery_new (ctx);
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_connect_registry (discovery, "inproc://regB-pub"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_subscribe (discovery, "svc"));

    void *provider = NULL;
    setup_provider (ctx, &provider, "inproc://svc-peer",
                    "inproc://regA-router", "svc");

    msleep (300);

    zlink_provider_info_t providers[4];
    size_t count = 4;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_get_providers (discovery, "svc", providers, &count));
    TEST_ASSERT_EQUAL_INT (1, (int) count);
    TEST_ASSERT_EQUAL_STRING ("inproc://svc-peer", providers[0].endpoint);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_provider_destroy (&provider));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry_b));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry_a));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

static void test_gateway_refresh_on_unregister ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *registry = NULL;
    setup_registry (ctx, &registry, "inproc://reg-pub3", "inproc://reg-router3");
    void *discovery = zlink_discovery_new (ctx);
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_connect_registry (discovery, "inproc://reg-pub3"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_subscribe (discovery, "svc"));
    void *provider = NULL;
    setup_provider (ctx, &provider, "inproc://svc3", "inproc://reg-router3",
                    "svc");

    msleep (200);

    void *gateway = zlink_gateway_new (ctx, discovery);
    TEST_ASSERT_NOT_NULL (gateway);

    zlink_msg_t req;
    zlink_msg_init_size (&req, 1);
    memcpy (zlink_msg_data (&req), "x", 1);
    uint64_t request_id = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_gateway_send (gateway, "svc", &req, 1, 0, &request_id));

    msleep (100);
    TEST_ASSERT_EQUAL_INT (1, zlink_gateway_connection_count (gateway, "svc"));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_provider_unregister (provider, "svc"));
    msleep (300);
    TEST_ASSERT_EQUAL_INT (0, zlink_gateway_connection_count (gateway, "svc"));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&gateway));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_provider_destroy (&provider));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

static void test_registry_peer_timeout ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *registry_a = NULL;
    void *registry_b = NULL;
    setup_registry (ctx, &registry_a, "inproc://regA2-pub",
                    "inproc://regA2-router");
    setup_registry (ctx, &registry_b, "inproc://regB2-pub",
                    "inproc://regB2-router");
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_registry_set_broadcast_interval (registry_b, 50));

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_registry_add_peer (registry_b, "inproc://regA2-pub"));

    void *discovery = zlink_discovery_new (ctx);
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_connect_registry (discovery, "inproc://regB2-pub"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_subscribe (discovery, "svc"));

    void *provider = NULL;
    setup_provider (ctx, &provider, "inproc://svc-peer2",
                    "inproc://regA2-router", "svc");

    zlink_provider_info_t providers[4];
    size_t count = 0;
    bool found = false;
    for (int i = 0; i < 20; ++i) {
        count = 4;
        TEST_ASSERT_SUCCESS_ERRNO (
          zlink_discovery_get_providers (discovery, "svc", providers, &count));
        if (count == 1) {
            found = true;
            break;
        }
        msleep (50);
    }
    TEST_ASSERT_TRUE (found);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_provider_destroy (&provider));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry_a));

    bool removed = false;
    for (int i = 0; i < 30; ++i) {
        count = 4;
        TEST_ASSERT_SUCCESS_ERRNO (
          zlink_discovery_get_providers (discovery, "svc", providers, &count));
        if (count == 0) {
            removed = true;
            break;
        }
        msleep (50);
    }
    TEST_ASSERT_TRUE (removed);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry_b));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

int main (int, char **)
{
    setup_test_environment ();

    UNITY_BEGIN ();
    RUN_TEST (test_discovery_get_providers);
    RUN_TEST (test_gateway_send_recv);
    RUN_TEST (test_registry_peer_sync);
    RUN_TEST (test_gateway_refresh_on_unregister);
    RUN_TEST (test_registry_peer_timeout);
    return UNITY_END ();
}
