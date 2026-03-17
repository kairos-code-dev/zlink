/* SPDX-License-Identifier: MPL-2.0 */

#include "../../testutil_unity.hpp"
#include "../../testutil.hpp"

#include "services/discovery/discovery.hpp"

#include <chrono>
#include <errno.h>
#include <stdlib.h>
#include <string>
#include <string.h>
#include <vector>

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
                              size_t part_count_,
                          void *)
{
    for (size_t i = 0; i < part_count_; ++i)
        zlink_msg_close (&parts_[i]);
}

bool wait_for_registry_uplink (void *discovery_, int timeout_ms_)
{
    zlink::discovery_t *discovery =
      static_cast<zlink::discovery_t *> (discovery_);
    if (!discovery)
        return false;

    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::milliseconds (timeout_ms_);
    while (std::chrono::steady_clock::now () < deadline) {
        std::string uplink;
        if (discovery->latest_registry_uplink (&uplink) && !uplink.empty ())
            return true;
        msleep (10);
    }
    return false;
}

void *create_gateway_attached (void *ctx_,
                               void *discovery_,
                               const char *service_name_,
                               const char *routing_id_,
                               zlink_socket_msg_handler_fn handler_)
{
    void *gateway =
      zlink_gateway_new (ctx_, service_name_, routing_id_, handler_, NULL);
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
      zlink_registry_bind (registry, pub_ep_, router_ep_));
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
        if (zlink_registry_bind (registry, pub_ep_out_, router_ep_out_) == 0) {
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

bool query_ready_gateway_entries (
  void *registry_,
  const char *service_name_,
  std::vector<zlink_registry_topology_entry_t> *entries_out_)
{
    if (!registry_ || !service_name_ || !entries_out_)
        return false;

    zlink_registry_topology_filter_t filter;
    memset (&filter, 0, sizeof (filter));
    filter.service_kind = ZLINK_SERVICE_KIND_GATEWAY;
    filter.state = ZLINK_TOPOLOGY_STATE_READY;
    strncpy (filter.service_name, service_name_,
             sizeof (filter.service_name) - 1);

    size_t count = 0;
    if (zlink_registry_topology_query (registry_, &filter, NULL, &count) != 0)
        return false;

    entries_out_->clear ();
    if (count == 0)
        return true;

    entries_out_->resize (count);
    return zlink_registry_topology_query (registry_, &filter, &(*entries_out_)[0],
                                          &count)
           == 0;
}

bool wait_for_provider_count (void *registry_,
                              const char *service_name_,
                              int expected_count_,
                              int timeout_ms_)
{
    const int sleep_ms = 25;
    const int max_attempts = timeout_ms_ / sleep_ms;

    for (int i = 0; i < max_attempts; ++i) {
        std::vector<zlink_registry_topology_entry_t> entries;
        if (!query_ready_gateway_entries (registry_, service_name_, &entries))
            return false;
        if (entries.size () == static_cast<size_t> (expected_count_))
            return true;
        msleep (sleep_ms);
    }

    std::vector<zlink_registry_topology_entry_t> entries;
    return query_ready_gateway_entries (registry_, service_name_, &entries)
           && entries.size () == static_cast<size_t> (expected_count_);
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
    TEST_ASSERT_TRUE (wait_for_registry_uplink (server_->discovery, 2000));
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
    const int seed_jitter = rand () % 1000;
    int registry_seed = 25700 + seed_jitter;
    void *registry = create_started_registry_with_port_seed (
      ctx, &registry_seed, registry_pub, sizeof (registry_pub),
      registry_router, sizeof (registry_router));
    TEST_ASSERT_NOT_NULL (registry);
    msleep (50);

    void *discovery = zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_connect_registry (discovery, registry_router));
    gateway_server_t server;
    char bind_ep[64];
    int bind_seed = 5700 + seed_jitter;
    init_gateway_server (&server, ctx, registry_router, "test-svc", NULL,
                         &bind_seed, bind_ep, sizeof (bind_ep));

    char advertise_ep[256] = {0};
    size_t advertise_len = sizeof (advertise_ep);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_gateway_last_endpoint (server.gateway, advertise_ep, &advertise_len));

    TEST_ASSERT_TRUE (wait_for_provider_count (registry, "test-svc", 1, 3000));

    std::vector<zlink_registry_topology_entry_t> entries;
    TEST_ASSERT_TRUE (
      query_ready_gateway_entries (registry, "test-svc", &entries));
    TEST_ASSERT_EQUAL_INT (1, (int) entries.size ());
    TEST_ASSERT_EQUAL_STRING ("test-svc", entries[0].service_name);
    TEST_ASSERT_EQUAL_STRING (advertise_ep, entries[0].endpoint);
    TEST_ASSERT_GREATER_THAN_UINT (0, entries[0].routing_id.size);

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
    const int seed_jitter = rand () % 1000;
    int registry_seed = 25710 + seed_jitter;
    void *registry = create_started_registry_with_port_seed (
      ctx, &registry_seed, registry_pub, sizeof (registry_pub),
      registry_router, sizeof (registry_router));
    TEST_ASSERT_NOT_NULL (registry);
    msleep (50);

    step_log ("service_filtering: create discovery");
    void *discovery = zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (discovery);
    step_log ("service_filtering: connect discovery");
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_connect_registry (discovery, registry_router));

    gateway_server_t server_a;
    gateway_server_t server_b;
    char bind_ep_a[64];
    char bind_ep_b[64];
    int bind_seed_a = 5701 + seed_jitter;
    int bind_seed_b = 5702 + seed_jitter;
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

    step_log ("service_filtering: wait svc-A");
    TEST_ASSERT_TRUE (wait_for_provider_count (registry, "svc-A", 1, 3000));

    std::vector<zlink_registry_topology_entry_t> entries;
    TEST_ASSERT_TRUE (query_ready_gateway_entries (registry, "svc-A", &entries));
    TEST_ASSERT_EQUAL_INT (1, (int) entries.size ());
    TEST_ASSERT_EQUAL_STRING ("svc-A", entries[0].service_name);
    TEST_ASSERT_EQUAL_STRING (advertise_a, entries[0].endpoint);

    step_log ("service_filtering: wait svc-B");
    TEST_ASSERT_TRUE (wait_for_provider_count (registry, "svc-B", 1, 3000));

    TEST_ASSERT_TRUE (query_ready_gateway_entries (registry, "svc-B", &entries));
    TEST_ASSERT_EQUAL_INT (1, (int) entries.size ());
    TEST_ASSERT_EQUAL_STRING ("svc-B", entries[0].service_name);
    TEST_ASSERT_EQUAL_STRING (advertise_b, entries[0].endpoint);

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
    const int seed_jitter = rand () % 1000;
    int registry_seed = 25720 + seed_jitter;
    void *registry = NULL;
    for (int attempt = 0; attempt < 32; ++attempt) {
        registry = zlink_registry_new (ctx);
        TEST_ASSERT_NOT_NULL (registry);
        make_registry_endpoint (registry_pub, sizeof (registry_pub),
                                registry_seed);
        make_registry_endpoint (registry_router, sizeof (registry_router),
                                registry_seed + 1);
        if (zlink_registry_set_heartbeat (registry, 50, 200) == 0
            && zlink_registry_bind (registry, registry_pub, registry_router)
                 == 0) {
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
    gateway_server_t server;
    char bind_ep[64];
    int bind_seed = 5703 + seed_jitter;
    init_gateway_server (&server, ctx, registry_router, "hb-svc",
                         NULL, &bind_seed, bind_ep, sizeof (bind_ep));

    TEST_ASSERT_TRUE (wait_for_provider_count (registry, "hb-svc", 1, 3000));
    destroy_gateway_server (&server);
    msleep (350);
    TEST_ASSERT_TRUE (wait_for_provider_count (registry, "hb-svc", 0, 2000));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
}

static void test_registry_bind_rejects_rebind ()
{
    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);

    char registry_pub[MAX_SOCKET_STRING];
    char registry_router[MAX_SOCKET_STRING];
    make_registry_endpoint (registry_pub, sizeof (registry_pub), 22640);
    make_registry_endpoint (registry_router, sizeof (registry_router), 22641);

    void *registry = zlink_registry_new (ctx);
    TEST_ASSERT_NOT_NULL (registry);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_registry_bind (registry, registry_pub, registry_router));
    TEST_ASSERT_EQUAL_INT (
      -1, zlink_registry_bind (registry, registry_pub, registry_router));
    TEST_ASSERT_EQUAL_INT (EBUSY, zlink_errno ());
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
}

int main (void)
{
    setup_test_environment (180);

    UNITY_BEGIN ();
#define RUN_TEST_CASE(name)                                                    \
    do {                                                                       \
        if (should_run_named_test (#name))                                     \
            RUN_TEST (name);                                                   \
    } while (0)
    RUN_TEST_CASE (test_discovery_provider_registration);
    RUN_TEST_CASE (test_discovery_service_filtering);
    RUN_TEST_CASE (test_discovery_heartbeat_timeout);
    RUN_TEST_CASE (test_registry_bind_rejects_rebind);
#undef RUN_TEST_CASE
    return UNITY_END ();
}
