/* SPDX-License-Identifier: MPL-2.0 */

#include "../testutil.hpp"
#include "../testutil_unity.hpp"
#include "spot_multi_service_fixture.hpp"

#include "../../src/services/discovery/discovery_owned_service.hpp"
#include "../../src/services/discovery/discovery_protocol.hpp"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <string>
#include <vector>

#include <unity.h>

using namespace spot_multi_service_fixture;

void setUp ()
{
}

void tearDown ()
{
}

namespace
{
bool wait_for_spot_node_provider_weight_local (void *registry_,
                                               const char *service_name_,
                                               uint32_t expected_weight_,
                                               int timeout_ms_)
{
    return zlink_test_wait_until (timeout_ms_, [=] {
        zlink_member_peer_entry_t entries[8];
        size_t count = 8;
        if (zlink_registry_member_peers (registry_, service_name_, entries,
                                         &count)
            == ZLINK_CONFIG_OK) {
            for (size_t j = 0; j < count; ++j) {
                if (entries[j].service_role
                      == zlink::discovery_protocol::service_role_spot
                    && entries[j].weight == expected_weight_) {
                    return true;
                }
            }
        }
        return false;
    });
}

void test_spot_service_discovery_replays_existing_filters_end_to_end ()
{
    using namespace zlink::discovery_protocol;

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
    char xpub_endpoint[MAX_SOCKET_STRING];
    char sub_endpoint[MAX_SOCKET_STRING];
    snprintf (registry_pub, sizeof (registry_pub), "tcp://127.0.0.1:%d",
              test_port (6010));
    snprintf (registry_router, sizeof (registry_router), "tcp://127.0.0.1:%d",
              test_port (6011));
    snprintf (xpub_endpoint, sizeof (xpub_endpoint), "tcp://127.0.0.1:%d",
              test_port (6012));
    snprintf (sub_endpoint, sizeof (sub_endpoint), "tcp://127.0.0.1:%d",
              test_port (6013));

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_registry_bind (registry, registry_pub, registry_router));

    void *consumer_discovery =
      zlink_discovery_new (ctx, ZLINK_AUTO_CONNECT_CLIENT_SERVER, "spot-auto-sub");
    void *provider_discovery =
      zlink_discovery_new (ctx, ZLINK_AUTO_CONNECT_CLIENT_SERVER, "spot-auto-sub");
    TEST_ASSERT_NOT_NULL (consumer_discovery);
    TEST_ASSERT_NOT_NULL (provider_discovery);
    TEST_ASSERT_TRUE (connect_discovery_registry_with_retry_local (
      consumer_discovery, registry_router, 3000));
    TEST_ASSERT_TRUE (connect_discovery_registry_with_retry_local (
      provider_discovery, registry_router, 3000));

    void *node = zlink_spot_node_new (ctx, NULL);
    TEST_ASSERT_NOT_NULL (node);
    void *spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (spot);
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK, zlink_set_subscription (spot, "bench"));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK,
      zlink_spot_node_attach_discovery (node, consumer_discovery));

    void *xpub = zlink_socket (ctx, ZLINK_SOCKET_XPUB);
    void *sub = zlink_socket (ctx, ZLINK_SOCKET_SUB);
    TEST_ASSERT_NOT_NULL (xpub);
    TEST_ASSERT_NOT_NULL (sub);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_bind (xpub, xpub_endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_bind (sub, sub_endpoint));

    std::string resolved_pub;
    std::string resolved_sub;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink::discovery_owned_service::register_endpoint (
        static_cast<zlink::discovery_t *> (provider_discovery), xpub_endpoint, &resolved_pub, NULL, service_role_pub));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink::discovery_owned_service::register_endpoint (
        static_cast<zlink::discovery_t *> (provider_discovery), sub_endpoint, &resolved_sub, NULL, service_role_sub));

    TEST_ASSERT_TRUE (wait_for_service_summary_count_local (
      consumer_discovery, service_role_pub, 1, 5000));
    TEST_ASSERT_TRUE (wait_for_service_summary_count_local (
      consumer_discovery, service_role_sub, 1, 5000));
    TEST_ASSERT_TRUE (wait_for_service_attachment_shape_local (
      node, "spot-auto-sub", 0, 1, 1, 5000));

    int subscribed = 0;
    TEST_ASSERT_TRUE (
      wait_for_subscription_event_local (xpub, "bench", &subscribed, 5000));
    TEST_ASSERT_EQUAL_INT (1, subscribed);

    zlink_msg_t part;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&part, 4));
    memcpy (zlink_msg_data (&part), "pong", 4);
    TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_OK,
                           zlink_publish (xpub, "bench", &part, 1, 0));

    TEST_ASSERT_TRUE (wait_for_spot_service_message_local (
      spot, "spot-auto-sub", "bench", "pong", 5000));
    close_zero_linger (sub);
    close_zero_linger (xpub);
    destroy_multi_service_fixture_local (&spot, &node, &consumer_discovery,
                                         &provider_discovery, &registry, ctx);
}

void test_spot_node_provider_registers_default_weight ()
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
    snprintf (registry_pub, sizeof (registry_pub), "tcp://127.0.0.1:%d",
              test_port (6070));
    snprintf (registry_router, sizeof (registry_router), "tcp://127.0.0.1:%d",
              test_port (6071));

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_registry_bind (registry, registry_pub, registry_router));

    void *discovery =
      zlink_discovery_new (ctx, ZLINK_AUTO_CONNECT_SPOT_MESH, "spot-weight-default");
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_TRUE (connect_discovery_registry_with_retry_local (
      discovery, registry_router, 3000));

    void *node = zlink_spot_node_new (ctx, NULL);
    TEST_ASSERT_NOT_NULL (node);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_bind (node, "tcp://127.0.0.1:*"));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK,
      zlink_spot_node_attach_discovery (node, discovery));

    TEST_ASSERT_TRUE (wait_for_spot_node_provider_weight_local (
      registry, "spot-weight-default", 100, 5000));

    void *spot = NULL;
    void *provider_discovery = NULL;
    destroy_multi_service_fixture_local (&spot, &node, &discovery,
                                         &provider_discovery, &registry, ctx);
}

void test_spot_service_discovery_replays_filters_after_pubsub_churn ()
{
    using namespace zlink::discovery_protocol;

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
    char xpub_endpoint[MAX_SOCKET_STRING];
    char sub_endpoint[MAX_SOCKET_STRING];
    snprintf (registry_pub, sizeof (registry_pub), "tcp://127.0.0.1:%d",
              test_port (6014));
    snprintf (registry_router, sizeof (registry_router), "tcp://127.0.0.1:%d",
              test_port (6015));
    snprintf (xpub_endpoint, sizeof (xpub_endpoint), "tcp://127.0.0.1:%d",
              test_port (6016));
    snprintf (sub_endpoint, sizeof (sub_endpoint), "tcp://127.0.0.1:%d",
              test_port (6017));

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_registry_bind (registry, registry_pub, registry_router));

    void *consumer_discovery =
      zlink_discovery_new (ctx, ZLINK_AUTO_CONNECT_CLIENT_SERVER, "spot-auto-churn");
    void *provider_discovery =
      zlink_discovery_new (ctx, ZLINK_AUTO_CONNECT_CLIENT_SERVER, "spot-auto-churn");
    TEST_ASSERT_NOT_NULL (consumer_discovery);
    TEST_ASSERT_NOT_NULL (provider_discovery);
    TEST_ASSERT_TRUE (connect_discovery_registry_with_retry_local (
      consumer_discovery, registry_router, 3000));
    TEST_ASSERT_TRUE (connect_discovery_registry_with_retry_local (
      provider_discovery, registry_router, 3000));

    void *node = zlink_spot_node_new (ctx, NULL);
    TEST_ASSERT_NOT_NULL (node);
    void *spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (spot);
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK, zlink_set_subscription (spot, "bench"));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK,
      zlink_spot_node_attach_discovery (node, consumer_discovery));

    void *xpub = zlink_socket (ctx, ZLINK_SOCKET_XPUB);
    void *sub = zlink_socket (ctx, ZLINK_SOCKET_SUB);
    TEST_ASSERT_NOT_NULL (xpub);
    TEST_ASSERT_NOT_NULL (sub);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_bind (xpub, xpub_endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_bind (sub, sub_endpoint));

    std::string resolved_pub;
    std::string resolved_sub;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink::discovery_owned_service::register_endpoint (
        static_cast<zlink::discovery_t *> (provider_discovery), xpub_endpoint, &resolved_pub, NULL, service_role_pub));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink::discovery_owned_service::register_endpoint (
        static_cast<zlink::discovery_t *> (provider_discovery), sub_endpoint, &resolved_sub, NULL, service_role_sub));

    TEST_ASSERT_TRUE (wait_for_service_attachment_shape_local (
      node, "spot-auto-churn", 0, 1, 1, 5000));

    int subscribed = 0;
    TEST_ASSERT_TRUE (
      wait_for_subscription_event_local (xpub, "bench", &subscribed, 5000));
    TEST_ASSERT_EQUAL_INT (1, subscribed);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink::discovery_owned_service::unregister_endpoint (
        static_cast<zlink::discovery_t *> (provider_discovery), resolved_pub.c_str (), service_role_pub));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink::discovery_owned_service::unregister_endpoint (
        static_cast<zlink::discovery_t *> (provider_discovery), resolved_sub.c_str (), service_role_sub));
    TEST_ASSERT_TRUE (wait_for_service_attachment_shape_local (
      node, "spot-auto-churn", 0, 0, 0, 5000));

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink::discovery_owned_service::register_endpoint (
        static_cast<zlink::discovery_t *> (provider_discovery), xpub_endpoint, &resolved_pub, NULL, service_role_pub));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink::discovery_owned_service::register_endpoint (
        static_cast<zlink::discovery_t *> (provider_discovery), sub_endpoint, &resolved_sub, NULL, service_role_sub));
    TEST_ASSERT_TRUE (wait_for_service_attachment_shape_local (
      node, "spot-auto-churn", 0, 1, 1, 5000));

    TEST_ASSERT_TRUE (wait_for_publish_and_spot_service_message_local (
      xpub, spot, "spot-auto-churn", "bench", "again", 5000));

    close_zero_linger (sub);
    close_zero_linger (xpub);
    destroy_multi_service_fixture_local (&spot, &node, &consumer_discovery,
                                         &provider_discovery, &registry, ctx);
}

void test_spot_service_discovery_multi_router_distributes_across_candidates ()
{
    using namespace zlink::discovery_protocol;

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
    snprintf (registry_pub, sizeof (registry_pub), "tcp://127.0.0.1:%d",
              test_port (6020));
    snprintf (registry_router, sizeof (registry_router), "tcp://127.0.0.1:%d",
              test_port (6021));
    snprintf (router_a_endpoint, sizeof (router_a_endpoint), "tcp://127.0.0.1:%d",
              test_port (6022));
    snprintf (router_b_endpoint, sizeof (router_b_endpoint), "tcp://127.0.0.1:%d",
              test_port (6023));

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_registry_bind (registry, registry_pub, registry_router));

    void *consumer_discovery =
      zlink_discovery_new (ctx, ZLINK_AUTO_CONNECT_CLIENT_SERVER, "spot-auto-router");
    void *provider_discovery =
      zlink_discovery_new (ctx, ZLINK_AUTO_CONNECT_CLIENT_SERVER, "spot-auto-router");
    TEST_ASSERT_NOT_NULL (consumer_discovery);
    TEST_ASSERT_NOT_NULL (provider_discovery);
    TEST_ASSERT_TRUE (connect_discovery_registry_with_retry_local (
      consumer_discovery, registry_router, 3000));
    TEST_ASSERT_TRUE (connect_discovery_registry_with_retry_local (
      provider_discovery, registry_router, 3000));

    void *node = zlink_spot_node_new (ctx, NULL);
    TEST_ASSERT_NOT_NULL (node);
    void *spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (spot);
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK,
      zlink_spot_node_attach_discovery (node, consumer_discovery));

    void *router_a = zlink_socket (ctx, ZLINK_SOCKET_ROUTER);
    void *router_b = zlink_socket (ctx, ZLINK_SOCKET_ROUTER);
    TEST_ASSERT_NOT_NULL (router_a);
    TEST_ASSERT_NOT_NULL (router_b);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (router_a, "svc-r-a", 7));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (router_b, "svc-r-b", 7));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_bind (router_a, router_a_endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_bind (router_b, router_b_endpoint));

    std::string resolved_a;
    std::string resolved_b;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink::discovery_owned_service::register_endpoint (
        static_cast<zlink::discovery_t *> (provider_discovery), router_a_endpoint, &resolved_a, NULL, service_role_router));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink::discovery_owned_service::register_endpoint (
        static_cast<zlink::discovery_t *> (provider_discovery), router_b_endpoint, &resolved_b, NULL, service_role_router));

    TEST_ASSERT_TRUE (wait_for_service_summary_count_local (
      consumer_discovery, service_role_router, 2, 5000));
    TEST_ASSERT_TRUE (wait_for_service_attachment_shape_local (
      node, "spot-auto-router", 2, 0, 0, 5000));

    TEST_ASSERT_TRUE (wait_for_router_distribution_local (
      spot, "spot-auto-router", router_a, router_b, 5000));
    close_zero_linger (router_b);
    close_zero_linger (router_a);
    destroy_multi_service_fixture_local (&spot, &node, &consumer_discovery,
                                         &provider_discovery, &registry, ctx);
}

void test_spot_service_discovery_many_router_candidates_attach_and_distribute ()
{
    using namespace zlink::discovery_protocol;

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
    snprintf (registry_pub, sizeof (registry_pub), "tcp://127.0.0.1:%d",
              test_port (6020));
    snprintf (registry_router, sizeof (registry_router), "tcp://127.0.0.1:%d",
              test_port (6021));

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_registry_bind (registry, registry_pub, registry_router));

    void *consumer_discovery =
      zlink_discovery_new (ctx, ZLINK_AUTO_CONNECT_CLIENT_SERVER, "spot-auto-router-n");
    void *provider_discovery =
      zlink_discovery_new (ctx, ZLINK_AUTO_CONNECT_CLIENT_SERVER, "spot-auto-router-n");
    TEST_ASSERT_NOT_NULL (consumer_discovery);
    TEST_ASSERT_NOT_NULL (provider_discovery);
    TEST_ASSERT_TRUE (connect_discovery_registry_with_retry_local (
      consumer_discovery, registry_router, 3000));
    TEST_ASSERT_TRUE (connect_discovery_registry_with_retry_local (
      provider_discovery, registry_router, 3000));

    void *node = zlink_spot_node_new (ctx, NULL);
    TEST_ASSERT_NOT_NULL (node);
    void *spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (spot);
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK,
      zlink_spot_node_attach_discovery (node, consumer_discovery));

    std::vector<void *> routers;
    for (int i = 0; i < 6; ++i) {
        char router_endpoint[MAX_SOCKET_STRING];
        char routing_id[16];
        snprintf (router_endpoint, sizeof (router_endpoint),
                  "tcp://127.0.0.1:%d", test_port (6022 + i));
        snprintf (routing_id, sizeof (routing_id), "svc-n-%d", i);

        void *router = zlink_socket (ctx, ZLINK_SOCKET_ROUTER);
        TEST_ASSERT_NOT_NULL (router);
        TEST_ASSERT_SUCCESS_ERRNO (
          zlink_set_routing_id (router, routing_id, strlen (routing_id)));
        TEST_ASSERT_SUCCESS_ERRNO (zlink_bind (router, router_endpoint));
        routers.push_back (router);

        std::string resolved_endpoint;
        TEST_ASSERT_SUCCESS_ERRNO (
          zlink::discovery_owned_service::register_endpoint (
            static_cast<zlink::discovery_t *> (provider_discovery), router_endpoint, &resolved_endpoint, NULL,
            service_role_router));
    }

    TEST_ASSERT_TRUE (wait_for_service_summary_count_local (
      consumer_discovery, service_role_router, routers.size (), 5000));
    TEST_ASSERT_TRUE (wait_for_service_attachment_shape_local (
      node, "spot-auto-router-n", static_cast<uint32_t> (routers.size ()), 0, 0,
      5000));
    TEST_ASSERT_TRUE (wait_for_router_set_distribution_local (
      spot, "spot-auto-router-n", routers, 5000));

    for (size_t i = 0; i < routers.size (); ++i)
        close_zero_linger (routers[i]);
    destroy_multi_service_fixture_local (&spot, &node, &consumer_discovery,
                                         &provider_discovery, &registry, ctx);
}
}

int main (void)
{
    setup_test_environment ();

    UNITY_BEGIN ();
    RUN_TEST (test_spot_service_discovery_replays_existing_filters_end_to_end);
    RUN_TEST (test_spot_node_provider_registers_default_weight);
    RUN_TEST (test_spot_service_discovery_replays_filters_after_pubsub_churn);
    RUN_TEST (
      test_spot_service_discovery_multi_router_distributes_across_candidates);
    RUN_TEST (
      test_spot_service_discovery_many_router_candidates_attach_and_distribute);
    return UNITY_END ();
}
