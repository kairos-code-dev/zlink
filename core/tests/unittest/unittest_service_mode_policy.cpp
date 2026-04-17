/* SPDX-License-Identifier: MPL-2.0 */

#include "../testutil.hpp"
#include "../testutil_unity.hpp"

#include "services/discovery/discovery_protocol.hpp"
#include "services/discovery/discovery_owned_service.hpp"

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

void noop_send_ready_handler (void *, void *)
{
}

void noop_reply_handler (zlink_request_result_t,
                         zlink_msg_t *parts_,
                         size_t part_count_,
                         void *)
{
    zlink_multipart_close (parts_, part_count_);
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

bool bind_registry_test_endpoints (void *registry_,
                                   std::string *pub_out_,
                                   std::string *router_out_)
{
    const int base_port = 37000 + (current_process_id () % 1000) * 8;
    for (int i = 0; i < 64; ++i) {
        std::ostringstream pub_endpoint;
        std::ostringstream router_endpoint;
        pub_endpoint << "tcp://127.0.0.1:" << (base_port + i * 2);
        router_endpoint << "tcp://127.0.0.1:" << (base_port + i * 2 + 1);
        if (zlink_registry_bind (
              registry_, pub_endpoint.str ().c_str (),
              router_endpoint.str ().c_str ())
            == ZLINK_BIND_OK) {
            if (pub_out_)
                *pub_out_ = pub_endpoint.str ();
            if (router_out_)
                *router_out_ = router_endpoint.str ();
            return true;
        }
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

bool wait_for_service_summary_count_local (void *discovery_,
                                           uint16_t service_role_,
                                           size_t expected_count_,
                                           int timeout_ms_)
{
    const int attempts = timeout_ms_ / 25;
    for (int i = 0; i < attempts; ++i) {
        zlink_member_peer_entry_t entries[8];
        size_t count = 8;
        if (zlink_discovery_member_peers (discovery_, entries, &count)
              == ZLINK_CONFIG_OK) {
            size_t matched = 0;
            for (size_t j = 0; j < count; ++j) {
                if (entries[j].service_role == service_role_)
                    ++matched;
            }
            if (matched >= expected_count_)
                return true;
        }
        msleep (25);
    }
    return false;
}

bool wait_for_subscribe_recv_local (void *subject_,
                                    char *topic_out_,
                                    size_t *topic_len_out_,
                                    zlink_msg_t **parts_out_,
                                    size_t *part_count_out_,
                                    int timeout_ms_)
{
    const int attempts = timeout_ms_ / 25;
    for (int i = 0; i < attempts; ++i) {
        if (zlink_subscribe (subject_, NULL, parts_out_, part_count_out_,
                             topic_out_, topic_len_out_,
                             static_cast<zlink_recv_flags_t> (ZLINK_DONTWAIT))
            == ZLINK_RECV_OK) {
            return true;
        }
        msleep (25);
    }
    return false;
}

bool wait_for_service_attachment_count_local (void *node_,
                                              size_t expected_count_,
                                              int timeout_ms_)
{
    const int attempts = timeout_ms_ / 25;
    for (int i = 0; i < attempts; ++i) {
        size_t count = 0;
        if (zlink_spot_node_service_attachment_count (node_, &count)
              == ZLINK_CONFIG_OK
            && count == expected_count_) {
            return true;
        }
        msleep (25);
    }
    return false;
}

bool wait_for_service_attachment_shape_local (void *node_,
                                              const char *service_name_,
                                              uint32_t auto_router_count_,
                                              uint32_t auto_pub_count_,
                                              uint32_t auto_sub_count_,
                                              int timeout_ms_)
{
    const int attempts = timeout_ms_ / 25;
    for (int i = 0; i < attempts; ++i) {
        size_t count = 0;
        if (zlink_spot_node_service_attachment_count (node_, &count)
            == ZLINK_CONFIG_OK) {
            for (size_t j = 0; j < count; ++j) {
                zlink_spot_service_attachment_stats_t row;
                memset (&row, 0, sizeof (row));
                if (zlink_spot_node_service_attachment_at (node_, j, &row)
                      != ZLINK_CONFIG_OK) {
                    continue;
                }
                if (strcmp (row.service_name, service_name_) != 0)
                    continue;
                if (row.auto_router_count == auto_router_count_
                    && row.auto_pub_count == auto_pub_count_
                    && row.auto_sub_count == auto_sub_count_) {
                    return true;
                }
            }
        }
        msleep (25);
    }
    return false;
}

bool wait_for_service_monitor_event_local (
  void *node_,
  zlink_spot_service_monitor_event_t *out_,
  int timeout_ms_)
{
    const int attempts = timeout_ms_ / 25;
    for (int i = 0; i < attempts; ++i) {
        if (zlink_spot_node_monitor_recv (
              node_, out_,
              static_cast<zlink_recv_flags_t> (ZLINK_DONTWAIT))
            == ZLINK_RECV_OK) {
            return true;
        }
        msleep (25);
    }
    return false;
}

bool wait_for_subscription_event_local (void *xpub_,
                                        const char *expected_topic_,
                                        int *subscribed_out_,
                                        int timeout_ms_)
{
    const int attempts = timeout_ms_ / 25;
    for (int i = 0; i < attempts; ++i) {
        zlink_routing_id_t source_rid;
        char topic[64];
        size_t topic_len = sizeof (topic);
        int subscribed = 0;
        memset (&source_rid, 0, sizeof (source_rid));
        if (zlink_subscription_event (
              xpub_, &source_rid, &subscribed, topic, &topic_len, ZLINK_DONTWAIT)
            == ZLINK_RECV_OK) {
            if (topic_len == std::strlen (expected_topic_)
                && memcmp (topic, expected_topic_, topic_len) == 0) {
                if (subscribed_out_)
                    *subscribed_out_ = subscribed;
                return true;
            }
        }
        msleep (25);
    }
    return false;
}

bool wait_for_router_payload_local (void *router_,
                                    const char *expected_payload_,
                                    int timeout_ms_)
{
    const int attempts = timeout_ms_ / 25;
    for (int i = 0; i < attempts; ++i) {
        const zlink_routing_id_t *source_node_rid = NULL;
        const zlink_routing_id_t *source_spot_rid = NULL;
        uint64_t request_seq = 0;
        zlink_msg_t *parts = NULL;
        size_t part_count = 0;
        if (zlink_router_recv (router_, &source_node_rid, &source_spot_rid,
                               &request_seq, &parts, &part_count, ZLINK_DONTWAIT)
            == ZLINK_RECV_OK) {
            const bool matched =
              part_count == 1
              && zlink_msg_size (&parts[0]) == std::strlen (expected_payload_)
              && memcmp (zlink_msg_data (&parts[0]), expected_payload_,
                         std::strlen (expected_payload_))
                   == 0;
            zlink_multipart_close (parts, part_count);
            if (matched)
                return true;
        }
        msleep (25);
    }
    return false;
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

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_poller_add (poller, spot, spot, ZLINK_POLLOUT));

    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    char topic[64];
    size_t topic_len = sizeof (topic);
    TEST_ASSERT_EQUAL_INT (
      ZLINK_RECV_NO_DATA,
      zlink_subscribe (spot, NULL, &parts, &part_count, topic, &topic_len,
                       ZLINK_DONTWAIT));
    TEST_ASSERT_EQUAL_INT (EAGAIN, zlink_errno ());

    topic_len = sizeof (topic);
    TEST_ASSERT_EQUAL_INT (
      ZLINK_RECV_NO_DATA,
      zlink_subscribe (spot, NULL, &parts, &part_count, topic, &topic_len,
                       ZLINK_DONTWAIT));
    TEST_ASSERT_EQUAL_INT (EAGAIN, zlink_errno ());

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_poller_add (poller, spot, spot, ZLINK_POLLIN));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_poller_remove (poller, spot));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_poller_add (poller, spot, spot, ZLINK_POLLOUT));

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

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_poller_add (poller, stream, stream, ZLINK_POLLOUT));

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
    TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_NOT_SUPPORTED, zlink_send (node, &part, 1, 0));
    TEST_ASSERT_EQUAL_INT (ENOTSUP, zlink_errno ());
    TEST_ASSERT_NOT_EQUAL (
      ZLINK_SUBMIT_OK, zlink_send_rid (node, &routing_id, &routed_part, 1, 0));
    TEST_ASSERT_EQUAL_INT (ENOTSUP, zlink_errno ());
    zlink_msg_close (&routed_part);
    zlink_msg_close (&part);
    TEST_ASSERT_EQUAL_INT (
      ZLINK_RECV_NO_DATA,
      zlink_subscribe (node, NULL, &parts, &part_count, topic, &topic_len,
                       ZLINK_DONTWAIT));
    TEST_ASSERT_EQUAL_INT (EAGAIN, zlink_errno ());
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

void test_spot_node_discovery_attach_limits_service_aware_facades ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *discovery =
      zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_SPOT, "spot-svc");
    TEST_ASSERT_NOT_NULL (discovery);

    void *node = zlink_spot_node_new (ctx);
    TEST_ASSERT_NOT_NULL (node);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_attach_discovery (node, discovery));

    void *spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (spot);

    errno = 0;
    TEST_ASSERT_NULL (zlink_spot_new (node));
    TEST_ASSERT_EQUAL_INT (EBUSY, zlink_errno ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_spot_node_attach_discovery_rejects_preexisting_multiple_facades ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *discovery =
      zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_SPOT, "spot-svc");
    TEST_ASSERT_NOT_NULL (discovery);

    void *node = zlink_spot_node_new (ctx);
    TEST_ASSERT_NOT_NULL (node);

    void *spot_a = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (spot_a);
    void *spot_b = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (spot_b);

    errno = 0;
    TEST_ASSERT_NOT_EQUAL (
      ZLINK_CONFIG_OK, zlink_spot_node_attach_discovery (node, discovery));
    TEST_ASSERT_EQUAL_INT (EBUSY, zlink_errno ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot_b));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot_a));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_spot_node_attach_discovery_rejects_duplicate_discovery ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *discovery =
      zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_SPOT, "spot-svc");
    TEST_ASSERT_NOT_NULL (discovery);

    void *node = zlink_spot_node_new (ctx);
    TEST_ASSERT_NOT_NULL (node);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_attach_discovery (node, discovery));

    errno = 0;
    TEST_ASSERT_NOT_EQUAL (
      ZLINK_CONFIG_OK, zlink_spot_node_attach_discovery (node, discovery));
    TEST_ASSERT_EQUAL_INT (EBUSY, zlink_errno ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_spot_publish_service_name_matches_attached_discovery ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *discovery =
      zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_SPOT, "spot-svc");
    TEST_ASSERT_NOT_NULL (discovery);

    void *node = zlink_spot_node_new (ctx);
    TEST_ASSERT_NOT_NULL (node);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_attach_discovery (node, discovery));

    void *spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (spot);

    zlink_msg_t part;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&part, 4));
    memcpy (zlink_msg_data (&part), "ping", 4);

    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_OK,
      zlink_spot_publish (spot, "spot-svc", "bench", &part, 1,
                          ZLINK_SEND_FLAGS_NONE));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_spot_publish_service_name_rejects_mismatch ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *discovery =
      zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_SPOT, "spot-svc");
    TEST_ASSERT_NOT_NULL (discovery);

    void *node = zlink_spot_node_new (ctx);
    TEST_ASSERT_NOT_NULL (node);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_attach_discovery (node, discovery));

    void *spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (spot);

    zlink_msg_t part;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&part, 4));
    memcpy (zlink_msg_data (&part), "ping", 4);

    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_NOT_FOUND,
      zlink_spot_publish (spot, "other-svc", "bench", &part, 1,
                          ZLINK_SEND_FLAGS_NONE));
    TEST_ASSERT_EQUAL_INT (ENOENT, zlink_errno ());
    zlink_msg_close (&part);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_spot_node_manual_service_attachment_limits_service_aware_facades ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *node = zlink_spot_node_new (ctx);
    TEST_ASSERT_NOT_NULL (node);
    void *spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (spot);

    void *router = zlink_socket (ctx, ZLINK_SOCKET_ROUTER);
    TEST_ASSERT_NOT_NULL (router);
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK,
      zlink_spot_node_attach_router (node, "svc-router", router));

    errno = 0;
    TEST_ASSERT_NULL (zlink_spot_new (node));
    TEST_ASSERT_EQUAL_INT (EBUSY, zlink_errno ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    close_zero_linger (router);
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

void test_spot_node_attach_discovery_rejects_unsupported_socket_role ()
{
    using namespace zlink::discovery_owned_service;
    using namespace zlink::discovery_protocol;

    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *node = zlink_spot_node_new (ctx);
    TEST_ASSERT_NOT_NULL (node);

    void *registry = zlink_registry_new (ctx);
    TEST_ASSERT_NOT_NULL (registry);

    std::string registry_pub_endpoint;
    std::string registry_router_endpoint;
    TEST_ASSERT_TRUE (bind_registry_test_endpoints (
      registry, &registry_pub_endpoint, &registry_router_endpoint));

    void *discovery =
      zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_SOCKET, "svc-dealer");
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_TRUE (connect_discovery_registry_with_retry_local (
      discovery, registry_router_endpoint.c_str (), 3000));

    std::string registered_endpoint;
    TEST_ASSERT_SUCCESS_ERRNO (register_endpoint (
      static_cast<zlink::discovery_t *> (discovery), service_type_socket,
      "tcp://127.0.0.1:39111", &registered_endpoint, NULL, service_role_dealer));

    TEST_ASSERT_NOT_EQUAL (
      ZLINK_CONFIG_OK, zlink_spot_node_attach_discovery (node, discovery));
    TEST_ASSERT_EQUAL_INT (ENOTSUP, zlink_errno ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
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

void test_spot_node_manual_service_attachment_snapshot ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *node = zlink_spot_node_new (ctx);
    TEST_ASSERT_NOT_NULL (node);
    void *spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (spot);

    void *router = zlink_socket (ctx, ZLINK_SOCKET_ROUTER);
    void *pub = zlink_socket (ctx, ZLINK_SOCKET_PUB);
    void *sub = zlink_socket (ctx, ZLINK_SOCKET_SUB);
    TEST_ASSERT_NOT_NULL (router);
    TEST_ASSERT_NOT_NULL (pub);
    TEST_ASSERT_NOT_NULL (sub);

    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK,
      zlink_spot_node_attach_router (node, "svc-router", router));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK,
      zlink_spot_node_attach_pubsub (node, "svc-pubsub", pub, sub));

    size_t count = 0;
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK,
      zlink_spot_node_service_attachment_count (node, &count));
    TEST_ASSERT_EQUAL_UINT64 (2, static_cast<unsigned long long> (count));

    zlink_spot_service_attachment_stats_t row0;
    zlink_spot_service_attachment_stats_t row1;
    memset (&row0, 0, sizeof (row0));
    memset (&row1, 0, sizeof (row1));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK, zlink_spot_node_service_attachment_at (node, 0, &row0));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK, zlink_spot_node_service_attachment_at (node, 1, &row1));

    const zlink_spot_service_attachment_stats_t *router_row =
      strcmp (row0.service_name, "svc-router") == 0 ? &row0 : &row1;
    const zlink_spot_service_attachment_stats_t *pubsub_row =
      strcmp (row0.service_name, "svc-pubsub") == 0 ? &row0 : &row1;
    TEST_ASSERT_EQUAL_STRING ("svc-router", router_row->service_name);
    TEST_ASSERT_EQUAL_UINT32 (1, router_row->router_count);
    TEST_ASSERT_EQUAL_UINT32 (0, router_row->pub_count);
    TEST_ASSERT_EQUAL_UINT32 (0, router_row->sub_count);
    TEST_ASSERT_EQUAL_STRING ("svc-pubsub", pubsub_row->service_name);
    TEST_ASSERT_EQUAL_UINT32 (0, pubsub_row->router_count);
    TEST_ASSERT_EQUAL_UINT32 (1, pubsub_row->pub_count);
    TEST_ASSERT_EQUAL_UINT32 (1, pubsub_row->sub_count);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    close_zero_linger (sub);
    close_zero_linger (pub);
    close_zero_linger (router);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_spot_service_pubsub_surface_uses_service_metadata ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *node = zlink_spot_node_new (ctx);
    TEST_ASSERT_NOT_NULL (node);
    void *spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (spot);

    void *pub = zlink_socket (ctx, ZLINK_SOCKET_PUB);
    void *sub = zlink_socket (ctx, ZLINK_SOCKET_SUB);
    TEST_ASSERT_NOT_NULL (pub);
    TEST_ASSERT_NOT_NULL (sub);

    std::string endpoint = bind_socket_test_endpoint (pub);
    TEST_ASSERT_FALSE (endpoint.empty ());
    TEST_ASSERT_EQUAL_INT (ZLINK_CONNECT_OK, zlink_connect (sub, endpoint.c_str ()));

    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK,
      zlink_spot_node_attach_pubsub (node, "svc-alpha", pub, sub));
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK, zlink_set_subscription (spot, "bench"));
    msleep (20);

    zlink_msg_t part;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&part, 4));
    memcpy (zlink_msg_data (&part), "pong", 4);
    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_OK,
      zlink_spot_publish (spot, "svc-alpha", "bench", &part, 1,
                          static_cast<zlink_send_flags_t> (0)));
    msleep (50);

    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    zlink_routing_id_t source_rid;
    memset (&source_rid, 0, sizeof (source_rid));
    char service_name[64];
    size_t service_name_len = sizeof (service_name);
    char topic[64];
    size_t topic_len = sizeof (topic);
    TEST_ASSERT_EQUAL_INT (
      ZLINK_RECV_OK,
      zlink_spot_subscribe (spot, &source_rid, &parts, &part_count, service_name,
                            &service_name_len, topic, &topic_len,
                            static_cast<zlink_recv_flags_t> (ZLINK_DONTWAIT)));
    TEST_ASSERT_EQUAL_UINT32 (9, static_cast<uint32_t> (service_name_len));
    TEST_ASSERT_EQUAL_MEMORY ("svc-alpha", service_name, service_name_len);
    TEST_ASSERT_EQUAL_UINT32 (5, static_cast<uint32_t> (topic_len));
    TEST_ASSERT_EQUAL_MEMORY ("bench", topic, topic_len);
    TEST_ASSERT_EQUAL_UINT8 (0, source_rid.size);
    TEST_ASSERT_EQUAL_UINT32 (1, static_cast<uint32_t> (part_count));
    TEST_ASSERT_EQUAL_MEMORY ("pong", zlink_msg_data (&parts[0]), 4);
    zlink_multipart_close (parts, part_count);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    close_zero_linger (sub);
    close_zero_linger (pub);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_spot_service_send_and_request_fail_for_missing_service ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *node = zlink_spot_node_new (ctx);
    TEST_ASSERT_NOT_NULL (node);
    void *spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (spot);

    zlink_msg_t part0;
    zlink_msg_t part1;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&part0, 4));
    memcpy (zlink_msg_data (&part0), "ping", 4);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&part1, 4));
    memcpy (zlink_msg_data (&part1), "ping", 4);

    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_NOT_FOUND,
      zlink_spot_send_service (spot, "missing", &part0, 1, ZLINK_DONTWAIT));
    TEST_ASSERT_EQUAL_INT (ENOENT, zlink_errno ());

    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_NOT_FOUND,
      zlink_spot_request_service (spot, "missing", &part1, 1,
                                  &noop_reply_handler, NULL,
                                  ZLINK_DONTWAIT, 10));
    TEST_ASSERT_EQUAL_INT (ENOENT, zlink_errno ());
    zlink_msg_close (&part0);
    zlink_msg_close (&part1);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_spot_publish_fails_for_missing_or_inactive_service ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *node = zlink_spot_node_new (ctx);
    TEST_ASSERT_NOT_NULL (node);
    void *spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (spot);

    void *router = zlink_socket (ctx, ZLINK_SOCKET_ROUTER);
    TEST_ASSERT_NOT_NULL (router);
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK, zlink_spot_node_attach_router (node, "svc-router", router));

    zlink_msg_t missing_part;
    zlink_msg_t inactive_part;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&missing_part, 4));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&inactive_part, 4));
    memcpy (zlink_msg_data (&missing_part), "ping", 4);
    memcpy (zlink_msg_data (&inactive_part), "ping", 4);

    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_NOT_FOUND,
      zlink_spot_publish (spot, "missing", "bench", &missing_part, 1,
                          ZLINK_DONTWAIT));
    TEST_ASSERT_EQUAL_INT (ENOENT, zlink_errno ());

    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_NOT_CONNECTED,
      zlink_spot_publish (spot, "svc-router", "bench", &inactive_part, 1,
                          ZLINK_DONTWAIT));
    TEST_ASSERT_EQUAL_INT (ENOTCONN, zlink_errno ());

    zlink_msg_close (&missing_part);
    zlink_msg_close (&inactive_part);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    close_zero_linger (router);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_spot_send_service_rejects_inactive_router_attachment ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *node = zlink_spot_node_new (ctx);
    TEST_ASSERT_NOT_NULL (node);
    void *spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (spot);

    void *router = zlink_socket (ctx, ZLINK_SOCKET_ROUTER);
    TEST_ASSERT_NOT_NULL (router);
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK, zlink_spot_node_attach_router (node, "svc-router", router));

    zlink_msg_t part;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&part, 4));
    memcpy (zlink_msg_data (&part), "ping", 4);

    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_NOT_CONNECTED,
      zlink_spot_send_service (spot, "svc-router", &part, 1, ZLINK_DONTWAIT));
    TEST_ASSERT_EQUAL_INT (ENOTCONN, zlink_errno ());

    zlink_msg_close (&part);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    close_zero_linger (router);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_spot_node_rejects_duplicate_socket_service_name_discovery ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *node = zlink_spot_node_new (ctx);
    TEST_ASSERT_NOT_NULL (node);
    void *spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (spot);

    void *discovery_a =
      zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_SOCKET, "svc-alpha");
    void *discovery_b =
      zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_SOCKET, "svc-alpha");
    TEST_ASSERT_NOT_NULL (discovery_a);
    TEST_ASSERT_NOT_NULL (discovery_b);

    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK, zlink_spot_node_attach_discovery (node, discovery_a));
    TEST_ASSERT_NOT_EQUAL (
      ZLINK_CONFIG_OK, zlink_spot_node_attach_discovery (node, discovery_b));
    TEST_ASSERT_EQUAL_INT (EBUSY, zlink_errno ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery_b));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery_a));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_spot_node_monitor_recv_reports_service_and_role ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *node = zlink_spot_node_new (ctx);
    TEST_ASSERT_NOT_NULL (node);
    void *spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (spot);

    void *pub = zlink_socket (ctx, ZLINK_SOCKET_PUB);
    void *sub = zlink_socket (ctx, ZLINK_SOCKET_SUB);
    void *remote_pub = zlink_socket (ctx, ZLINK_SOCKET_PUB);
    TEST_ASSERT_NOT_NULL (pub);
    TEST_ASSERT_NOT_NULL (sub);
    TEST_ASSERT_NOT_NULL (remote_pub);

    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK, zlink_spot_node_attach_pubsub (node, "svc-mon", pub, sub));

    const std::string endpoint = bind_socket_test_endpoint (remote_pub);
    TEST_ASSERT_FALSE (endpoint.empty ());
    TEST_ASSERT_EQUAL_INT (ZLINK_CONNECT_OK, zlink_connect (sub, endpoint.c_str ()));

    zlink_spot_service_monitor_event_t event;
    memset (&event, 0, sizeof (event));
    TEST_ASSERT_TRUE (
      wait_for_service_monitor_event_local (node, &event, 2000));
    TEST_ASSERT_EQUAL_STRING ("svc-mon", event.service_name);
    TEST_ASSERT_EQUAL_INT (ZLINK_SPOT_SERVICE_ATTACHMENT_SUB, event.role);
    TEST_ASSERT_NOT_EQUAL (0u, event.event.event);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    close_zero_linger (remote_pub);
    close_zero_linger (sub);
    close_zero_linger (pub);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_spot_node_discovery_destroy_preserves_manual_monitors ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *node = zlink_spot_node_new (ctx);
    TEST_ASSERT_NOT_NULL (node);
    void *spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (spot);

    void *pub = zlink_socket (ctx, ZLINK_SOCKET_PUB);
    void *sub = zlink_socket (ctx, ZLINK_SOCKET_SUB);
    void *remote_pub = zlink_socket (ctx, ZLINK_SOCKET_PUB);
    TEST_ASSERT_NOT_NULL (pub);
    TEST_ASSERT_NOT_NULL (sub);
    TEST_ASSERT_NOT_NULL (remote_pub);

    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK, zlink_spot_node_attach_pubsub (node, "svc-mixed", pub, sub));

    void *discovery =
      zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_SOCKET, "svc-mixed");
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK, zlink_spot_node_attach_discovery (node, discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));

    const std::string endpoint = bind_socket_test_endpoint (remote_pub);
    TEST_ASSERT_FALSE (endpoint.empty ());
    TEST_ASSERT_EQUAL_INT (ZLINK_CONNECT_OK, zlink_connect (sub, endpoint.c_str ()));

    zlink_spot_service_monitor_event_t event;
    memset (&event, 0, sizeof (event));
    TEST_ASSERT_TRUE (
      wait_for_service_monitor_event_local (node, &event, 2000));
    TEST_ASSERT_EQUAL_STRING ("svc-mixed", event.service_name);
    TEST_ASSERT_EQUAL_INT (ZLINK_SPOT_SERVICE_ATTACHMENT_SUB, event.role);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    close_zero_linger (remote_pub);
    close_zero_linger (sub);
    close_zero_linger (pub);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_spot_node_discovery_destroy_preserves_manual_attachments ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *node = zlink_spot_node_new (ctx);
    TEST_ASSERT_NOT_NULL (node);
    void *spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (spot);

    void *router = zlink_socket (ctx, ZLINK_SOCKET_ROUTER);
    TEST_ASSERT_NOT_NULL (router);
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK,
      zlink_spot_node_attach_router (node, "svc-manual", router));

    void *discovery =
      zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_SOCKET, "svc-auto");
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK, zlink_spot_node_attach_discovery (node, discovery));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));

    zlink_msg_t part;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&part, 4));
    memcpy (zlink_msg_data (&part), "ping", 4);
    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_NOT_CONNECTED,
      zlink_spot_send_service (spot, "svc-manual", &part, 1, ZLINK_DONTWAIT));
    TEST_ASSERT_EQUAL_INT (ENOTCONN, zlink_errno ());
    zlink_msg_close (&part);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    close_zero_linger (router);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_spot_node_accepts_multiple_socket_service_discoveries ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *node = zlink_spot_node_new (ctx);
    TEST_ASSERT_NOT_NULL (node);
    void *spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (spot);

    void *discovery_alpha =
      zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_SOCKET, "svc-alpha");
    void *discovery_beta =
      zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_SOCKET, "svc-beta");
    TEST_ASSERT_NOT_NULL (discovery_alpha);
    TEST_ASSERT_NOT_NULL (discovery_beta);

    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK,
      zlink_spot_node_attach_discovery (node, discovery_alpha));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK,
      zlink_spot_node_attach_discovery (node, discovery_beta));

    size_t count = 0;
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK,
      zlink_spot_node_service_attachment_count (node, &count));
    TEST_ASSERT_EQUAL_UINT32 (2, static_cast<uint32_t> (count));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery_alpha));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery_beta));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
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
    RUN_TEST (test_spot_node_discovery_attach_limits_service_aware_facades);
    RUN_TEST (test_spot_node_attach_discovery_rejects_preexisting_multiple_facades);
    RUN_TEST (test_spot_node_attach_discovery_rejects_duplicate_discovery);
    RUN_TEST (test_spot_publish_service_name_matches_attached_discovery);
    RUN_TEST (test_spot_publish_service_name_rejects_mismatch);
    RUN_TEST (test_spot_node_manual_service_attachment_limits_service_aware_facades);
    RUN_TEST (test_spot_node_manual_service_attachment_snapshot);
    RUN_TEST (test_spot_service_pubsub_surface_uses_service_metadata);
    RUN_TEST (test_spot_service_send_and_request_fail_for_missing_service);
    RUN_TEST (test_spot_publish_fails_for_missing_or_inactive_service);
    RUN_TEST (test_spot_send_service_rejects_inactive_router_attachment);
    RUN_TEST (test_spot_node_rejects_duplicate_socket_service_name_discovery);
    RUN_TEST (test_spot_node_monitor_recv_reports_service_and_role);
    RUN_TEST (test_spot_node_discovery_destroy_preserves_manual_monitors);
    RUN_TEST (test_spot_node_discovery_destroy_preserves_manual_attachments);
    RUN_TEST (test_spot_node_accepts_multiple_socket_service_discoveries);
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
    RUN_TEST (test_spot_node_attach_discovery_rejects_unsupported_socket_role);
    RUN_TEST (test_discovery_resolve_spot_rejects_invalid_arguments);
    RUN_TEST (test_discovery_resolve_spot_rejects_socket_service_view);
    RUN_TEST (test_stream_send_ready_is_independent_from_recv_callback);
    RUN_TEST (test_generic_monitor_poller_accepts_non_pollin_events);

    return UNITY_END ();
}
