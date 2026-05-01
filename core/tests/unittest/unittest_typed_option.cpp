/* SPDX-License-Identifier: MPL-2.0 */

#include "../testutil.hpp"
#include "../testutil_unity.hpp"

#include "../../src/api/service_api_internal.hpp"
#include "../../src/api/zlink_testing.hpp"
#include "core/internal_defs.hpp"
#include "core/options_owner.hpp"
#include "../../src/services/spot/spot_handle.hpp"
#include "../../src/services/spot/spot_node.hpp"
#include "../../src/services/spot/spot_node_access.hpp"

#include <string.h>
#include <unity.h>

namespace
{
void *make_test_spot_handle (void *node_)
{
    zlink::spot_node_t *node = zlink::spot_node_access_t::from_handle (node_);
    if (!node)
        return NULL;

    spot_handle_t *spot = new (std::nothrow) spot_handle_t ();
    if (!spot)
        return NULL;
    spot->node = node;
    register_spot_mode_state (spot);
    return spot;
}

void destroy_test_spot_handle (void **spot_p_)
{
    if (!spot_p_ || !*spot_p_)
        return;

    spot_handle_t *spot = static_cast<spot_handle_t *> (*spot_p_);
    zlink::destroy_spot_handle_for_testing (spot);
    erase_spot_mode_state (spot);
    *spot_p_ = NULL;
}
} // namespace

void setUp ()
{
}

void tearDown ()
{
}

static void *new_ctx ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    return ctx;
}

static void close_ctx (void *ctx_)
{
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx_));
}

static void close_monitor_if_open (void **monitor_p_)
{
    if (monitor_p_ && *monitor_p_)
        TEST_ASSERT_SUCCESS_ERRNO (zlink_monitor_close (monitor_p_));
}

void test_typed_raw_socket_options ()
{
    void *ctx = new_ctx ();
    void *router = zlink_socket (ctx, ZLINK_SOCKET_ROUTER);
    void *dealer = zlink_socket (ctx, ZLINK_SOCKET_DEALER);
    void *stream = zlink_socket (ctx, ZLINK_SOCKET_STREAM);
    void *xpub = zlink_socket (ctx, ZLINK_SOCKET_XPUB);
    void *xsub = zlink_socket (ctx, ZLINK_SOCKET_XSUB);
    TEST_ASSERT_NOT_NULL (router);
    TEST_ASSERT_NOT_NULL (dealer);
    TEST_ASSERT_NOT_NULL (stream);
    TEST_ASSERT_NOT_NULL (xpub);
    TEST_ASSERT_NOT_NULL (xsub);

    int value = 42;
    size_t size = sizeof (value);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_option (router, ZLINK_OPT_SNDHWM, &value, sizeof (value)));
    value = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_get_option (router, ZLINK_OPT_SNDHWM, &value, &size));
    TEST_ASSERT_EQUAL_INT (42, value);

    value = 1;
    size = sizeof (value);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_option (stream, ZLINK_OPT_IPV6, &value, sizeof (value)));
    value = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_get_option (stream, ZLINK_OPT_IPV6, &value, &size));
    TEST_ASSERT_EQUAL_INT (1, value);

    value = 2500;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_option (
      dealer, ZLINK_OPT_HEARTBEAT_IVL, &value, sizeof (value)));
    value = 0;
    size = sizeof (value);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_get_option (dealer, ZLINK_OPT_HEARTBEAT_IVL, &value, &size));
    TEST_ASSERT_EQUAL_INT (2500, value);

    value = 0;
    size = sizeof (value);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_get_option (xpub, ZLINK_OPT_TYPE, &value, &size));
    TEST_ASSERT_EQUAL_INT (ZLINK_CORE_SOCKET_XPUB, value);

    value = -1;
    size = sizeof (value);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_get_option (router, ZLINK_OPT_RID_DUPLICATE_POLICY, &value,
                        &size));
    TEST_ASSERT_EQUAL_INT (ZLINK_RID_DUPLICATE_REJECT, value);

    value = ZLINK_RID_DUPLICATE_HANDOVER;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_option (router, ZLINK_OPT_RID_DUPLICATE_POLICY, &value,
                        sizeof (value)));
    value = -1;
    size = sizeof (value);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_get_option (router, ZLINK_OPT_RID_DUPLICATE_POLICY, &value,
                        &size));
    TEST_ASSERT_EQUAL_INT (ZLINK_RID_DUPLICATE_HANDOVER, value);

    value = 1;
    size = sizeof (value);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_router_option (
      router, ZLINK_ROUTER_OPT_MANDATORY, &value, sizeof (value)));
    value = 0;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_router_option (
      router, ZLINK_ROUTER_OPT_MANDATORY, &value, &size));
    TEST_ASSERT_EQUAL_INT (1, value);

    value = 4321;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_router_option (
      router, ZLINK_ROUTER_OPT_REQUEST_TIMEOUT_MS, &value, sizeof (value)));
    value = 0;
    size = sizeof (value);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_router_option (
      router, ZLINK_ROUTER_OPT_REQUEST_TIMEOUT_MS, &value, &size));
    TEST_ASSERT_EQUAL_INT (4321, value);

    value = 50;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_router_option (
      router, ZLINK_ROUTER_OPT_WEIGHT, &value, sizeof (value)));
    value = 0;
    size = sizeof (value);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_router_option (
      router, ZLINK_ROUTER_OPT_WEIGHT, &value, &size));
    TEST_ASSERT_EQUAL_INT (50, value);

    value = 1;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_dealer_option (
      dealer, ZLINK_DEALER_OPT_PROBE, &value, sizeof (value)));

    value = 3210;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_dealer_option (
      dealer, ZLINK_DEALER_OPT_REQUEST_TIMEOUT_MS, &value, sizeof (value)));

    value = 25;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_dealer_option (
      dealer, ZLINK_DEALER_OPT_WEIGHT, &value, sizeof (value)));

    value = 1;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_stream_option (
      stream, ZLINK_STREAM_OPT_NOTIFY, &value, sizeof (value)));
    value = 0;
    size = sizeof (value);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_stream_option (
      stream, ZLINK_STREAM_OPT_NOTIFY, &value, &size));
    TEST_ASSERT_EQUAL_INT (1, value);

    TEST_ASSERT_NOT_EQUAL (
      ZLINK_CONFIG_OK, zlink_set_routing_id (stream, "s1", 2));
    TEST_ASSERT_EQUAL_INT (EINVAL, errno);

    value = 1;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_pub_option (xpub, ZLINK_PUB_OPT_NODROP, &value, sizeof (value)));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_subscription (xsub, "topic"));

    value = 0;
    size = sizeof (value);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_get_sub_option (xsub, ZLINK_SUB_OPT_TOPICS_COUNT, &value, &size));
    TEST_ASSERT_EQUAL_INT (1, value);

    char filter[32];
    size_t filter_len = sizeof (filter);
    int is_pattern = -1;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_subscription_at (xsub, 0, filter, &filter_len, &is_pattern));
    TEST_ASSERT_EQUAL_UINT (5, (unsigned int) filter_len);
    TEST_ASSERT_EQUAL_MEMORY ("topic", filter, 5);
    TEST_ASSERT_EQUAL_INT (0, is_pattern);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (router));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (dealer));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (stream));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (xpub));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (xsub));
    close_ctx (ctx);
}

void test_typed_spot_node_unified_options ()
{
    void *ctx = new_ctx ();
    void *node = zlink_spot_node_new (ctx, NULL);
    TEST_ASSERT_NOT_NULL (node);
    void *spot = make_test_spot_handle (node);
    TEST_ASSERT_NOT_NULL (spot);

    int pub_hwm = 77;
    int sub_hwm = 88;
    size_t size = sizeof (pub_hwm);
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_INVALID_ARGUMENT,
      zlink_set_option (node, ZLINK_OPT_SNDHWM, &pub_hwm, sizeof (pub_hwm)));
    TEST_ASSERT_EQUAL_INT (EINVAL, zlink_errno ());
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_INVALID_ARGUMENT,
      zlink_set_option (node, ZLINK_OPT_RCVHWM, &sub_hwm, sizeof (sub_hwm)));
    TEST_ASSERT_EQUAL_INT (EINVAL, zlink_errno ());

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_spot_node_option (node, ZLINK_SPOT_NODE_OPT_PUBSUB_HWM,
                                  &sub_hwm, sizeof (sub_hwm)));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (node, "npub", 4));

    int nodrop = 1;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_pub_option (node, ZLINK_PUB_OPT_NODROP, &nodrop, sizeof (nodrop)));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_subscription (spot, "alpha"));

    int got = 0;
    size = sizeof (got);
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_INVALID_ARGUMENT,
      zlink_get_option (node, ZLINK_OPT_SNDHWM, &got, &size));
    TEST_ASSERT_EQUAL_INT (EINVAL, zlink_errno ());

    size = sizeof (got);
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_INVALID_ARGUMENT,
      zlink_get_option (node, ZLINK_OPT_RCVHWM, &got, &size));
    TEST_ASSERT_EQUAL_INT (EINVAL, zlink_errno ());

    size = sizeof (got);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_get_sub_option (spot, ZLINK_SUB_OPT_TOPICS_COUNT, &got, &size));
    TEST_ASSERT_EQUAL_INT (1, got);

    char filter[32];
    size_t filter_len = sizeof (filter);
    int is_pattern = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_subscription_at (spot, 0, filter, &filter_len, &is_pattern));
    TEST_ASSERT_EQUAL_UINT (5, (unsigned int) filter_len);
    TEST_ASSERT_EQUAL_MEMORY ("alpha", filter, 5);
    TEST_ASSERT_EQUAL_INT (0, is_pattern);

    destroy_test_spot_handle (&spot);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    close_ctx (ctx);
}

void test_typed_service_handle_dispatch_domains ()
{
    void *ctx = new_ctx ();
    void *discovery =
      zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_SPOT, "svc-alpha");
    void *registry = zlink_registry_new (ctx);
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_NOT_NULL (registry);

    size_t metadata_limit = 91;
    size_t size = sizeof (metadata_limit);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_option (discovery, ZLINK_OPT_DISCOVERY_METADATA_MAX_SIZE,
                        &metadata_limit, sizeof (metadata_limit)));
    metadata_limit = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_get_option (discovery, ZLINK_OPT_DISCOVERY_METADATA_MAX_SIZE,
                        &metadata_limit, &size));
    TEST_ASSERT_EQUAL_UINT (91, (unsigned int) metadata_limit);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (discovery, "disc", 4));
    zlink_routing_id_t rid;
    memset (&rid, 0, sizeof (rid));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_routing_id (discovery, &rid));
    TEST_ASSERT_EQUAL_UINT8 (4, rid.size);
    TEST_ASSERT_EQUAL_MEMORY ("disc", rid.data, 4);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_tls_client (discovery, "ca.pem", "disc-host", 1));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_tls_server (registry, "srv-cert.pem", "srv-key.pem", 1));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_tls_client (registry, "ca.pem", "registry-host", 0));

    errno = 0;
    TEST_ASSERT_NOT_EQUAL (
      ZLINK_CONFIG_OK, zlink_set_routing_id (registry, "rid", 3));
    TEST_ASSERT_EQUAL_INT (EFAULT, errno);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    close_ctx (ctx);
}

void test_spot_node_internal_socket_snapshot_is_supported ()
{
    void *ctx = new_ctx ();
    void *node = zlink_spot_node_new (ctx, NULL);
    TEST_ASSERT_NOT_NULL (node);
    void *spot = make_test_spot_handle (node);
    TEST_ASSERT_NOT_NULL (spot);
    size_t socket_count = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_internal_sockets_snapshot (node, NULL, NULL,
                                                 &socket_count));
    TEST_ASSERT_GREATER_THAN_UINT (0, socket_count);

    destroy_test_spot_handle (&spot);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    close_ctx (ctx);
}

void test_discovery_routing_id_locks_after_registry_connect ()
{
    if (!zlink_has ("tcp")) {
        TEST_IGNORE_MESSAGE ("TCP not available");
        return;
    }

    void *ctx = new_ctx ();
    void *discovery =
      zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_SPOT, "svc-lock");
    void *registry = zlink_registry_new (ctx);
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_NOT_NULL (registry);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (discovery, "disc", 4));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_registry_bind (registry, ENDPOINT_0, ENDPOINT_1));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_connect_registry (discovery, ENDPOINT_1));

    errno = 0;
    TEST_ASSERT_NOT_EQUAL (
      ZLINK_CONFIG_OK, zlink_set_routing_id (discovery, "next", 4));
    TEST_ASSERT_EQUAL_INT (EFSM, errno);

    zlink_routing_id_t rid;
    memset (&rid, 0, sizeof (rid));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_routing_id (discovery, &rid));
    TEST_ASSERT_EQUAL_UINT8 (4, rid.size);
    TEST_ASSERT_EQUAL_MEMORY ("disc", rid.data, 4);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    close_ctx (ctx);
}

void test_option_owner_map_matches_domains ()
{
    TEST_ASSERT_EQUAL_INT (
      zlink::options_owner_core_socket,
      zlink::option_owner_of (ZLINK_INTERNAL_OPT_SNDHWM));
    TEST_ASSERT_EQUAL_INT (
      zlink::options_owner_core_socket,
      zlink::option_owner_of (ZLINK_INTERNAL_OPT_HEARTBEAT_TIMEOUT));

    TEST_ASSERT_EQUAL_INT (
      zlink::options_owner_transport_network,
      zlink::option_owner_of (ZLINK_INTERNAL_OPT_TCP_NODELAY));
    TEST_ASSERT_EQUAL_INT (
      zlink::options_owner_transport_network,
      zlink::option_owner_of (ZLINK_INTERNAL_OPT_BINDTODEVICE));

    TEST_ASSERT_EQUAL_INT (
      zlink::options_owner_protocol_metadata,
      zlink::option_owner_of (ZLINK_INTERNAL_OPT_HEARTBEAT_TTL));
#ifdef ZLINK_HAVE_TLS
    TEST_ASSERT_EQUAL_INT (
      zlink::options_owner_protocol_metadata,
      zlink::option_owner_of (ZLINK_INTERNAL_OPT_TLS_CERT));
#endif

    TEST_ASSERT_EQUAL_INT (
      zlink::options_owner_service_specific,
      zlink::option_owner_of (ZLINK_INTERNAL_OPT_TOPICS_COUNT));
    TEST_ASSERT_EQUAL_INT (
      zlink::options_owner_service_specific,
      zlink::option_owner_of (ZLINK_INTERNAL_OPT_LAST_ENDPOINT));
    TEST_ASSERT_EQUAL_INT (
      zlink::options_owner_unknown,
      zlink::option_owner_of (ZLINK_INTERNAL_OPT_BLOCKY));

    TEST_ASSERT_EQUAL_INT (
      zlink::options_owner_core_socket,
      zlink::common_option_owner_of (ZLINK_OPT_SNDHWM));
    TEST_ASSERT_EQUAL_INT (
      zlink::options_owner_core_socket,
      zlink::common_option_owner_of (ZLINK_OPT_RID_DUPLICATE_POLICY));
    TEST_ASSERT_EQUAL_INT (
      zlink::options_owner_transport_network,
      zlink::common_option_owner_of (ZLINK_OPT_TCP_NODELAY));
    TEST_ASSERT_EQUAL_INT (
      zlink::options_owner_protocol_metadata,
      zlink::common_option_owner_of (ZLINK_OPT_HEARTBEAT_TTL));
    TEST_ASSERT_EQUAL_INT (
      zlink::options_owner_service_specific,
      zlink::common_option_owner_of (ZLINK_OPT_LAST_ENDPOINT));
    TEST_ASSERT_EQUAL_INT (
      zlink::options_owner_unknown,
      zlink::common_option_owner_of (ZLINK_OPT_BLOCKY));

    TEST_ASSERT_EQUAL_INT (
      zlink::options_owner_service_specific,
      zlink::router_option_owner_of (ZLINK_ROUTER_OPT_CONNECT_ROUTING_ID));
    TEST_ASSERT_EQUAL_INT (
      zlink::options_owner_core_socket,
      zlink::router_option_owner_of (ZLINK_ROUTER_OPT_WEIGHT));
    TEST_ASSERT_EQUAL_INT (
      zlink::options_owner_service_specific,
      zlink::dealer_option_owner_of (ZLINK_DEALER_OPT_PROBE));
    TEST_ASSERT_EQUAL_INT (
      zlink::options_owner_core_socket,
      zlink::dealer_option_owner_of (ZLINK_DEALER_OPT_WEIGHT));
    TEST_ASSERT_EQUAL_INT (
      zlink::options_owner_core_socket,
      zlink::stream_option_owner_of (ZLINK_STREAM_OPT_NOTIFY));
    TEST_ASSERT_EQUAL_INT (
      zlink::options_owner_service_specific,
      zlink::pub_option_owner_of (ZLINK_PUB_OPT_MANUAL_LAST_VALUE));
    TEST_ASSERT_EQUAL_INT (
      zlink::options_owner_service_specific,
      zlink::sub_option_owner_of (ZLINK_SUB_OPT_TOPICS_COUNT));

    TEST_ASSERT_EQUAL_INT (
      zlink::options_owner_core_socket,
      zlink::option_owner_of_bag_field (
        zlink::options_bag_field_monitor_event_version));
    TEST_ASSERT_EQUAL_INT (
      zlink::options_owner_protocol_metadata,
      zlink::option_owner_of_bag_field (zlink::options_bag_field_hello_msg));
    TEST_ASSERT_EQUAL_INT (
      zlink::options_owner_protocol_metadata,
      zlink::option_owner_of_bag_field (
        zlink::options_bag_field_disconnect_msg));
    TEST_ASSERT_EQUAL_INT (
      zlink::options_owner_protocol_metadata,
      zlink::option_owner_of_bag_field (zlink::options_bag_field_hiccup_msg));
    TEST_ASSERT_EQUAL_INT (
      zlink::options_owner_transport_network,
      zlink::option_owner_of_bag_field (zlink::options_bag_field_busy_poll));

    TEST_ASSERT_EQUAL_STRING (
      "core-socket",
      zlink::option_owner_name (
        zlink::option_owner_of (ZLINK_INTERNAL_OPT_SNDHWM)));
    TEST_ASSERT_EQUAL_STRING (
      "transport-network",
      zlink::option_owner_name (
        zlink::option_owner_of (ZLINK_INTERNAL_OPT_TCP_NODELAY)));
    TEST_ASSERT_EQUAL_STRING (
      "protocol-metadata",
      zlink::option_owner_name (
        zlink::option_owner_of (ZLINK_INTERNAL_OPT_HEARTBEAT_TTL)));
    TEST_ASSERT_EQUAL_STRING (
      "service-specific",
      zlink::option_owner_name (
        zlink::option_owner_of (ZLINK_INTERNAL_OPT_TOPICS_COUNT)));
}

int main (void)
{
    setup_test_environment ();

    UNITY_BEGIN ();
    RUN_TEST (test_typed_raw_socket_options);
    RUN_TEST (test_typed_spot_node_unified_options);
    RUN_TEST (test_typed_service_handle_dispatch_domains);
    RUN_TEST (test_spot_node_internal_socket_snapshot_is_supported);
    RUN_TEST (test_discovery_routing_id_locks_after_registry_connect);
    RUN_TEST (test_option_owner_map_matches_domains);
    return UNITY_END ();
}
