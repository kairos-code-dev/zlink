/* SPDX-License-Identifier: MPL-2.0 */

#include "../testutil_unity.hpp"
#include "../testutil.hpp"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

SETUP_TEARDOWN_TESTCONTEXT

namespace
{
bool should_run_named_test (const char *name_)
{
    const char *selected = getenv ("ZLINK_TEST_CASE");
    return !selected || !*selected || strcmp (selected, name_) == 0;
}

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

void discard_gateway_message (const zlink_routing_id_t *,
                              zlink_msg_t *parts_,
                              size_t part_count_)
{
    for (size_t i = 0; i < part_count_; ++i)
        zlink_msg_close (&parts_[i]);
}

void *create_gateway_attached (void *ctx_,
                               void *discovery_,
                               const char *service_name_,
                               const char *routing_id_,
                               zlink_socket_msg_handler_fn handler_)
{
    void *gateway =
      zlink_gateway_new (ctx_, service_name_, routing_id_, handler_);
    if (!gateway)
        return NULL;
    if (zlink_gateway_attach_discovery (gateway, discovery_) != 0) {
        const int err = errno;
        zlink_gateway_destroy (&gateway);
        errno = err;
        return NULL;
    }
    return gateway;
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

void make_registry_endpoint (char *endpoint_out_,
                             size_t endpoint_size_,
                             int port_seed_)
{
    snprintf (endpoint_out_, endpoint_size_, "tcp://127.0.0.1:%d",
              test_port (port_seed_));
}

void *create_started_registry_with_port_seed (void *ctx_,
                                               int *port_seed_,
                                               char *pub_ep_out_,
                                               size_t pub_size_,
                                               char *router_ep_out_,
                                               size_t router_size_)
{
    for (int attempt = 0; attempt < 32; ++attempt) {
        void *registry = zlink_registry_new (ctx_);
        if (!registry)
            return NULL;
        snprintf (pub_ep_out_, pub_size_, "tcp://127.0.0.1:%d",
                  test_port (*port_seed_));
        snprintf (router_ep_out_, router_size_, "tcp://127.0.0.1:%d",
                  test_port (*port_seed_ + 1));
        if (zlink_registry_set_endpoints (registry, pub_ep_out_,
                                          router_ep_out_) == 0
            && zlink_registry_start (registry) == 0) {
            *port_seed_ += 2;
            return registry;
        }
        zlink_registry_destroy (&registry);
        *port_seed_ += 2;
    }
    errno = EADDRINUSE;
    return NULL;
}

void bind_gateway_with_port_seed (void *gateway_,
                                   int *port_seed_,
                                   char *endpoint_out_,
                                   size_t endpoint_size_,
                                   int timeout_ms_)
{
    for (int port_attempt = 0; port_attempt < 32; ++port_attempt) {
        snprintf (endpoint_out_, endpoint_size_, "tcp://127.0.0.1:%d",
                  test_port (*port_seed_));
        const int step_ms = 10;
        const int attempts = timeout_ms_ / step_ms;
        for (int i = 0; i < attempts; ++i) {
            if (zlink_gateway_bind (gateway_, endpoint_out_) == 0) {
                *port_seed_ += 1;
                return;
            }
            if (errno == EADDRINUSE)
                break;
            if (errno != EAGAIN && errno != ENOENT)
                break;
            msleep (step_ms);
        }
        if (errno != EADDRINUSE)
            break;
        *port_seed_ += 1;
    }
    TEST_FAIL_MESSAGE ("gateway bind with port seed timeout");
}

void update_gateway_weight_with_timeout (void *gateway_,
                                         uint32_t weight_,
                                         int timeout_ms_)
{
    zlink_routing_id_t rid;
    memset (&rid, 0, sizeof (rid));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_routing_id (gateway_, &rid));

    const int step_ms = 10;
    const int attempts = timeout_ms_ / step_ms;
    for (int i = 0; i < attempts; ++i) {
        if (zlink_gateway_update_peer_weight (gateway_, &rid, weight_) == 0)
            return;
        if (errno != EAGAIN && errno != ENOENT)
            break;
        msleep (step_ms);
    }
    TEST_FAIL_MESSAGE ("gateway update_peer_weight timeout");
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
                          const char *service_name_,
                          const char *routing_id_,
                          int *bind_seed_,
                          char *endpoint_out_,
                          size_t endpoint_size_)
{
    step_log ("gateway_server: create discovery");
    server_->discovery =
      zlink_discovery_new (ctx_, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (server_->discovery);
    step_log ("gateway_server: connect registry");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_connect_registry (
      server_->discovery, registry_ep_));
    step_log ("gateway_server: create gateway");
    server_->gateway =
      create_gateway_attached (ctx_, server_->discovery, service_name_, routing_id_,
                         &discard_gateway_message);
    TEST_ASSERT_NOT_NULL (server_->gateway);
    step_log ("gateway_server: bind gateway");
    bind_gateway_with_port_seed (server_->gateway, bind_seed_, endpoint_out_,
                                  endpoint_size_, 3000);
}

void destroy_gateway_server (gateway_server_t *server_)
{
    if (server_->gateway) {
        step_log ("gateway_server: destroy gateway");
        TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&server_->gateway));
    }
    if (server_->discovery) {
        step_log ("gateway_server: destroy discovery");
        TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&server_->discovery));
    }
}
}

static void test_discovery_provider_registration ()
{
    step_log ("=== test_discovery_provider_registration ===");

    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);

    char registry_pub[MAX_SOCKET_STRING];
    char registry_router[MAX_SOCKET_STRING];
    int registry_seed = 25700;
    void *registry = create_started_registry_with_port_seed (
      ctx, &registry_seed, registry_pub, sizeof (registry_pub),
      registry_router, sizeof (registry_router));
    TEST_ASSERT_NOT_NULL (registry);
    msleep (50);

    void *discovery = zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_connect_registry (discovery, registry_router));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_subscribe (discovery, "test-svc"));

    gateway_server_t server;
    char bind_ep[64];
    int bind_seed = 5700;
    init_gateway_server (&server, ctx, registry_router, "test-svc", NULL,
                         &bind_seed, bind_ep, sizeof (bind_ep));

    char advertise_ep[256] = {0};
    size_t advertise_len = sizeof (advertise_ep);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_gateway_last_endpoint (server.gateway, advertise_ep, &advertise_len));

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
    TEST_ASSERT_EQUAL_UINT32 (0, providers[0].weight);
    TEST_ASSERT_GREATER_THAN_UINT (0, providers[0].routing_id.size);

    destroy_gateway_server (&server);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
}

static void test_discovery_service_filtering ()
{
    step_log ("=== test_discovery_service_filtering ===");

    step_log ("service_filtering: get ctx");
    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);

    step_log ("service_filtering: setup registry");
    char registry_pub[MAX_SOCKET_STRING];
    char registry_router[MAX_SOCKET_STRING];
    int registry_seed = 25710;
    void *registry = create_started_registry_with_port_seed (
      ctx, &registry_seed, registry_pub, sizeof (registry_pub),
      registry_router, sizeof (registry_router));
    TEST_ASSERT_NOT_NULL (registry);
    msleep (50);

    step_log ("service_filtering: create discovery");
    void *discovery = zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (discovery);
    step_log ("service_filtering: connect/subscribe svc-A");
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_connect_registry (discovery, registry_router));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_subscribe (discovery, "svc-A"));

    gateway_server_t server_a;
    gateway_server_t server_b;
    char bind_ep_a[64];
    char bind_ep_b[64];
    int bind_seed_a = 5701;
    int bind_seed_b = 5702;
    step_log ("service_filtering: init server A");
    init_gateway_server (&server_a, ctx, registry_router, "svc-A",
                         NULL, &bind_seed_a, bind_ep_a, sizeof (bind_ep_a));
    step_log ("service_filtering: init server B");
    init_gateway_server (&server_b, ctx, registry_router, "svc-B",
                         NULL, &bind_seed_b, bind_ep_b, sizeof (bind_ep_b));

    char advertise_a[256] = {0};
    char advertise_b[256] = {0};
    size_t advertise_len = sizeof (advertise_a);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_gateway_last_endpoint (server_a.gateway, advertise_a, &advertise_len));
    advertise_len = sizeof (advertise_b);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_gateway_last_endpoint (server_b.gateway, advertise_b, &advertise_len));

    step_log ("service_filtering: update weights");
    update_gateway_weight_with_timeout (server_a.gateway, 10, 3000);
    update_gateway_weight_with_timeout (server_b.gateway, 20, 3000);

    step_log ("service_filtering: wait svc-A");
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

    step_log ("service_filtering: subscribe svc-B");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_subscribe (discovery, "svc-B"));
    step_log ("service_filtering: wait svc-B");
    TEST_ASSERT_TRUE (wait_for_provider_count (discovery, "svc-B", 1, 1000));

    count = 4;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_get_receivers (discovery, "svc-B", providers, &count));
    TEST_ASSERT_EQUAL_INT (1, (int) count);
    TEST_ASSERT_EQUAL_STRING ("svc-B", providers[0].service_name);
    TEST_ASSERT_EQUAL_STRING (advertise_b, providers[0].endpoint);
    TEST_ASSERT_EQUAL_UINT32 (20, providers[0].weight);

    step_log ("service_filtering: destroy server B");
    destroy_gateway_server (&server_b);
    step_log ("service_filtering: destroy server A");
    destroy_gateway_server (&server_a);
    step_log ("service_filtering: destroy discovery");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    step_log ("service_filtering: destroy registry");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
}

static void test_discovery_heartbeat_timeout ()
{
    step_log ("=== test_discovery_heartbeat_timeout ===");

    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);

    char registry_pub[MAX_SOCKET_STRING];
    char registry_router[MAX_SOCKET_STRING];
    int registry_seed = 25720;
    void *registry = NULL;
    for (int attempt = 0; attempt < 32; ++attempt) {
        registry = zlink_registry_new (ctx);
        TEST_ASSERT_NOT_NULL (registry);
        make_registry_endpoint (registry_pub, sizeof (registry_pub),
                                registry_seed);
        make_registry_endpoint (registry_router, sizeof (registry_router),
                                registry_seed + 1);
        if (zlink_registry_set_endpoints (registry, registry_pub,
                                          registry_router) == 0
            && zlink_registry_set_heartbeat (registry, 50, 200) == 0
            && zlink_registry_start (registry) == 0) {
            registry_seed += 2;
            break;
        }
        zlink_registry_destroy (&registry);
        registry = NULL;
        registry_seed += 2;
    }
    TEST_ASSERT_NOT_NULL (registry);
    msleep (50);

    void *discovery = zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_connect_registry (discovery, registry_router));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_subscribe (discovery, "hb-svc"));

    gateway_server_t server;
    char bind_ep[64];
    int bind_seed = 5703;
    init_gateway_server (&server, ctx, registry_router, "hb-svc",
                         NULL, &bind_seed, bind_ep, sizeof (bind_ep));

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

    char registry_pub[MAX_SOCKET_STRING];
    char registry_router[MAX_SOCKET_STRING];
    int registry_seed = 25730;
    void *registry = create_started_registry_with_port_seed (
      ctx, &registry_seed, registry_pub, sizeof (registry_pub),
      registry_router, sizeof (registry_router));
    TEST_ASSERT_NOT_NULL (registry);
    msleep (50);

    void *discovery = zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_connect_registry (discovery, registry_router));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_subscribe (discovery, "weight-svc"));

    gateway_server_t server;
    char bind_ep[64];
    int bind_seed = 5704;
    init_gateway_server (&server, ctx, registry_router,
                         "weight-svc", NULL, &bind_seed, bind_ep,
                         sizeof (bind_ep));

    char advertise_ep[256] = {0};
    size_t advertise_len = sizeof (advertise_ep);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_gateway_last_endpoint (server.gateway, advertise_ep, &advertise_len));

    update_gateway_weight_with_timeout (server.gateway, 10, 3000);
    TEST_ASSERT_TRUE (wait_for_provider_count (discovery, "weight-svc", 1, 1000));

    zlink_receiver_info_t providers[4];
    size_t count = 4;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_get_receivers (
      discovery, "weight-svc", providers, &count));
    TEST_ASSERT_EQUAL_INT (1, (int) count);
    TEST_ASSERT_EQUAL_UINT32 (10, providers[0].weight);

    update_gateway_weight_with_timeout (server.gateway, 50, 500);
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

    char registry_pub[MAX_SOCKET_STRING];
    char registry_router[MAX_SOCKET_STRING];
    int registry_seed = 25740;
    void *registry = create_started_registry_with_port_seed (
      ctx, &registry_seed, registry_pub, sizeof (registry_pub),
      registry_router, sizeof (registry_router));
    TEST_ASSERT_NOT_NULL (registry);
    msleep (50);

    void *discovery = zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_connect_registry (
      discovery, registry_router));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_subscribe (discovery, "weight-stress-svc"));

    gateway_server_t server;
    char bind_ep[64];
    int bind_seed = 5705;
    init_gateway_server (&server, ctx, registry_router,
                         "weight-stress-svc", NULL, &bind_seed, bind_ep,
                         sizeof (bind_ep));

    char advertise_ep[256] = {0};
    size_t advertise_len = sizeof (advertise_ep);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_gateway_last_endpoint (server.gateway, advertise_ep, &advertise_len));
    update_gateway_weight_with_timeout (server.gateway, 10, 3000);
    TEST_ASSERT_TRUE (
      wait_for_provider_count (discovery, "weight-stress-svc", 1, 2000));

    uint32_t expected_weight = 10;
    for (int i = 0; i < 16; ++i) {
        expected_weight = (i % 2) == 0 ? 7 : 53;
        update_gateway_weight_with_timeout (server.gateway, expected_weight,
                                            100);
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
#define RUN_TEST_CASE(name)                                                    \
    do {                                                                       \
        if (should_run_named_test (#name))                                     \
            RUN_TEST (name);                                                   \
    } while (0)
    RUN_TEST_CASE (test_discovery_provider_registration);
    RUN_TEST_CASE (test_discovery_service_filtering);
    RUN_TEST_CASE (test_discovery_heartbeat_timeout);
#undef RUN_TEST_CASE
    return UNITY_END ();
}
