/* SPDX-License-Identifier: MPL-2.0 */

#include "../testutil.hpp"
#include "../testutil_unity.hpp"

#include "services/discovery/discovery_protocol.hpp"

#include <cerrno>
#include <cstring>
#include <sstream>
#include <string>

#include <unity.h>

#if !defined(_WIN32)
#include <unistd.h>
#else
#include <process.h>
#endif

void setUp ()
{
}

void tearDown ()
{
}

namespace
{
void noop_socket_handler (const zlink_routing_id_t *,
                          zlink_msg_t *parts_,
                          size_t part_count_,
                          void *)
{
    zlink_multipart_close (parts_, part_count_);
}

void noop_spot_handler (const zlink_routing_id_t *,
                        const char *,
                        size_t,
                        zlink_msg_t *parts_,
                        size_t part_count_,
                        void *)
{
    zlink_multipart_close (parts_, part_count_);
}

void noop_send_ready_handler (void *, void *)
{
}

int current_process_id ()
{
#if !defined(_WIN32)
    return static_cast<int> (getpid ());
#else
    return static_cast<int> (_getpid ());
#endif
}

std::string bind_spot_test_endpoint (void *node_)
{
    const int base_port = 36000 + (current_process_id () % 1000) * 8;
    for (int i = 0; i < 64; ++i) {
        std::ostringstream endpoint;
        endpoint << "tcp://127.0.0.1:" << (base_port + i);
        if (zlink_spot_node_bind (node_, endpoint.str ().c_str ()) == 0)
            return endpoint.str ();
    }
    return std::string ();
}

std::string bind_socket_test_endpoint (void *socket_)
{
    const int base_port = 36500 + (current_process_id () % 1000) * 8;
    for (int i = 0; i < 64; ++i) {
        std::ostringstream endpoint;
        endpoint << "tcp://127.0.0.1:" << (base_port + i);
        if (zlink_bind (socket_, endpoint.str ().c_str ()) == ZLINK_BIND_OK)
            return endpoint.str ();
    }
    return std::string ();
}

void test_spot_callback_policy ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *node = zlink_spot_node_new (ctx);
    TEST_ASSERT_NOT_NULL (node);
    void *spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (spot);

    void *poller = zlink_poller_new ();
    TEST_ASSERT_NOT_NULL (poller);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_send_ready_handler (spot, &noop_send_ready_handler, NULL));

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_poller_add (poller, spot, spot, ZLINK_POLLIN));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_poller_remove (poller, spot));

    TEST_ASSERT_NOT_EQUAL (
      ZLINK_CONFIG_OK, zlink_poller_add (poller, spot, spot, ZLINK_POLLOUT));
    TEST_ASSERT_EQUAL_INT (EBUSY, zlink_errno ());

    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    char topic[64];
    size_t topic_len = sizeof (topic);
    TEST_ASSERT_EQUAL_INT (
      ZLINK_RECV_NO_DATA,
      zlink_subscribe (spot, NULL, &parts, &part_count, topic, &topic_len,
                       ZLINK_DONTWAIT));
    TEST_ASSERT_EQUAL_INT (EAGAIN, zlink_errno ());

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_subscribe_handler (spot, &noop_spot_handler, NULL));

    topic_len = sizeof (topic);
    TEST_ASSERT_EQUAL_INT (
      ZLINK_RECV_BUSY,
      zlink_subscribe (spot, NULL, &parts, &part_count, topic, &topic_len,
                       ZLINK_DONTWAIT));
    TEST_ASSERT_EQUAL_INT (EBUSY, zlink_errno ());

    TEST_ASSERT_NOT_EQUAL (
      ZLINK_CONFIG_OK, zlink_poller_add (poller, spot, spot, ZLINK_POLLIN));
    TEST_ASSERT_EQUAL_INT (EBUSY, zlink_errno ());
    TEST_ASSERT_NOT_EQUAL (
      ZLINK_CONFIG_OK, zlink_poller_add (poller, spot, spot, ZLINK_POLLOUT));
    TEST_ASSERT_EQUAL_INT (EBUSY, zlink_errno ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_poller_destroy (&poller));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_stream_send_ready_is_independent_from_recv_callback ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *stream = zlink_socket (ctx, ZLINK_SOCKET_STREAM);
    TEST_ASSERT_NOT_NULL (stream);

    void *poller = zlink_poller_new ();
    TEST_ASSERT_NOT_NULL (poller);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_send_ready_handler (stream, &noop_send_ready_handler, NULL));

    TEST_ASSERT_NOT_EQUAL (
      ZLINK_CONFIG_OK, zlink_poller_add (poller, stream, stream, ZLINK_POLLOUT));
    TEST_ASSERT_EQUAL_INT (EBUSY, zlink_errno ());

    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    TEST_ASSERT_EQUAL_INT (
      ZLINK_RECV_NO_DATA,
      zlink_recv (stream, NULL, &parts, &part_count, ZLINK_DONTWAIT));
    TEST_ASSERT_EQUAL_INT (EAGAIN, zlink_errno ());

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_recv_handler (stream, &noop_socket_handler, NULL));
    TEST_ASSERT_NOT_EQUAL (
      ZLINK_CONFIG_OK, zlink_poller_add (poller, stream, stream, ZLINK_POLLIN));
    TEST_ASSERT_EQUAL_INT (EBUSY, zlink_errno ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_poller_destroy (&poller));
    close_zero_linger (stream);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_generic_monitor_poller_accepts_non_pollin_events ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *server = zlink_socket (ctx, ZLINK_SOCKET_PAIR);
    TEST_ASSERT_NOT_NULL (server);
    void *client = zlink_socket (ctx, ZLINK_SOCKET_PAIR);
    TEST_ASSERT_NOT_NULL (client);

    zlink_socket_monitor_open_options_t socket_monitor_opts;
    memset (&socket_monitor_opts, 0, sizeof (socket_monitor_opts));
    socket_monitor_opts.events = ZLINK_EVENT_ALL;
    void *monitor = zlink_socket_monitor_open (server, &socket_monitor_opts);
    TEST_ASSERT_NOT_NULL (monitor);

    void *poller = zlink_poller_new ();
    TEST_ASSERT_NOT_NULL (poller);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_poller_add (poller, monitor, monitor, ZLINK_POLLOUT));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_poller_modify (poller, monitor, ZLINK_POLLOUT));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_poller_remove (poller, monitor));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_poller_destroy (&poller));
    close_zero_linger (client);
    close_zero_linger (server);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_monitor_close (&monitor));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_spot_node_topic_surface_and_callback_modes ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *node = zlink_spot_node_new (ctx);
    TEST_ASSERT_NOT_NULL (node);

    void *poller = zlink_poller_new ();
    TEST_ASSERT_NOT_NULL (poller);

    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    char topic[32];
    size_t topic_len = sizeof (topic);
    zlink_msg_t part;
    zlink_msg_t routed_part;
    zlink_routing_id_t routing_id;
    memset (&routing_id, 0, sizeof (routing_id));
    routing_id.size = 4;
    routing_id.data[0] = 'r';
    routing_id.data[1] = 'o';
    routing_id.data[2] = 'u';
    routing_id.data[3] = 't';

    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&part, 4));
    memcpy (zlink_msg_data (&part), "pong", 4);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&routed_part, 4));
    memcpy (zlink_msg_data (&routed_part), "pong", 4);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_subscription (node, "bench"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_unset_subscription (node, "bench"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_send_ready_handler (node, &noop_send_ready_handler, NULL));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_subscribe_handler (node, &noop_spot_handler, NULL));
    TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_NOT_SUPPORTED, zlink_send (node, &part, 1, 0));
    TEST_ASSERT_EQUAL_INT (ENOTSUP, zlink_errno ());
    TEST_ASSERT_NOT_EQUAL (
      ZLINK_SUBMIT_OK, zlink_send_rid (node, &routing_id, &routed_part, 1, 0));
    TEST_ASSERT_EQUAL_INT (ENOTSUP, zlink_errno ());
    zlink_msg_close (&routed_part);
    zlink_msg_close (&part);
    TEST_ASSERT_EQUAL_INT (
      ZLINK_RECV_BUSY,
      zlink_subscribe (node, NULL, &parts, &part_count, topic, &topic_len,
                       ZLINK_DONTWAIT));
    TEST_ASSERT_EQUAL_INT (EBUSY, zlink_errno ());
    TEST_ASSERT_NOT_EQUAL (
      ZLINK_CONFIG_OK, zlink_poller_add (poller, node, node, ZLINK_POLLIN));
    TEST_ASSERT_EQUAL_INT (ENOTSUP, zlink_errno ());
    TEST_ASSERT_NOT_EQUAL (
      ZLINK_CONFIG_OK, zlink_poller_add (poller, node, node, ZLINK_POLLOUT));
    TEST_ASSERT_EQUAL_INT (ENOTSUP, zlink_errno ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_poller_destroy (&poller));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_spot_tls_configuration_is_node_owned ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *node = zlink_spot_node_new (ctx);
    TEST_ASSERT_NOT_NULL (node);
    void *spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (spot);

    errno = 0;
    TEST_ASSERT_NOT_EQUAL (
      ZLINK_CONFIG_OK, zlink_set_tls_server (spot, "server.crt", "server.key", 0));
    TEST_ASSERT_EQUAL_INT (ENOTSUP, zlink_errno ());

    errno = 0;
    TEST_ASSERT_NOT_EQUAL (
      ZLINK_CONFIG_OK, zlink_set_tls_client (spot, "ca.crt", "localhost", 0));
    TEST_ASSERT_EQUAL_INT (ENOTSUP, zlink_errno ());

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_tls_server (node, "server.crt", "server.key", 0));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_tls_client (node, "ca.crt", "localhost", 0));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_discovery_protocol_accepts_socket_family_and_roles ()
{
    using namespace zlink::discovery_protocol;

    TEST_ASSERT_TRUE (is_valid_service_type (service_type_spot_node));
    TEST_ASSERT_TRUE (is_valid_service_type (service_type_socket));
    TEST_ASSERT_FALSE (is_valid_service_type (0));

    TEST_ASSERT_EQUAL_UINT16 (service_role_spot,
                              fixed_service_role_for_type (
                                service_type_spot_node));
    TEST_ASSERT_EQUAL_UINT16 (service_role_invalid,
                              fixed_service_role_for_type (
                                service_type_socket));

    TEST_ASSERT_TRUE (is_valid_service_role_for_type (service_type_spot_node,
                                                      service_role_spot));
    TEST_ASSERT_FALSE (is_valid_service_role_for_type (
      service_type_spot_node, service_role_pub));
    TEST_ASSERT_TRUE (is_valid_service_role_for_type (service_type_socket,
                                                      service_role_router));
    TEST_ASSERT_TRUE (is_valid_service_role_for_type (service_type_socket,
                                                      service_role_dealer));
    TEST_ASSERT_TRUE (is_valid_service_role_for_type (service_type_socket,
                                                      service_role_pub));
    TEST_ASSERT_TRUE (is_valid_service_role_for_type (service_type_socket,
                                                      service_role_sub));
}

void test_discovery_protocol_derives_socket_roles_and_matching ()
{
    using namespace zlink::discovery_protocol;

    TEST_ASSERT_EQUAL_UINT16 (service_role_router,
                              derive_socket_service_role (
                                ZLINK_CORE_SOCKET_ROUTER));
    TEST_ASSERT_EQUAL_UINT16 (service_role_dealer,
                              derive_socket_service_role (
                                ZLINK_CORE_SOCKET_DEALER));
    TEST_ASSERT_EQUAL_UINT16 (service_role_pub,
                              derive_socket_service_role (
                                ZLINK_CORE_SOCKET_PUB));
    TEST_ASSERT_EQUAL_UINT16 (service_role_sub,
                              derive_socket_service_role (
                                ZLINK_CORE_SOCKET_SUB));
    TEST_ASSERT_EQUAL_UINT16 (service_role_invalid,
                              derive_socket_service_role (
                                ZLINK_CORE_SOCKET_PAIR));

    TEST_ASSERT_TRUE (
      service_roles_match (service_role_spot, service_role_spot));
    TEST_ASSERT_TRUE (
      service_roles_match (service_role_pub, service_role_sub));
    TEST_ASSERT_TRUE (
      service_roles_match (service_role_sub, service_role_pub));
    TEST_ASSERT_TRUE (
      service_roles_match (service_role_router, service_role_router));
    TEST_ASSERT_TRUE (
      service_roles_match (service_role_router, service_role_dealer));
    TEST_ASSERT_TRUE (
      service_roles_match (service_role_dealer, service_role_router));
    TEST_ASSERT_TRUE (
      service_roles_match (service_role_dealer, service_role_dealer));
    TEST_ASSERT_FALSE (
      service_roles_match (service_role_pub, service_role_pub));
    TEST_ASSERT_FALSE (
      service_roles_match (service_role_sub, service_role_sub));
    TEST_ASSERT_FALSE (
      service_roles_match (service_role_pub, service_role_router));
}

void test_discovery_protocol_applies_socket_auto_connect_policy ()
{
    using namespace zlink::discovery_protocol;

    TEST_ASSERT_TRUE (socket_auto_connect_target_matches (
      service_role_router, service_role_router,
      ZLINK_DISCOVERY_DEALER_PEER_MODE_ROUTER));
    TEST_ASSERT_FALSE (socket_auto_connect_target_matches (
      service_role_router, service_role_dealer,
      ZLINK_DISCOVERY_DEALER_PEER_MODE_ROUTER));

    TEST_ASSERT_TRUE (socket_auto_connect_target_matches (
      service_role_sub, service_role_pub,
      ZLINK_DISCOVERY_DEALER_PEER_MODE_ROUTER));
    TEST_ASSERT_FALSE (socket_auto_connect_target_matches (
      service_role_sub, service_role_sub,
      ZLINK_DISCOVERY_DEALER_PEER_MODE_ROUTER));
    TEST_ASSERT_FALSE (socket_auto_connect_target_matches (
      service_role_pub, service_role_sub,
      ZLINK_DISCOVERY_DEALER_PEER_MODE_ROUTER));

    TEST_ASSERT_TRUE (socket_auto_connect_target_matches (
      service_role_dealer, service_role_router,
      ZLINK_DISCOVERY_DEALER_PEER_MODE_ROUTER));
    TEST_ASSERT_FALSE (socket_auto_connect_target_matches (
      service_role_dealer, service_role_dealer,
      ZLINK_DISCOVERY_DEALER_PEER_MODE_ROUTER));

    TEST_ASSERT_FALSE (socket_auto_connect_target_matches (
      service_role_dealer, service_role_router,
      ZLINK_DISCOVERY_DEALER_PEER_MODE_DEALER));
    TEST_ASSERT_TRUE (socket_auto_connect_target_matches (
      service_role_dealer, service_role_dealer,
      ZLINK_DISCOVERY_DEALER_PEER_MODE_DEALER));
}

void test_discovery_new_accepts_socket_family ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *discovery =
      zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_SOCKET, "socket-svc");
    TEST_ASSERT_NOT_NULL (discovery);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_socket_attach_discovery_rejects_unsupported_socket_type ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *discovery =
      zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_SOCKET, "socket-svc");
    TEST_ASSERT_NOT_NULL (discovery);

    void *pair = zlink_socket (ctx, ZLINK_SOCKET_PAIR);
    TEST_ASSERT_NOT_NULL (pair);

    TEST_ASSERT_NOT_EQUAL (
      ZLINK_CONFIG_OK, zlink_socket_attach_discovery (pair, discovery));
    TEST_ASSERT_EQUAL_INT (ENOTSUP, zlink_errno ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (pair));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_socket_attach_discovery_gates_manual_peer_apis ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *discovery =
      zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_SOCKET, "socket-svc");
    TEST_ASSERT_NOT_NULL (discovery);

    void *dealer = zlink_socket (ctx, ZLINK_SOCKET_DEALER);
    TEST_ASSERT_NOT_NULL (dealer);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_socket_attach_discovery (dealer, discovery));

    TEST_ASSERT_NOT_EQUAL (
      ZLINK_CONNECT_OK, zlink_connect (dealer, "tcp://127.0.0.1:39001"));
    TEST_ASSERT_EQUAL_INT (EFSM, zlink_errno ());
    TEST_ASSERT_NOT_EQUAL (
      ZLINK_CONNECT_OK, zlink_disconnect (dealer, "tcp://127.0.0.1:39001"));
    TEST_ASSERT_EQUAL_INT (EFSM, zlink_errno ());
    TEST_ASSERT_NOT_EQUAL (
      ZLINK_CONNECT_OK, zlink_unbind (dealer, "tcp://127.0.0.1:39001"));
    TEST_ASSERT_EQUAL_INT (EFSM, zlink_errno ());
    TEST_ASSERT_NOT_EQUAL (ZLINK_CLOSE_OK, zlink_close (dealer));
    TEST_ASSERT_EQUAL_INT (EFSM, zlink_errno ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_socket_attach_discovery_fails_after_bind_without_registry ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *discovery =
      zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_SOCKET, "socket-svc");
    TEST_ASSERT_NOT_NULL (discovery);

    void *router = zlink_socket (ctx, ZLINK_SOCKET_ROUTER);
    TEST_ASSERT_NOT_NULL (router);

    const std::string endpoint = bind_socket_test_endpoint (router);
    TEST_ASSERT_FALSE (endpoint.empty ());

    TEST_ASSERT_NOT_EQUAL (
      ZLINK_CONFIG_OK, zlink_socket_attach_discovery (router, discovery));
    TEST_ASSERT_EQUAL_INT (EAGAIN, zlink_errno ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (router));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_discovery_dealer_peer_mode_defaults_and_accepts_known_modes ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *discovery =
      zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_SOCKET, "socket-svc");
    TEST_ASSERT_NOT_NULL (discovery);

    TEST_ASSERT_EQUAL (
      ZLINK_CONFIG_OK,
      zlink_discovery_set_dealer_peer_mode (
        discovery, ZLINK_DISCOVERY_DEALER_PEER_MODE_ROUTER));
    TEST_ASSERT_EQUAL (
      ZLINK_CONFIG_OK,
      zlink_discovery_set_dealer_peer_mode (
        discovery, ZLINK_DISCOVERY_DEALER_PEER_MODE_DEALER));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_discovery_dealer_peer_mode_rejects_invalid_mode ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *discovery =
      zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_SOCKET, "socket-svc");
    TEST_ASSERT_NOT_NULL (discovery);

    TEST_ASSERT_NOT_EQUAL (
      ZLINK_CONFIG_OK,
      zlink_discovery_set_dealer_peer_mode (
        discovery, static_cast<zlink_discovery_dealer_peer_mode_t> (99)));
    TEST_ASSERT_EQUAL_INT (EINVAL, zlink_errno ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_discovery_dealer_peer_mode_rejects_spot_service_view ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *discovery =
      zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_SPOT, "spot-svc");
    TEST_ASSERT_NOT_NULL (discovery);

    TEST_ASSERT_NOT_EQUAL (
      ZLINK_CONFIG_OK,
      zlink_discovery_set_dealer_peer_mode (
        discovery, ZLINK_DISCOVERY_DEALER_PEER_MODE_DEALER));
    TEST_ASSERT_EQUAL_INT (ENOTSUP, zlink_errno ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_discovery_resolve_spot_rejects_invalid_arguments ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *discovery = zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_SPOT,
                                           "spot-svc");
    TEST_ASSERT_NOT_NULL (discovery);

    zlink_routing_id_t spot_rid;
    memset (&spot_rid, 0, sizeof (spot_rid));
    zlink_routing_id_t owner_node_rid;
    memset (&owner_node_rid, 0, sizeof (owner_node_rid));

    TEST_ASSERT_NOT_EQUAL (
      ZLINK_CONFIG_OK,
      zlink_discovery_resolve_spot (discovery, NULL, &owner_node_rid));
    TEST_ASSERT_EQUAL_INT (EINVAL, zlink_errno ());

    TEST_ASSERT_NOT_EQUAL (
      ZLINK_CONFIG_OK,
      zlink_discovery_resolve_spot (discovery, &spot_rid, &owner_node_rid));
    TEST_ASSERT_EQUAL_INT (EINVAL, zlink_errno ());

    spot_rid.size = 4;
    memcpy (spot_rid.data, "spot", 4);
    TEST_ASSERT_NOT_EQUAL (
      ZLINK_CONFIG_OK,
      zlink_discovery_resolve_spot (discovery, &spot_rid, NULL));
    TEST_ASSERT_EQUAL_INT (EINVAL, zlink_errno ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_discovery_resolve_spot_rejects_socket_service_view ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *discovery =
      zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_SOCKET, "socket-svc");
    TEST_ASSERT_NOT_NULL (discovery);

    zlink_routing_id_t spot_rid;
    memset (&spot_rid, 0, sizeof (spot_rid));
    spot_rid.size = 4;
    memcpy (spot_rid.data, "spot", 4);

    zlink_routing_id_t owner_node_rid;
    memset (&owner_node_rid, 0, sizeof (owner_node_rid));

    TEST_ASSERT_NOT_EQUAL (
      ZLINK_CONFIG_OK,
      zlink_discovery_resolve_spot (discovery, &spot_rid, &owner_node_rid));
    TEST_ASSERT_EQUAL_INT (ENOTSUP, zlink_errno ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}
}

int main (void)
{
    UNITY_BEGIN ();

    setup_test_environment ();

    RUN_TEST (test_spot_callback_policy);
    RUN_TEST (test_spot_node_topic_surface_and_callback_modes);
    RUN_TEST (test_spot_tls_configuration_is_node_owned);
    RUN_TEST (test_discovery_protocol_accepts_socket_family_and_roles);
    RUN_TEST (test_discovery_protocol_derives_socket_roles_and_matching);
    RUN_TEST (test_discovery_protocol_applies_socket_auto_connect_policy);
    RUN_TEST (test_discovery_new_accepts_socket_family);
    RUN_TEST (test_socket_attach_discovery_rejects_unsupported_socket_type);
    RUN_TEST (test_socket_attach_discovery_gates_manual_peer_apis);
    RUN_TEST (test_socket_attach_discovery_fails_after_bind_without_registry);
    RUN_TEST (test_discovery_dealer_peer_mode_defaults_and_accepts_known_modes);
    RUN_TEST (test_discovery_dealer_peer_mode_rejects_invalid_mode);
    RUN_TEST (test_discovery_dealer_peer_mode_rejects_spot_service_view);
    RUN_TEST (test_discovery_resolve_spot_rejects_invalid_arguments);
    RUN_TEST (test_discovery_resolve_spot_rejects_socket_service_view);
    RUN_TEST (test_stream_send_ready_is_independent_from_recv_callback);
    RUN_TEST (test_generic_monitor_poller_accepts_non_pollin_events);

    return UNITY_END ();
}
