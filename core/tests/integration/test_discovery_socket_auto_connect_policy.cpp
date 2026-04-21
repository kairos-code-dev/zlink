/* SPDX-License-Identifier: MPL-2.0 */

#include "../testutil.hpp"
#include "../testutil_unity.hpp"

#include <chrono>
#include <string.h>
#include <unity.h>

void setUp ()
{
}

void tearDown ()
{
}

namespace
{
bool bind_registry_test_endpoints_local (void *registry_,
                                         int base_port_,
                                         char *pub_out_,
                                         size_t pub_size_,
                                         char *router_out_,
                                         size_t router_size_)
{
    if (!registry_ || !pub_out_ || !router_out_ || pub_size_ == 0
        || router_size_ == 0) {
        errno = EINVAL;
        return false;
    }

    const int base = base_port_ + test_port_offset ();
    for (int i = 0; i < 128; ++i) {
        snprintf (pub_out_, pub_size_, "tcp://127.0.0.1:%d", base + i * 2);
        snprintf (router_out_, router_size_, "tcp://127.0.0.1:%d",
                  base + i * 2 + 1);
        if (zlink_registry_bind (registry_, pub_out_, router_out_)
            == ZLINK_BIND_OK) {
            return true;
        }
    }

    return false;
}

bool bind_socket_test_endpoint_local (void *socket_,
                                      int base_port_,
                                      char *endpoint_out_,
                                      size_t endpoint_size_)
{
    if (!socket_ || !endpoint_out_ || endpoint_size_ == 0) {
        errno = EINVAL;
        return false;
    }

    const int base = base_port_ + test_port_offset ();
    for (int i = 0; i < 128; ++i) {
        snprintf (endpoint_out_, endpoint_size_, "tcp://127.0.0.1:%d",
                  base + i);
        if (zlink_bind (socket_, endpoint_out_) == ZLINK_BIND_OK)
            return true;
    }

    return false;
}

bool connect_discovery_registry_with_retry_local (void *discovery_,
                                                  const char *endpoint_,
                                                  int timeout_ms_)
{
    const int attempts = timeout_ms_ / 25;
    for (int i = 0; i < attempts; ++i) {
        if (zlink_discovery_connect_registry (discovery_, endpoint_)
            == ZLINK_CONNECT_OK) {
            return true;
        }
        msleep (25);
    }
    return false;
}

bool destroy_discovery_with_retry_local (void **discovery_p_, int timeout_ms_)
{
    const int attempts = timeout_ms_ / 25;
    for (int i = 0; i < attempts; ++i) {
        if (zlink_discovery_destroy (discovery_p_) == ZLINK_CLOSE_OK)
            return true;
        if (zlink_errno () != EBUSY)
            return false;
        msleep (25);
    }
    return false;
}

bool wait_for_topology_entry_local (
  void *registry_,
  const zlink_registry_topology_filter_t *filter_,
  zlink_registry_topology_entry_t *entry_out_,
  uint32_t desired_count_,
  int timeout_ms_)
{
    const int attempts = timeout_ms_ / 25;
    for (int i = 0; i < attempts; ++i) {
        zlink_registry_topology_entry_t entries[8];
        size_t count = 8;
        if (zlink_registry_topology_query (registry_, filter_, entries, &count)
            == ZLINK_CONFIG_OK) {
            for (size_t j = 0; j < count; ++j) {
                if (entries[j].desired_count != desired_count_)
                    continue;
                if (entry_out_)
                    *entry_out_ = entries[j];
                return true;
            }
        }
        msleep (25);
    }
    return false;
}

void init_socket_topology_filter_local (
  zlink_registry_topology_filter_t *filter_,
  const char *service_name_,
  zlink_service_role_t service_role_,
  const zlink_routing_id_t *routing_id_)
{
    memset (filter_, 0, sizeof (*filter_));
    filter_->service_kind = ZLINK_SERVICE_KIND_SOCKET;
    filter_->service_role = service_role_;
    filter_->source = ZLINK_TOPOLOGY_SOURCE_DISCOVERY;
    strncpy (filter_->service_name, service_name_,
             sizeof (filter_->service_name) - 1);
    if (routing_id_)
        filter_->routing_id = *routing_id_;
}

void test_socket_discovery_default_dealer_mode_targets_router ()
{
    if (!zlink_has ("tcp")) {
        TEST_IGNORE_MESSAGE ("TCP not available");
        return;
    }

    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *registry = zlink_registry_new (ctx);
    TEST_ASSERT_NOT_NULL (registry);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_registry_set_broadcast_interval (registry, 50));

    char registry_pub[MAX_SOCKET_STRING];
    char registry_router[MAX_SOCKET_STRING];
    char router_endpoint[MAX_SOCKET_STRING];
    char dealer_endpoint[MAX_SOCKET_STRING];
    TEST_ASSERT_TRUE (bind_registry_test_endpoints_local (
      registry, 5960, registry_pub, sizeof (registry_pub), registry_router,
      sizeof (registry_router)));

    void *router_discovery =
      zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_SOCKET, "socket-auto-router");
    void *dealer_discovery =
      zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_SOCKET, "socket-auto-router");
    TEST_ASSERT_NOT_NULL (router_discovery);
    TEST_ASSERT_NOT_NULL (dealer_discovery);
    TEST_ASSERT_TRUE (connect_discovery_registry_with_retry_local (
      router_discovery, registry_router, 3000));
    TEST_ASSERT_TRUE (connect_discovery_registry_with_retry_local (
      dealer_discovery, registry_router, 3000));

    void *router = zlink_socket (ctx, ZLINK_SOCKET_ROUTER);
    void *dealer = zlink_socket (ctx, ZLINK_SOCKET_DEALER);
    TEST_ASSERT_NOT_NULL (router);
    TEST_ASSERT_NOT_NULL (dealer);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (router, "router-a", 8));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (dealer, "dealer-a", 8));

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_socket_attach_discovery (router, router_discovery));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_socket_attach_discovery (dealer, dealer_discovery));
    TEST_ASSERT_TRUE (bind_socket_test_endpoint_local (
      router, 5962, router_endpoint, sizeof (router_endpoint)));
    TEST_ASSERT_TRUE (bind_socket_test_endpoint_local (
      dealer, 5963, dealer_endpoint, sizeof (dealer_endpoint)));

    zlink_routing_id_t router_rid;
    zlink_routing_id_t dealer_rid;
    memset (&router_rid, 0, sizeof (router_rid));
    memset (&dealer_rid, 0, sizeof (dealer_rid));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_routing_id (router, &router_rid));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_routing_id (dealer, &dealer_rid));

    zlink_registry_topology_filter_t dealer_filter;
    init_socket_topology_filter_local (&dealer_filter, "socket-auto-router",
                                       ZLINK_SERVICE_ROLE_DEALER, &dealer_rid);
    zlink_registry_topology_entry_t dealer_entry;
    memset (&dealer_entry, 0, sizeof (dealer_entry));
    TEST_ASSERT_TRUE (wait_for_topology_entry_local (
      registry, &dealer_filter, &dealer_entry, 1, 20000));
    TEST_ASSERT_EQUAL_UINT32 (1u, dealer_entry.desired_count);

    zlink_registry_topology_filter_t router_filter;
    init_socket_topology_filter_local (&router_filter, "socket-auto-router",
                                       ZLINK_SERVICE_ROLE_ROUTER, &router_rid);
    zlink_registry_topology_entry_t router_entry;
    memset (&router_entry, 0, sizeof (router_entry));
    TEST_ASSERT_TRUE (wait_for_topology_entry_local (
      registry, &router_filter, &router_entry, 0, 20000));
    TEST_ASSERT_EQUAL_UINT32 (0u, router_entry.desired_count);

    TEST_ASSERT_TRUE (
      destroy_discovery_with_retry_local (&dealer_discovery, 3000));
    TEST_ASSERT_TRUE (
      destroy_discovery_with_retry_local (&router_discovery, 3000));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_socket_discovery_default_dealer_mode_ignores_dealer_peers ()
{
    if (!zlink_has ("tcp")) {
        TEST_IGNORE_MESSAGE ("TCP not available");
        return;
    }

    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *registry = zlink_registry_new (ctx);
    TEST_ASSERT_NOT_NULL (registry);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_registry_set_broadcast_interval (registry, 50));

    char registry_pub[MAX_SOCKET_STRING];
    char registry_router[MAX_SOCKET_STRING];
    char dealer_a_endpoint[MAX_SOCKET_STRING];
    char dealer_b_endpoint[MAX_SOCKET_STRING];
    TEST_ASSERT_TRUE (bind_registry_test_endpoints_local (
      registry, 5970, registry_pub, sizeof (registry_pub), registry_router,
      sizeof (registry_router)));

    void *discovery_a =
      zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_SOCKET, "socket-auto-default");
    void *discovery_b =
      zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_SOCKET, "socket-auto-default");
    TEST_ASSERT_NOT_NULL (discovery_a);
    TEST_ASSERT_NOT_NULL (discovery_b);
    TEST_ASSERT_TRUE (connect_discovery_registry_with_retry_local (
      discovery_a, registry_router, 3000));
    TEST_ASSERT_TRUE (connect_discovery_registry_with_retry_local (
      discovery_b, registry_router, 3000));

    void *dealer_a = zlink_socket (ctx, ZLINK_SOCKET_DEALER);
    void *dealer_b = zlink_socket (ctx, ZLINK_SOCKET_DEALER);
    TEST_ASSERT_NOT_NULL (dealer_a);
    TEST_ASSERT_NOT_NULL (dealer_b);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (dealer_a, "dealer-b0", 9));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (dealer_b, "dealer-b1", 9));

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_socket_attach_discovery (dealer_a, discovery_a));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_socket_attach_discovery (dealer_b, discovery_b));
    TEST_ASSERT_TRUE (bind_socket_test_endpoint_local (
      dealer_a, 5972, dealer_a_endpoint, sizeof (dealer_a_endpoint)));
    TEST_ASSERT_TRUE (bind_socket_test_endpoint_local (
      dealer_b, 5973, dealer_b_endpoint, sizeof (dealer_b_endpoint)));

    zlink_routing_id_t dealer_a_rid;
    zlink_routing_id_t dealer_b_rid;
    memset (&dealer_a_rid, 0, sizeof (dealer_a_rid));
    memset (&dealer_b_rid, 0, sizeof (dealer_b_rid));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_routing_id (dealer_a, &dealer_a_rid));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_routing_id (dealer_b, &dealer_b_rid));

    zlink_registry_topology_filter_t filter_a;
    init_socket_topology_filter_local (&filter_a, "socket-auto-default",
                                       ZLINK_SERVICE_ROLE_DEALER,
                                       &dealer_a_rid);
    zlink_registry_topology_entry_t entry_a;
    memset (&entry_a, 0, sizeof (entry_a));
    TEST_ASSERT_TRUE (wait_for_topology_entry_local (
      registry, &filter_a, &entry_a, 0, 5000));

    zlink_registry_topology_filter_t filter_b;
    init_socket_topology_filter_local (&filter_b, "socket-auto-default",
                                       ZLINK_SERVICE_ROLE_DEALER,
                                       &dealer_b_rid);
    zlink_registry_topology_entry_t entry_b;
    memset (&entry_b, 0, sizeof (entry_b));
    TEST_ASSERT_TRUE (wait_for_topology_entry_local (
      registry, &filter_b, &entry_b, 0, 5000));

    TEST_ASSERT_TRUE (destroy_discovery_with_retry_local (&discovery_b, 3000));
    TEST_ASSERT_TRUE (destroy_discovery_with_retry_local (&discovery_a, 3000));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_socket_discovery_explicit_dealer_mode_targets_dealer ()
{
    if (!zlink_has ("tcp")) {
        TEST_IGNORE_MESSAGE ("TCP not available");
        return;
    }

    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *registry = zlink_registry_new (ctx);
    TEST_ASSERT_NOT_NULL (registry);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_registry_set_broadcast_interval (registry, 50));

    char registry_pub[MAX_SOCKET_STRING];
    char registry_router[MAX_SOCKET_STRING];
    char dealer_a_endpoint[MAX_SOCKET_STRING];
    char dealer_b_endpoint[MAX_SOCKET_STRING];
    TEST_ASSERT_TRUE (bind_registry_test_endpoints_local (
      registry, 5980, registry_pub, sizeof (registry_pub), registry_router,
      sizeof (registry_router)));

    void *discovery_a =
      zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_SOCKET, "socket-auto-dealer");
    void *discovery_b =
      zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_SOCKET, "socket-auto-dealer");
    TEST_ASSERT_NOT_NULL (discovery_a);
    TEST_ASSERT_NOT_NULL (discovery_b);
    TEST_ASSERT_TRUE (connect_discovery_registry_with_retry_local (
      discovery_a, registry_router, 3000));
    TEST_ASSERT_TRUE (connect_discovery_registry_with_retry_local (
      discovery_b, registry_router, 3000));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_set_dealer_peer_mode (
        discovery_a, ZLINK_DISCOVERY_DEALER_PEER_MODE_DEALER));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_set_dealer_peer_mode (
        discovery_b, ZLINK_DISCOVERY_DEALER_PEER_MODE_DEALER));

    void *dealer_a = zlink_socket (ctx, ZLINK_SOCKET_DEALER);
    void *dealer_b = zlink_socket (ctx, ZLINK_SOCKET_DEALER);
    TEST_ASSERT_NOT_NULL (dealer_a);
    TEST_ASSERT_NOT_NULL (dealer_b);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (dealer_a, "dealer-c0", 9));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (dealer_b, "dealer-c1", 9));

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_socket_attach_discovery (dealer_a, discovery_a));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_socket_attach_discovery (dealer_b, discovery_b));
    TEST_ASSERT_TRUE (bind_socket_test_endpoint_local (
      dealer_a, 5982, dealer_a_endpoint, sizeof (dealer_a_endpoint)));
    TEST_ASSERT_TRUE (bind_socket_test_endpoint_local (
      dealer_b, 5983, dealer_b_endpoint, sizeof (dealer_b_endpoint)));

    zlink_routing_id_t dealer_a_rid;
    zlink_routing_id_t dealer_b_rid;
    memset (&dealer_a_rid, 0, sizeof (dealer_a_rid));
    memset (&dealer_b_rid, 0, sizeof (dealer_b_rid));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_routing_id (dealer_a, &dealer_a_rid));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_routing_id (dealer_b, &dealer_b_rid));

    zlink_registry_topology_filter_t filter_a;
    init_socket_topology_filter_local (&filter_a, "socket-auto-dealer",
                                       ZLINK_SERVICE_ROLE_DEALER,
                                       &dealer_a_rid);
    zlink_registry_topology_entry_t entry_a;
    memset (&entry_a, 0, sizeof (entry_a));
    TEST_ASSERT_TRUE (wait_for_topology_entry_local (
      registry, &filter_a, &entry_a, 1, 20000));

    zlink_registry_topology_filter_t filter_b;
    init_socket_topology_filter_local (&filter_b, "socket-auto-dealer",
                                       ZLINK_SERVICE_ROLE_DEALER,
                                       &dealer_b_rid);
    zlink_registry_topology_entry_t entry_b;
    memset (&entry_b, 0, sizeof (entry_b));
    TEST_ASSERT_TRUE (wait_for_topology_entry_local (
      registry, &filter_b, &entry_b, 1, 20000));

    TEST_ASSERT_TRUE (destroy_discovery_with_retry_local (&discovery_b, 3000));
    TEST_ASSERT_TRUE (destroy_discovery_with_retry_local (&discovery_a, 3000));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_socket_discovery_router_router_uses_single_initiator ()
{
    if (!zlink_has ("tcp")) {
        TEST_IGNORE_MESSAGE ("TCP not available");
        return;
    }

    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *registry = zlink_registry_new (ctx);
    TEST_ASSERT_NOT_NULL (registry);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_registry_set_broadcast_interval (registry, 50));

    char registry_pub[MAX_SOCKET_STRING];
    char registry_router[MAX_SOCKET_STRING];
    char router_a_endpoint[MAX_SOCKET_STRING];
    char router_b_endpoint[MAX_SOCKET_STRING];
    TEST_ASSERT_TRUE (bind_registry_test_endpoints_local (
      registry, 5984, registry_pub, sizeof (registry_pub), registry_router,
      sizeof (registry_router)));

    void *discovery_a = zlink_discovery_new (
      ctx, ZLINK_SERVICE_TYPE_SOCKET, "socket-auto-router-router");
    void *discovery_b = zlink_discovery_new (
      ctx, ZLINK_SERVICE_TYPE_SOCKET, "socket-auto-router-router");
    TEST_ASSERT_NOT_NULL (discovery_a);
    TEST_ASSERT_NOT_NULL (discovery_b);
    TEST_ASSERT_TRUE (connect_discovery_registry_with_retry_local (
      discovery_a, registry_router, 3000));
    TEST_ASSERT_TRUE (connect_discovery_registry_with_retry_local (
      discovery_b, registry_router, 3000));

    void *router_a = zlink_socket (ctx, ZLINK_SOCKET_ROUTER);
    void *router_b = zlink_socket (ctx, ZLINK_SOCKET_ROUTER);
    TEST_ASSERT_NOT_NULL (router_a);
    TEST_ASSERT_NOT_NULL (router_b);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (router_a, "router-aa", 9));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (router_b, "router-bb", 9));

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_socket_attach_discovery (router_a, discovery_a));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_socket_attach_discovery (router_b, discovery_b));
    TEST_ASSERT_TRUE (bind_socket_test_endpoint_local (
      router_a, 5986, router_a_endpoint, sizeof (router_a_endpoint)));
    TEST_ASSERT_TRUE (bind_socket_test_endpoint_local (
      router_b, 5987, router_b_endpoint, sizeof (router_b_endpoint)));

    zlink_routing_id_t rid_a;
    zlink_routing_id_t rid_b;
    memset (&rid_a, 0, sizeof (rid_a));
    memset (&rid_b, 0, sizeof (rid_b));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_routing_id (router_a, &rid_a));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_routing_id (router_b, &rid_b));

    zlink_registry_topology_filter_t filter_a;
    init_socket_topology_filter_local (&filter_a, "socket-auto-router-router",
                                       ZLINK_SERVICE_ROLE_ROUTER, &rid_a);
    zlink_registry_topology_filter_t filter_b;
    init_socket_topology_filter_local (&filter_b, "socket-auto-router-router",
                                       ZLINK_SERVICE_ROLE_ROUTER, &rid_b);
    bool single_initiator = false;
    for (int i = 0; i < 200 && !single_initiator; ++i) {
        zlink_registry_topology_entry_t entry_a;
        zlink_registry_topology_entry_t entry_b;
        size_t count_a = 1;
        size_t count_b = 1;
        memset (&entry_a, 0, sizeof (entry_a));
        memset (&entry_b, 0, sizeof (entry_b));
        if (zlink_registry_topology_query (registry, &filter_a, &entry_a,
                                           &count_a)
              != ZLINK_CONFIG_OK
            || count_a == 0) {
            msleep (25);
            continue;
        }
        if (zlink_registry_topology_query (registry, &filter_b, &entry_b, &count_b)
              != ZLINK_CONFIG_OK
            || count_b == 0) {
            msleep (25);
            continue;
        }
        single_initiator = entry_a.desired_count + entry_b.desired_count == 1u;
        if (!single_initiator)
            msleep (25);
    }

    TEST_ASSERT_TRUE (single_initiator);

    TEST_ASSERT_TRUE (destroy_discovery_with_retry_local (&discovery_b, 3000));
    TEST_ASSERT_TRUE (destroy_discovery_with_retry_local (&discovery_a, 3000));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}
} // namespace

int main ()
{
    setup_test_environment ();

    UNITY_BEGIN ();
    RUN_TEST (test_socket_discovery_default_dealer_mode_targets_router);
    RUN_TEST (test_socket_discovery_default_dealer_mode_ignores_dealer_peers);
    RUN_TEST (test_socket_discovery_explicit_dealer_mode_targets_dealer);
    RUN_TEST (test_socket_discovery_router_router_uses_single_initiator);
    return UNITY_END ();
}
