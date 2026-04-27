/* SPDX-License-Identifier: MPL-2.0 */

#include "../testutil.hpp"
#include "../testutil_unity.hpp"
#include "spot_multi_service_fixture.hpp"

#include "../../src/services/discovery/discovery_owned_service.hpp"
#include "../../src/services/discovery/discovery_protocol.hpp"

#include <stdio.h>

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
void test_spot_multi_service_discovery_scale_and_churn ()
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
              test_port (6030));
    snprintf (registry_router, sizeof (registry_router), "tcp://127.0.0.1:%d",
              test_port (6031));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_registry_bind (registry, registry_pub, registry_router));

    void *node = zlink_spot_node_new (ctx, NULL);
    TEST_ASSERT_NOT_NULL (node);
    void *spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (spot);

    std::vector<void *> consumer_discoveries;
    std::vector<void *> provider_discoveries;
    std::vector<void *> routers;
    std::vector<std::string> service_names;

    for (int service_index = 0; service_index < 3; ++service_index) {
        char service_name[32];
        snprintf (service_name, sizeof (service_name), "spot-scale-%d",
                  service_index);
        service_names.push_back (service_name);

        void *consumer_discovery =
          zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_SOCKET, service_name);
        void *provider_discovery =
          zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_SOCKET, service_name);
        TEST_ASSERT_NOT_NULL (consumer_discovery);
        TEST_ASSERT_NOT_NULL (provider_discovery);
        TEST_ASSERT_TRUE (connect_discovery_registry_with_retry_local (
          consumer_discovery, registry_router, 3000));
        TEST_ASSERT_TRUE (connect_discovery_registry_with_retry_local (
          provider_discovery, registry_router, 3000));
        TEST_ASSERT_EQUAL_INT (
          ZLINK_CONFIG_OK,
          zlink_spot_node_attach_discovery (node, consumer_discovery));
        consumer_discoveries.push_back (consumer_discovery);
        provider_discoveries.push_back (provider_discovery);

        for (int router_index = 0; router_index < 4; ++router_index) {
            char router_endpoint[MAX_SOCKET_STRING];
            char routing_id[32];
            snprintf (router_endpoint, sizeof (router_endpoint),
                      "tcp://127.0.0.1:%d",
                      test_port (6032 + service_index * 8 + router_index));
            snprintf (routing_id, sizeof (routing_id), "scale-%d-%d",
                      service_index, router_index);

            void *router = zlink_socket (ctx, ZLINK_SOCKET_ROUTER);
            TEST_ASSERT_NOT_NULL (router);
            TEST_ASSERT_SUCCESS_ERRNO (
              zlink_set_routing_id (router, routing_id, strlen (routing_id)));
            TEST_ASSERT_SUCCESS_ERRNO (zlink_bind (router, router_endpoint));
            routers.push_back (router);

            std::string resolved_endpoint;
            TEST_ASSERT_SUCCESS_ERRNO (
              zlink::discovery_owned_service::register_endpoint (
                static_cast<zlink::discovery_t *> (provider_discovery),
                service_type_socket, router_endpoint, &resolved_endpoint, NULL,
                service_role_router));
        }
    }

    for (size_t service_index = 0; service_index < service_names.size ();
         ++service_index) {
        TEST_ASSERT_TRUE (wait_for_service_summary_count_local (
          consumer_discoveries[service_index], service_role_router, 4, 5000));
        TEST_ASSERT_TRUE (wait_for_service_attachment_shape_local (
          node, service_names[service_index].c_str (), 4, 0, 0, 5000));
    }

    for (size_t service_index = 0; service_index < service_names.size ();
         ++service_index) {
        std::vector<void *> service_routers;
        for (size_t router_index = 0; router_index < 4; ++router_index)
            service_routers.push_back (routers[service_index * 4 + router_index]);
        TEST_ASSERT_TRUE (wait_for_router_set_distribution_local (
          spot, service_names[service_index].c_str (), service_routers, 5000));
    }

    for (int round = 0; round < 3; ++round) {
        for (size_t service_index = 0; service_index < provider_discoveries.size ();
             ++service_index) {
            char router_endpoint[MAX_SOCKET_STRING];
            snprintf (router_endpoint, sizeof (router_endpoint),
                      "tcp://127.0.0.1:%d",
                      test_port (6060 + round * 8 + static_cast<int> (service_index)));
            void *router = zlink_socket (ctx, ZLINK_SOCKET_ROUTER);
            TEST_ASSERT_NOT_NULL (router);
            TEST_ASSERT_SUCCESS_ERRNO (zlink_bind (router, router_endpoint));
            routers.push_back (router);

            std::string resolved_endpoint;
            TEST_ASSERT_SUCCESS_ERRNO (
              zlink::discovery_owned_service::register_endpoint (
                static_cast<zlink::discovery_t *> (provider_discoveries[service_index]),
                service_type_socket, router_endpoint, &resolved_endpoint, NULL,
                service_role_router));
            TEST_ASSERT_TRUE (wait_for_service_attachment_shape_local (
              node, service_names[service_index].c_str (), 5, 0, 0, 5000));
            TEST_ASSERT_SUCCESS_ERRNO (
              zlink::discovery_owned_service::unregister_endpoint (
                static_cast<zlink::discovery_t *> (provider_discoveries[service_index]),
                service_type_socket, resolved_endpoint.c_str (), service_role_router));
            TEST_ASSERT_TRUE (wait_for_service_attachment_shape_local (
              node, service_names[service_index].c_str (), 4, 0, 0, 5000));
        }
    }

    for (size_t i = 0; i < routers.size (); ++i)
        close_zero_linger (routers[i]);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot));
    while (!consumer_discoveries.empty ()) {
        void *consumer = consumer_discoveries.back ();
        void *provider = provider_discoveries.back ();
        consumer_discoveries.pop_back ();
        provider_discoveries.pop_back ();
        TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&consumer));
        TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&provider));
    }
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_shutdown (ctx));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}
}

int main (void)
{
    setup_test_environment ();

    UNITY_BEGIN ();
    RUN_TEST (test_spot_multi_service_discovery_scale_and_churn);
    return UNITY_END ();
}
