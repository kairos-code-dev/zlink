/* SPDX-License-Identifier: MPL-2.0 */

#include "../testutil_unity.hpp"
#include "../testutil.hpp"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

SETUP_TEARDOWN_TESTCONTEXT

namespace
{
struct gateway_server_t
{
    gateway_server_t () : discovery (NULL), gateway (NULL) {}

    void *discovery;
    void *gateway;
};

bool test_debug_enabled ()
{
    return getenv ("ZLINK_TEST_DEBUG") != NULL;
}

void step_log (const char *msg_)
{
    if (test_debug_enabled ()) {
        fprintf (stderr, "[test] %s\n", msg_ ? msg_ : "");
        fflush (stderr);
    }
}

void discard_gateway_message (zlink_gateway_msg_kind_t kind_,
                              const char *,
                              size_t,
                              const zlink_routing_id_t *,
                              zlink_msg_t *parts_,
                              size_t part_count_)
{
    (void) kind_;
    for (size_t i = 0; i < part_count_; ++i)
        zlink_msg_close (&parts_[i]);
}

void setup_registry (void *ctx_,
                     void **registry_out_,
                     const char *pub_ep_,
                     const char *router_ep_)
{
    void *registry = zlink_registry_new (ctx_);
    TEST_ASSERT_NOT_NULL (registry);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_registry_set_endpoints (registry, pub_ep_, router_ep_));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_start (registry));
    *registry_out_ = registry;
}

void register_gateway_with_timeout (void *gateway_,
                                    const char *service_name_,
                                    const char *endpoint_,
                                    uint32_t weight_,
                                    int timeout_ms_)
{
    const int step_ms = 10;
    const int attempts = timeout_ms_ / step_ms;
    for (int i = 0; i < attempts; ++i) {
        if (zlink_gateway_register (gateway_, service_name_, endpoint_, weight_)
            == 0)
            return;
        if (errno != EAGAIN)
            break;
        msleep (step_ms);
    }
    TEST_FAIL_MESSAGE ("gateway register timeout");
}

void update_gateway_weight_with_timeout (void *gateway_,
                                         const char *service_name_,
                                         uint32_t weight_,
                                         int timeout_ms_)
{
    const int step_ms = 10;
    const int attempts = timeout_ms_ / step_ms;
    for (int i = 0; i < attempts; ++i) {
        if (zlink_gateway_update_weight (gateway_, service_name_, weight_) == 0)
            return;
        if (errno != EAGAIN)
            break;
        msleep (step_ms);
    }
    TEST_FAIL_MESSAGE ("gateway update_weight timeout");
}

bool wait_for_provider_count (void *discovery_,
                              const char *service_name_,
                              int expected_count_,
                              int timeout_ms_)
{
    const int sleep_ms = 25;
    const int max_attempts = timeout_ms_ / sleep_ms;

    for (int i = 0; i < max_attempts; ++i) {
        const int count =
          zlink_discovery_receiver_count (discovery_, service_name_);
        if (count == expected_count_)
            return true;
        msleep (sleep_ms);
    }
    return zlink_discovery_receiver_count (discovery_, service_name_)
           == expected_count_;
}

void init_gateway_server (gateway_server_t *server_,
                          void *ctx_,
                          const char *registry_ep_,
                          const char *routing_id_,
                          const char *bind_ep_)
{
    server_->discovery =
      zlink_discovery_new_typed (ctx_, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (server_->discovery);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_connect_registry (
      server_->discovery, registry_ep_));
    server_->gateway = zlink_gateway_new (ctx_, server_->discovery, routing_id_,
                                          &discard_gateway_message);
    TEST_ASSERT_NOT_NULL (server_->gateway);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_bind (server_->gateway, bind_ep_));
}

void destroy_gateway_server (gateway_server_t *server_)
{
    if (server_->gateway)
        TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&server_->gateway));
    if (server_->discovery)
        TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&server_->discovery));
}
}

static void test_discovery_provider_registration ()
{
    step_log ("=== test_discovery_provider_registration ===");

    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *registry = NULL;
    setup_registry (ctx, &registry, "inproc://reg-pub-basic",
                    "inproc://reg-router-basic");
    msleep (50);

    void *discovery = zlink_discovery_new_typed (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_connect_registry (discovery, "inproc://reg-router-basic"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_subscribe (discovery, "test-svc"));

    gateway_server_t server;
    char bind_ep[64];
    snprintf (bind_ep, sizeof (bind_ep), "tcp://127.0.0.1:%d", test_port (5700));
    init_gateway_server (&server, ctx, "inproc://reg-router-basic", NULL,
                         bind_ep);

    char advertise_ep[256] = {0};
    size_t advertise_len = sizeof (advertise_ep);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_gateway_last_endpoint (server.gateway, advertise_ep, &advertise_len));
    register_gateway_with_timeout (server.gateway, "test-svc", advertise_ep, 1,
                                   3000);

    TEST_ASSERT_TRUE (wait_for_provider_count (discovery, "test-svc", 1, 1000));
    TEST_ASSERT_EQUAL_INT (1,
                           zlink_discovery_receiver_count (discovery, "test-svc"));

    zlink_receiver_info_t providers[4];
    size_t provider_count = 4;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_get_receivers (
      discovery, "test-svc", providers, &provider_count));
    TEST_ASSERT_EQUAL_INT (1, (int) provider_count);
    TEST_ASSERT_EQUAL_STRING ("test-svc", providers[0].service_name);
    TEST_ASSERT_EQUAL_STRING (advertise_ep, providers[0].endpoint);
    TEST_ASSERT_EQUAL_UINT32 (1, providers[0].weight);
    TEST_ASSERT_GREATER_THAN_UINT (0, providers[0].routing_id.size);

    destroy_gateway_server (&server);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
}

static void test_discovery_service_filtering ()
{
    step_log ("=== test_discovery_service_filtering ===");

    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *registry = NULL;
    setup_registry (ctx, &registry, "inproc://reg-pub-filter",
                    "inproc://reg-router-filter");
    msleep (50);

    void *discovery = zlink_discovery_new_typed (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_connect_registry (discovery, "inproc://reg-router-filter"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_subscribe (discovery, "svc-A"));

    gateway_server_t server_a;
    gateway_server_t server_b;
    char bind_ep_a[64];
    char bind_ep_b[64];
    snprintf (bind_ep_a, sizeof (bind_ep_a), "tcp://127.0.0.1:%d",
              test_port (5701));
    snprintf (bind_ep_b, sizeof (bind_ep_b), "tcp://127.0.0.1:%d",
              test_port (5702));
    init_gateway_server (&server_a, ctx, "inproc://reg-router-filter", NULL,
                         bind_ep_a);
    init_gateway_server (&server_b, ctx, "inproc://reg-router-filter", NULL,
                         bind_ep_b);

    char advertise_a[256] = {0};
    char advertise_b[256] = {0};
    size_t advertise_len = sizeof (advertise_a);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_gateway_last_endpoint (server_a.gateway, advertise_a, &advertise_len));
    advertise_len = sizeof (advertise_b);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_gateway_last_endpoint (server_b.gateway, advertise_b, &advertise_len));

    register_gateway_with_timeout (server_a.gateway, "svc-A", advertise_a, 10,
                                   3000);
    register_gateway_with_timeout (server_b.gateway, "svc-B", advertise_b, 20,
                                   3000);

    TEST_ASSERT_TRUE (wait_for_provider_count (discovery, "svc-A", 1, 1000));

    zlink_receiver_info_t providers[4];
    size_t count = 4;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_get_receivers (discovery, "svc-A", providers, &count));
    TEST_ASSERT_EQUAL_INT (1, (int) count);
    TEST_ASSERT_EQUAL_STRING ("svc-A", providers[0].service_name);
    TEST_ASSERT_EQUAL_STRING (advertise_a, providers[0].endpoint);
    TEST_ASSERT_EQUAL_UINT32 (10, providers[0].weight);
    TEST_ASSERT_EQUAL_INT (0,
                           zlink_discovery_receiver_count (discovery, "svc-B"));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_subscribe (discovery, "svc-B"));
    TEST_ASSERT_TRUE (wait_for_provider_count (discovery, "svc-B", 1, 1000));

    count = 4;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_get_receivers (discovery, "svc-B", providers, &count));
    TEST_ASSERT_EQUAL_INT (1, (int) count);
    TEST_ASSERT_EQUAL_STRING ("svc-B", providers[0].service_name);
    TEST_ASSERT_EQUAL_STRING (advertise_b, providers[0].endpoint);
    TEST_ASSERT_EQUAL_UINT32 (20, providers[0].weight);

    destroy_gateway_server (&server_b);
    destroy_gateway_server (&server_a);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
}

static void test_discovery_heartbeat_timeout ()
{
    step_log ("=== test_discovery_heartbeat_timeout ===");

    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *registry = zlink_registry_new (ctx);
    TEST_ASSERT_NOT_NULL (registry);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_registry_set_endpoints (registry, "inproc://reg-pub-hb",
                                    "inproc://reg-router-hb"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_registry_set_heartbeat (registry, 50, 200));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_start (registry));
    msleep (50);

    void *discovery = zlink_discovery_new_typed (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_connect_registry (discovery, "inproc://reg-router-hb"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_subscribe (discovery, "hb-svc"));

    gateway_server_t server;
    char bind_ep[64];
    snprintf (bind_ep, sizeof (bind_ep), "tcp://127.0.0.1:%d", test_port (5703));
    init_gateway_server (&server, ctx, "inproc://reg-router-hb", NULL, bind_ep);

    char advertise_ep[256] = {0};
    size_t advertise_len = sizeof (advertise_ep);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_gateway_last_endpoint (server.gateway, advertise_ep, &advertise_len));
    register_gateway_with_timeout (server.gateway, "hb-svc", advertise_ep, 1,
                                   3000);

    TEST_ASSERT_TRUE (wait_for_provider_count (discovery, "hb-svc", 1, 1000));
    destroy_gateway_server (&server);
    msleep (350);
    TEST_ASSERT_TRUE (wait_for_provider_count (discovery, "hb-svc", 0, 500));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
}

static void test_discovery_weight_update ()
{
    step_log ("=== test_discovery_weight_update ===");

    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *registry = NULL;
    setup_registry (ctx, &registry, "inproc://reg-pub-weight",
                    "inproc://reg-router-weight");
    msleep (50);

    void *discovery = zlink_discovery_new_typed (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_connect_registry (discovery, "inproc://reg-router-weight"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_subscribe (discovery, "weight-svc"));

    gateway_server_t server;
    char bind_ep[64];
    snprintf (bind_ep, sizeof (bind_ep), "tcp://127.0.0.1:%d", test_port (5704));
    init_gateway_server (&server, ctx, "inproc://reg-router-weight", NULL,
                         bind_ep);

    char advertise_ep[256] = {0};
    size_t advertise_len = sizeof (advertise_ep);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_gateway_last_endpoint (server.gateway, advertise_ep, &advertise_len));

    register_gateway_with_timeout (server.gateway, "weight-svc", advertise_ep,
                                   10, 3000);
    TEST_ASSERT_TRUE (wait_for_provider_count (discovery, "weight-svc", 1, 1000));

    zlink_receiver_info_t providers[4];
    size_t count = 4;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_get_receivers (
      discovery, "weight-svc", providers, &count));
    TEST_ASSERT_EQUAL_INT (1, (int) count);
    TEST_ASSERT_EQUAL_UINT32 (10, providers[0].weight);

    update_gateway_weight_with_timeout (server.gateway, "weight-svc", 50, 500);
    msleep (200);

    count = 4;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_get_receivers (
      discovery, "weight-svc", providers, &count));
    TEST_ASSERT_EQUAL_INT (1, (int) count);
    TEST_ASSERT_EQUAL_UINT32 (50, providers[0].weight);

    destroy_gateway_server (&server);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
}

static void test_discovery_weight_update_stress ()
{
    step_log ("=== test_discovery_weight_update_stress ===");

    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *registry = NULL;
    setup_registry (ctx, &registry, "inproc://reg-pub-weight-stress",
                    "inproc://reg-router-weight-stress");
    msleep (50);

    void *discovery = zlink_discovery_new_typed (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_connect_registry (
      discovery, "inproc://reg-router-weight-stress"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_subscribe (discovery, "weight-stress-svc"));

    gateway_server_t server;
    char bind_ep[64];
    snprintf (bind_ep, sizeof (bind_ep), "tcp://127.0.0.1:%d", test_port (5705));
    init_gateway_server (&server, ctx, "inproc://reg-router-weight-stress",
                         NULL, bind_ep);

    char advertise_ep[256] = {0};
    size_t advertise_len = sizeof (advertise_ep);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_gateway_last_endpoint (server.gateway, advertise_ep, &advertise_len));
    register_gateway_with_timeout (server.gateway, "weight-stress-svc",
                                   advertise_ep, 10, 3000);
    TEST_ASSERT_TRUE (
      wait_for_provider_count (discovery, "weight-stress-svc", 1, 2000));

    uint32_t expected_weight = 10;
    for (int i = 0; i < 16; ++i) {
        expected_weight = (i % 2) == 0 ? 7 : 53;
        update_gateway_weight_with_timeout (server.gateway, "weight-stress-svc",
                                            expected_weight, 100);
    }
    msleep (200);

    zlink_receiver_info_t providers[4];
    size_t count = 4;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_get_receivers (
      discovery, "weight-stress-svc", providers, &count));
    TEST_ASSERT_EQUAL_INT (1, (int) count);
    TEST_ASSERT_EQUAL_UINT32 (expected_weight, providers[0].weight);

    destroy_gateway_server (&server);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
}

int main (void)
{
    setup_test_environment ();

    UNITY_BEGIN ();
    RUN_TEST (test_discovery_provider_registration);
    RUN_TEST (test_discovery_service_filtering);
    RUN_TEST (test_discovery_heartbeat_timeout);
    return UNITY_END ();
}
