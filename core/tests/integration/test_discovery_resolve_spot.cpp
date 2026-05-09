/* SPDX-License-Identifier: MPL-2.0 */

#include "../testutil.hpp"
#include "../testutil_unity.hpp"

#include <string.h>
#include <sstream>
#include <unity.h>

void setUp ()
{
}

void tearDown ()
{
}

namespace
{
bool allocate_loopback_tcp_endpoint_local (std::string *endpoint_out_)
{
    if (!endpoint_out_) {
        errno = EINVAL;
        return false;
    }

    for (int attempt = 0; attempt < 256; ++attempt) {
        fd_t fd = socket (AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (fd == retired_fd)
            continue;

        int reuse = 1;
        setsockopt (fd, SOL_SOCKET, SO_REUSEADDR, as_setsockopt_opt_t (&reuse),
                    sizeof (reuse));

        struct sockaddr_in addr;
        memset (&addr, 0, sizeof (addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl (INADDR_LOOPBACK);
        addr.sin_port = 0;

        if (bind (fd, reinterpret_cast<struct sockaddr *> (&addr),
                  sizeof (addr))
            == 0) {
#if defined ZLINK_HAVE_WINDOWS
            int addr_len = sizeof (addr);
#else
            socklen_t addr_len = sizeof (addr);
#endif
            if (getsockname (fd, reinterpret_cast<struct sockaddr *> (&addr),
                             &addr_len)
                == 0) {
                close (fd);
                std::ostringstream endpoint;
                endpoint << "tcp://127.0.0.1:" << ntohs (addr.sin_port);
                *endpoint_out_ = endpoint.str ();
                return true;
            }
        }

        close (fd);
    }

    errno = EADDRINUSE;
    return false;
}

bool bind_registry_test_endpoints_local (void *registry_,
                                         std::string *pub_out_,
                                         std::string *router_out_)
{
    for (int i = 0; i < 256; ++i) {
        std::string pub_endpoint;
        std::string router_endpoint;
        if (!allocate_loopback_tcp_endpoint_local (&pub_endpoint)
            || !allocate_loopback_tcp_endpoint_local (&router_endpoint)
            || pub_endpoint == router_endpoint) {
            continue;
        }
        if (zlink_registry_bind (registry_, pub_endpoint.c_str (),
                                 router_endpoint.c_str ())
            == ZLINK_BIND_OK) {
            if (pub_out_)
                *pub_out_ = pub_endpoint;
            if (router_out_)
                *router_out_ = router_endpoint;
            return true;
        }
    }
    errno = EADDRINUSE;
    return false;
}

bool bind_spot_test_endpoint_local (void *node_, std::string *endpoint_out_)
{
    if (!node_)
        return false;

    for (int i = 0; i < 256; ++i) {
        std::string endpoint;
        if (!allocate_loopback_tcp_endpoint_local (&endpoint))
            continue;
        if (zlink_spot_node_bind (node_, endpoint.c_str ()) == 0) {
            if (endpoint_out_)
                *endpoint_out_ = endpoint;
            return true;
        }
    }
    errno = EADDRINUSE;
    return false;
}

bool connect_discovery_registry_with_retry_local (void *discovery_,
                                                  const char *endpoint_,
                                                  int timeout_ms_)
{
    return zlink_test_wait_until (timeout_ms_, [=] {
        if (zlink_discovery_connect_registry (discovery_, endpoint_)
            == ZLINK_CONNECT_OK) {
            return true;
        }
        return false;
    });
}

void enable_spot_owner_sync_local (void *discovery_)
{
    int enabled = 1;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_option (discovery_, ZLINK_OPT_DISCOVERY_SPOT_OWNER_SYNC,
                        &enabled, sizeof (enabled)));
}

bool wait_for_resolve_spot_local (void *discovery_,
                                  const zlink_routing_id_t *spot_rid_,
                                  zlink_routing_id_t *owner_node_rid_out_,
                                  int timeout_ms_)
{
    return zlink_test_wait_until (timeout_ms_, [=] {
        if (zlink_discovery_resolve_spot (discovery_, spot_rid_,
                                          owner_node_rid_out_)
            == ZLINK_CONFIG_OK) {
            return true;
        }
        return false;
    });
}

bool wait_for_resolve_owner_local (void *discovery_,
                                   const zlink_routing_id_t *spot_rid_,
                                   const zlink_routing_id_t *expected_owner_,
                                   int timeout_ms_)
{
    zlink_routing_id_t resolved_owner;
    memset (&resolved_owner, 0, sizeof (resolved_owner));
    return zlink_test_wait_until (timeout_ms_, [=, &resolved_owner] {
        if (zlink_discovery_resolve_spot (discovery_, spot_rid_,
                                          &resolved_owner)
              == ZLINK_CONFIG_OK
            && expected_owner_
            && resolved_owner.size == expected_owner_->size
            && memcmp (resolved_owner.data, expected_owner_->data,
                       expected_owner_->size)
                 == 0) {
            return true;
        }
        return false;
    });
}

bool wait_for_resolve_spot_errno_local (void *discovery_,
                                        const zlink_routing_id_t *spot_rid_,
                                        int expected_errno_,
                                        int timeout_ms_)
{
    zlink_routing_id_t owner_node_rid;
    memset (&owner_node_rid, 0, sizeof (owner_node_rid));
    return zlink_test_wait_until (timeout_ms_, [=, &owner_node_rid] {
        errno = 0;
        if (zlink_discovery_resolve_spot (discovery_, spot_rid_,
                                          &owner_node_rid)
            != ZLINK_CONFIG_OK
            && errno == expected_errno_) {
            return true;
        }
        return false;
    });
}

bool wait_for_spot_owner_topology_entry_local (
  void *registry_,
  const char *channel_name_,
  const zlink_routing_id_t *spot_rid_,
  const char *expected_endpoint_,
  int timeout_ms_)
{
    if (!registry_ || !channel_name_ || !spot_rid_ || !expected_endpoint_)
        return false;

    zlink_registry_topology_filter_t filter;
    memset (&filter, 0, sizeof (filter));
    filter.service_kind = ZLINK_SERVICE_KIND_SPOT_PUB;
    filter.service_role = ZLINK_SERVICE_ROLE_SPOT;
    filter.auto_connect_type = ZLINK_AUTO_CONNECT_SPOT_MESH;
    strncpy (filter.channel_name, channel_name_,
             sizeof (filter.channel_name) - 1);
    filter.routing_id = *spot_rid_;

    return zlink_test_wait_until (timeout_ms_, [=, &filter] {
        zlink_registry_topology_entry_t entries[4];
        size_t count = 4;
        if (zlink_registry_topology_query (registry_, &filter, entries, &count)
              == ZLINK_CONFIG_OK
            && count == 1 && strcmp (entries[0].endpoint, expected_endpoint_) == 0
            && entries[0].routing_id.size == spot_rid_->size
            && memcmp (entries[0].routing_id.data, spot_rid_->data,
                       spot_rid_->size)
                 == 0) {
            return true;
        }
        return false;
    });
}

bool wait_for_no_spot_owner_topology_entry_local (
  void *registry_,
  const char *channel_name_,
  const zlink_routing_id_t *spot_rid_,
  int timeout_ms_)
{
    if (!registry_ || !channel_name_ || !spot_rid_)
        return false;

    zlink_registry_topology_filter_t filter;
    memset (&filter, 0, sizeof (filter));
    filter.service_kind = ZLINK_SERVICE_KIND_SPOT_PUB;
    filter.service_role = ZLINK_SERVICE_ROLE_SPOT;
    filter.auto_connect_type = ZLINK_AUTO_CONNECT_SPOT_MESH;
    strncpy (filter.channel_name, channel_name_,
             sizeof (filter.channel_name) - 1);
    filter.routing_id = *spot_rid_;

    return zlink_test_wait_until (timeout_ms_, [=, &filter] {
        zlink_registry_topology_entry_t entries[4];
        size_t count = 4;
        if (zlink_registry_topology_query (registry_, &filter, entries, &count)
              == ZLINK_CONFIG_OK
            && count == 0) {
            return true;
        }
        return false;
    });
}

void test_discovery_resolve_spot_returns_owner_node_rid ()
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

    std::string registry_pub;
    std::string registry_router;
    std::string node_endpoint;
    TEST_ASSERT_TRUE (bind_registry_test_endpoints_local (
      registry, &registry_pub, &registry_router));

    void *discovery =
      zlink_discovery_new (ctx, ZLINK_AUTO_CONNECT_SPOT_MESH, "resolve-spot-svc");
    TEST_ASSERT_NOT_NULL (discovery);
    enable_spot_owner_sync_local (discovery);
    TEST_ASSERT_TRUE (connect_discovery_registry_with_retry_local (
      discovery, registry_router.c_str (), 3000));

    void *node = zlink_spot_node_new (ctx, NULL);
    TEST_ASSERT_NOT_NULL (node);
    void *spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (spot);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_routing_id (node, "resolve-node-a", 14));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_routing_id (spot, "resolve-spot-a", 14));
    TEST_ASSERT_TRUE (bind_spot_test_endpoint_local (node, &node_endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_attach_discovery (node, discovery));

    zlink_routing_id_t spot_rid;
    zlink_routing_id_t node_rid;
    zlink_routing_id_t resolved_node_rid;
    memset (&spot_rid, 0, sizeof (spot_rid));
    memset (&node_rid, 0, sizeof (node_rid));
    memset (&resolved_node_rid, 0, sizeof (resolved_node_rid));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_routing_id (spot, &spot_rid));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_routing_id (node, &node_rid));
    TEST_ASSERT_TRUE (wait_for_resolve_spot_local (
      discovery, &spot_rid, &resolved_node_rid, 5000));
    TEST_ASSERT_EQUAL_UINT8 (node_rid.size, resolved_node_rid.size);
    TEST_ASSERT_EQUAL_MEMORY (node_rid.data, resolved_node_rid.data,
                              node_rid.size);

    zlink_routing_id_t missing_spot_rid;
    memset (&missing_spot_rid, 0, sizeof (missing_spot_rid));
    missing_spot_rid.size = 12;
    memcpy (missing_spot_rid.data, "missing-spot", 12);
    errno = 0;
    TEST_ASSERT_NOT_EQUAL (
      ZLINK_CONFIG_OK,
      zlink_discovery_resolve_spot (discovery, &missing_spot_rid,
                                    &resolved_node_rid));
    TEST_ASSERT_EQUAL_INT (ENOENT, errno);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_discovery_resolve_spot_is_scoped_by_channel_name ()
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

    std::string registry_pub;
    std::string registry_router;
    std::string node_a_endpoint;
    std::string node_b_endpoint;
    TEST_ASSERT_TRUE (bind_registry_test_endpoints_local (
      registry, &registry_pub, &registry_router));

    void *discovery_a =
      zlink_discovery_new (ctx, ZLINK_AUTO_CONNECT_SPOT_MESH, "resolve-svc-a");
    void *discovery_b =
      zlink_discovery_new (ctx, ZLINK_AUTO_CONNECT_SPOT_MESH, "resolve-svc-b");
    TEST_ASSERT_NOT_NULL (discovery_a);
    TEST_ASSERT_NOT_NULL (discovery_b);
    enable_spot_owner_sync_local (discovery_a);
    enable_spot_owner_sync_local (discovery_b);
    TEST_ASSERT_TRUE (connect_discovery_registry_with_retry_local (
      discovery_a, registry_router.c_str (), 3000));
    TEST_ASSERT_TRUE (connect_discovery_registry_with_retry_local (
      discovery_b, registry_router.c_str (), 3000));

    void *node_a = zlink_spot_node_new (ctx, NULL);
    void *node_b = zlink_spot_node_new (ctx, NULL);
    void *spot_a = zlink_spot_new (node_a);
    void *spot_b = zlink_spot_new (node_b);
    TEST_ASSERT_NOT_NULL (node_a);
    TEST_ASSERT_NOT_NULL (node_b);
    TEST_ASSERT_NOT_NULL (spot_a);
    TEST_ASSERT_NOT_NULL (spot_b);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (node_a, "resolve-node-a", 14));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (node_b, "resolve-node-b", 14));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (spot_a, "shared-spot", 11));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (spot_b, "shared-spot", 11));
    TEST_ASSERT_TRUE (bind_spot_test_endpoint_local (node_a, &node_a_endpoint));
    TEST_ASSERT_TRUE (bind_spot_test_endpoint_local (node_b, &node_b_endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_attach_discovery (node_a, discovery_a));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_attach_discovery (node_b, discovery_b));

    zlink_routing_id_t shared_spot_rid;
    zlink_routing_id_t node_a_rid;
    zlink_routing_id_t node_b_rid;
    zlink_routing_id_t resolved_a;
    zlink_routing_id_t resolved_b;
    memset (&shared_spot_rid, 0, sizeof (shared_spot_rid));
    memset (&node_a_rid, 0, sizeof (node_a_rid));
    memset (&node_b_rid, 0, sizeof (node_b_rid));
    memset (&resolved_a, 0, sizeof (resolved_a));
    memset (&resolved_b, 0, sizeof (resolved_b));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_routing_id (spot_a, &shared_spot_rid));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_routing_id (node_a, &node_a_rid));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_routing_id (node_b, &node_b_rid));

    TEST_ASSERT_TRUE (wait_for_resolve_spot_local (
      discovery_a, &shared_spot_rid, &resolved_a, 5000));
    TEST_ASSERT_TRUE (wait_for_resolve_spot_local (
      discovery_b, &shared_spot_rid, &resolved_b, 5000));

    TEST_ASSERT_EQUAL_UINT8 (node_a_rid.size, resolved_a.size);
    TEST_ASSERT_EQUAL_MEMORY (node_a_rid.data, resolved_a.data,
                              node_a_rid.size);
    TEST_ASSERT_EQUAL_UINT8 (node_b_rid.size, resolved_b.size);
    TEST_ASSERT_EQUAL_MEMORY (node_b_rid.data, resolved_b.data,
                              node_b_rid.size);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot_b));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot_a));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node_b));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node_a));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery_b));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery_a));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_discovery_resolve_spot_returns_enoent_after_owner_unregister ()
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

    std::string registry_pub;
    std::string registry_router;
    std::string node_endpoint;
    TEST_ASSERT_TRUE (bind_registry_test_endpoints_local (
      registry, &registry_pub, &registry_router));

    void *discovery =
      zlink_discovery_new (ctx, ZLINK_AUTO_CONNECT_SPOT_MESH, "resolve-unregister");
    TEST_ASSERT_NOT_NULL (discovery);
    enable_spot_owner_sync_local (discovery);
    TEST_ASSERT_TRUE (connect_discovery_registry_with_retry_local (
      discovery, registry_router.c_str (), 3000));

    void *node = zlink_spot_node_new (ctx, NULL);
    void *spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (node);
    TEST_ASSERT_NOT_NULL (spot);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (node, "resolve-node-u", 14));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (spot, "resolve-spot-u", 14));
    TEST_ASSERT_TRUE (bind_spot_test_endpoint_local (node, &node_endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_attach_discovery (node, discovery));

    zlink_routing_id_t spot_rid;
    zlink_routing_id_t resolved_node_rid;
    memset (&spot_rid, 0, sizeof (spot_rid));
    memset (&resolved_node_rid, 0, sizeof (resolved_node_rid));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_routing_id (spot, &spot_rid));
    TEST_ASSERT_TRUE (wait_for_resolve_spot_local (
      discovery, &spot_rid, &resolved_node_rid, 5000));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));

    TEST_ASSERT_TRUE (wait_for_resolve_spot_errno_local (
      discovery, &spot_rid, ENOENT, 5000));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_discovery_resolve_spot_handover_switches_owner ()
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

    std::string registry_pub;
    std::string registry_router;
    std::string node_a_endpoint;
    std::string node_b_endpoint;
    TEST_ASSERT_TRUE (bind_registry_test_endpoints_local (
      registry, &registry_pub, &registry_router));

    void *discovery_a =
      zlink_discovery_new (ctx, ZLINK_AUTO_CONNECT_SPOT_MESH, "resolve-handover");
    void *discovery_b =
      zlink_discovery_new (ctx, ZLINK_AUTO_CONNECT_SPOT_MESH, "resolve-handover");
    void *resolver_discovery =
      zlink_discovery_new (ctx, ZLINK_AUTO_CONNECT_SPOT_MESH, "resolve-handover");
    TEST_ASSERT_NOT_NULL (discovery_a);
    TEST_ASSERT_NOT_NULL (discovery_b);
    TEST_ASSERT_NOT_NULL (resolver_discovery);
    enable_spot_owner_sync_local (discovery_a);
    enable_spot_owner_sync_local (discovery_b);
    TEST_ASSERT_TRUE (connect_discovery_registry_with_retry_local (
      discovery_a, registry_router.c_str (), 3000));
    TEST_ASSERT_TRUE (connect_discovery_registry_with_retry_local (
      discovery_b, registry_router.c_str (), 3000));
    TEST_ASSERT_TRUE (connect_discovery_registry_with_retry_local (
      resolver_discovery, registry_router.c_str (), 3000));

    void *node_a = zlink_spot_node_new (ctx, NULL);
    void *node_b = zlink_spot_node_new (ctx, NULL);
    void *spot_a = zlink_spot_new (node_a);
    void *spot_b = zlink_spot_new (node_b);
    TEST_ASSERT_NOT_NULL (node_a);
    TEST_ASSERT_NOT_NULL (node_b);
    TEST_ASSERT_NOT_NULL (spot_a);
    TEST_ASSERT_NOT_NULL (spot_b);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (node_a, "resolve-node-h0", 15));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (node_b, "resolve-node-h1", 15));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (spot_a, "handover-spot", 13));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (spot_b, "handover-spot", 13));
    TEST_ASSERT_TRUE (bind_spot_test_endpoint_local (node_a, &node_a_endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_attach_discovery (node_a, discovery_a));

    zlink_routing_id_t handover_spot_rid;
    zlink_routing_id_t node_a_rid;
    zlink_routing_id_t node_b_rid;
    zlink_routing_id_t resolved_node_rid;
    memset (&handover_spot_rid, 0, sizeof (handover_spot_rid));
    memset (&node_a_rid, 0, sizeof (node_a_rid));
    memset (&node_b_rid, 0, sizeof (node_b_rid));
    memset (&resolved_node_rid, 0, sizeof (resolved_node_rid));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_routing_id (spot_a, &handover_spot_rid));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_routing_id (node_a, &node_a_rid));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_routing_id (node_b, &node_b_rid));
    TEST_ASSERT_TRUE (wait_for_resolve_owner_local (
      resolver_discovery, &handover_spot_rid, &node_a_rid, 5000));

    TEST_ASSERT_TRUE (bind_spot_test_endpoint_local (node_b, &node_b_endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_attach_discovery (node_b, discovery_b));

    TEST_ASSERT_TRUE (wait_for_resolve_owner_local (
      resolver_discovery, &handover_spot_rid, &node_b_rid, 5000));
    TEST_ASSERT_TRUE (wait_for_spot_owner_topology_entry_local (
      registry, "resolve-handover", &handover_spot_rid,
      node_b_endpoint.c_str (),
      5000));
    TEST_ASSERT_TRUE (wait_for_resolve_spot_local (
      resolver_discovery, &handover_spot_rid, &resolved_node_rid, 5000));
    TEST_ASSERT_EQUAL_UINT8 (node_b_rid.size, resolved_node_rid.size);
    TEST_ASSERT_EQUAL_MEMORY (node_b_rid.data, resolved_node_rid.data,
                              node_b_rid.size);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot_b));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot_a));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node_b));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node_a));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&resolver_discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery_b));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery_a));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_discovery_resolve_spot_default_owner_sync_off ()
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

    std::string registry_pub;
    std::string registry_router;
    std::string node_endpoint;
    TEST_ASSERT_TRUE (bind_registry_test_endpoints_local (
      registry, &registry_pub, &registry_router));

    void *discovery =
      zlink_discovery_new (ctx, ZLINK_AUTO_CONNECT_SPOT_MESH, "resolve-off");
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_TRUE (connect_discovery_registry_with_retry_local (
      discovery, registry_router.c_str (), 3000));

    void *node = zlink_spot_node_new (ctx, NULL);
    void *spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (node);
    TEST_ASSERT_NOT_NULL (spot);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (node, "resolve-node-off", 16));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (spot, "resolve-spot-off", 16));
    TEST_ASSERT_TRUE (bind_spot_test_endpoint_local (node, &node_endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_attach_discovery (node, discovery));

    zlink_routing_id_t spot_rid;
    memset (&spot_rid, 0, sizeof (spot_rid));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_routing_id (spot, &spot_rid));
    TEST_ASSERT_TRUE (wait_for_no_spot_owner_topology_entry_local (
      registry, "resolve-off", &spot_rid, 1500));

    enable_spot_owner_sync_local (discovery);
    zlink_routing_id_t resolved_node_rid;
    memset (&resolved_node_rid, 0, sizeof (resolved_node_rid));
    TEST_ASSERT_TRUE (wait_for_resolve_spot_local (
      discovery, &spot_rid, &resolved_node_rid, 5000));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}
} // namespace

int main ()
{
    setup_test_environment ();

    UNITY_BEGIN ();
    RUN_TEST (test_discovery_resolve_spot_returns_owner_node_rid);
    RUN_TEST (test_discovery_resolve_spot_is_scoped_by_channel_name);
    RUN_TEST (test_discovery_resolve_spot_returns_enoent_after_owner_unregister);
    RUN_TEST (test_discovery_resolve_spot_handover_switches_owner);
    RUN_TEST (test_discovery_resolve_spot_default_owner_sync_off);
    return UNITY_END ();
}
