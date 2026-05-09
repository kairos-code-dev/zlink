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

bool allocate_loopback_tcp_endpoint (char *endpoint_out_, size_t endpoint_size_)
{
    if (!endpoint_out_ || endpoint_size_ == 0) {
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
                snprintf (endpoint_out_, endpoint_size_, "tcp://127.0.0.1:%u",
                          static_cast<unsigned> (ntohs (addr.sin_port)));
                return true;
            }
        }

        close (fd);
    }

    errno = EADDRINUSE;
    return false;
}

std::string bind_spot_test_endpoint (void *node_)
{
    for (int i = 0; i < 64; ++i) {
        char endpoint[128];
        if (allocate_loopback_tcp_endpoint (endpoint, sizeof (endpoint))
            && zlink_spot_node_bind (node_, endpoint) == 0)
            return endpoint;
    }
    return std::string ();
}

std::string bind_socket_test_endpoint (void *socket_)
{
    for (int i = 0; i < 64; ++i) {
        char endpoint[128];
        if (allocate_loopback_tcp_endpoint (endpoint, sizeof (endpoint))
            && zlink_bind (socket_, endpoint) == ZLINK_BIND_OK)
            return endpoint;
    }
    return std::string ();
}

bool bind_registry_test_endpoints (void *registry_,
                                   std::string *pub_out_,
                                   std::string *router_out_)
{
    for (int i = 0; i < 64; ++i) {
        char pub_endpoint[128];
        char router_endpoint[128];
        if (!allocate_loopback_tcp_endpoint (pub_endpoint,
                                             sizeof (pub_endpoint))
            || !allocate_loopback_tcp_endpoint (router_endpoint,
                                                sizeof (router_endpoint))
            || strcmp (pub_endpoint, router_endpoint) == 0) {
            continue;
        }
        if (zlink_registry_bind (registry_, pub_endpoint, router_endpoint)
            == ZLINK_BIND_OK) {
            if (pub_out_)
                *pub_out_ = pub_endpoint;
            if (router_out_)
                *router_out_ = router_endpoint;
            return true;
        }
    }
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

bool wait_for_service_summary_count_local (void *discovery_,
                                           uint16_t service_role_,
                                           size_t expected_count_,
                                           int timeout_ms_)
{
    return zlink_test_wait_until (timeout_ms_, [=] {
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
        return false;
    });
}

bool wait_for_subscribe_recv_local (void *subject_,
                                    char *topic_out_,
                                    size_t *topic_len_out_,
                                    zlink_msg_t **parts_out_,
                                    size_t *part_count_out_,
                                    int timeout_ms_)
{
    return zlink_test_wait_until (timeout_ms_, [=] {
        if (zlink_subscribe (subject_, NULL, parts_out_, part_count_out_,
                             topic_out_, topic_len_out_,
                             static_cast<zlink_recv_flags_t> (ZLINK_DONTWAIT))
            == ZLINK_RECV_OK) {
            return true;
        }
        return false;
    });
}

bool wait_for_subscription_event_local (void *xpub_,
                                        const char *expected_topic_,
                                        int *subscribed_out_,
                                        int timeout_ms_)
{
    return zlink_test_wait_until (timeout_ms_, [=] {
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
        return false;
    });
}

bool wait_for_router_payload_local (void *router_,
                                    const char *expected_payload_,
                                    int timeout_ms_)
{
    return zlink_test_wait_until (timeout_ms_, [=] {
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
        return false;
    });
}

void test_spot_callback_policy ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *node = zlink_spot_node_new (ctx, NULL);
    TEST_ASSERT_NOT_NULL (node);
    void *spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (spot);

    void *poller = zlink_poller_new ();
    TEST_ASSERT_NOT_NULL (poller);

    TEST_ASSERT_EQUAL_INT (
      ZLINK_HANDLER_OK,
      zlink_send_ready_handler (spot, &noop_send_ready_handler, NULL));

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_poller_add (poller, spot, spot, ZLINK_POLLIN));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_poller_remove (poller, spot));

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_poller_add (poller, spot, spot, ZLINK_POLLOUT));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_poller_remove (poller, spot));

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
    TEST_ASSERT_SUCCESS_ERRNO (zlink_poller_remove (poller, spot));

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

    void *node = zlink_spot_node_new (ctx, NULL);
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
    TEST_ASSERT_EQUAL_INT (
      ZLINK_HANDLER_OK,
      zlink_send_ready_handler (node, &noop_send_ready_handler, NULL));
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
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_poller_add (poller, node, node, ZLINK_POLLIN));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_poller_remove (poller, node));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_poller_add (poller, node, node, ZLINK_POLLOUT));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_poller_remove (poller, node));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_poller_destroy (&poller));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_spot_tls_configuration_is_node_owned ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *node = zlink_spot_node_new (ctx, NULL);
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

void test_spot_node_discovery_attach_allows_multiple_spot_facades ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *discovery =
      zlink_discovery_new (ctx, ZLINK_AUTO_CONNECT_SPOT_MESH, "spot-svc");
    TEST_ASSERT_NOT_NULL (discovery);

    void *node = zlink_spot_node_new (ctx, NULL);
    TEST_ASSERT_NOT_NULL (node);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_attach_discovery (node, discovery));

    void *spot_a = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (spot_a);
    void *spot_b = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (spot_b);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot_b));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot_a));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_spot_node_attach_discovery_allows_preexisting_multiple_facades ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *discovery =
      zlink_discovery_new (ctx, ZLINK_AUTO_CONNECT_SPOT_MESH, "spot-svc");
    TEST_ASSERT_NOT_NULL (discovery);

    void *node = zlink_spot_node_new (ctx, NULL);
    TEST_ASSERT_NOT_NULL (node);

    void *spot_a = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (spot_a);
    void *spot_b = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (spot_b);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_attach_discovery (node, discovery));

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
      zlink_discovery_new (ctx, ZLINK_AUTO_CONNECT_SPOT_MESH, "spot-svc");
    TEST_ASSERT_NOT_NULL (discovery);

    void *node = zlink_spot_node_new (ctx, NULL);
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

void test_spot_publish_uses_bound_spot_topic ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *discovery =
      zlink_discovery_new (ctx, ZLINK_AUTO_CONNECT_SPOT_MESH, "spot-svc");
    TEST_ASSERT_NOT_NULL (discovery);

    void *node = zlink_spot_node_new (ctx, NULL);
    TEST_ASSERT_NOT_NULL (node);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_attach_discovery (node, discovery));

    void *spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (spot);

    zlink_msg_t part;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&part, 4));
    memcpy (zlink_msg_data (&part), "ping", 4);

    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_OK,
      zlink_spot_publish (spot, "bench", &part, 1, ZLINK_SEND_FLAGS_NONE));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_spot_publish_part_final_keeps_single_message_contract ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *discovery =
      zlink_discovery_new (ctx, ZLINK_AUTO_CONNECT_SPOT_MESH, "spot-svc");
    TEST_ASSERT_NOT_NULL (discovery);

    void *node = zlink_spot_node_new (ctx, NULL);
    TEST_ASSERT_NOT_NULL (node);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_attach_discovery (node, discovery));

    void *spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (spot);

    zlink_msg_t first;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&first, 4));
    memcpy (zlink_msg_data (&first), "ping", 4);
    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_OK,
      zlink_spot_publish_part (
        spot, "bench", &first, ZLINK_SEND_FLAGS_NONE, ZLINK_PART_FINAL));

    zlink_msg_t second;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&second, 4));
    memcpy (zlink_msg_data (&second), "pong", 4);
    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_OK,
      zlink_spot_publish_part (
        spot, "bench", &second, ZLINK_SEND_FLAGS_NONE, ZLINK_PART_FINAL));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_spot_node_manual_service_attachment_allows_multiple_spot_facades ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *node = zlink_spot_node_new (ctx, NULL);
    TEST_ASSERT_NOT_NULL (node);
    void *spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (spot);

    void *dealer = zlink_socket (ctx, ZLINK_SOCKET_DEALER);
    TEST_ASSERT_NOT_NULL (dealer);
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK,
      zlink_spot_node_attach_channel_dealer_manual (node, "chan-router",
                                                    dealer));

    void *second_spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (second_spot);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&second_spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    close_zero_linger (dealer);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_discovery_protocol_accepts_socket_family_and_roles ()
{
    using namespace zlink::discovery_protocol;

    TEST_ASSERT_TRUE (
      is_valid_auto_connect_type (ZLINK_AUTO_CONNECT_ROUTE_MESH));
    TEST_ASSERT_TRUE (
      is_valid_auto_connect_type (ZLINK_AUTO_CONNECT_CLIENT_SERVER));
    TEST_ASSERT_TRUE (
      is_valid_auto_connect_type (ZLINK_AUTO_CONNECT_DEALER_MESH));
    TEST_ASSERT_TRUE (is_valid_auto_connect_type (ZLINK_AUTO_CONNECT_FANOUT));
    TEST_ASSERT_TRUE (
      is_valid_auto_connect_type (ZLINK_AUTO_CONNECT_SPOT_MESH));
    TEST_ASSERT_FALSE (
      is_valid_auto_connect_type (ZLINK_AUTO_CONNECT_INVALID));

    TEST_ASSERT_TRUE (auto_connect_type_allows_role (
      ZLINK_AUTO_CONNECT_ROUTE_MESH, service_role_router));
    TEST_ASSERT_FALSE (auto_connect_type_allows_role (
      ZLINK_AUTO_CONNECT_ROUTE_MESH, service_role_dealer));
    TEST_ASSERT_TRUE (auto_connect_type_allows_role (
      ZLINK_AUTO_CONNECT_CLIENT_SERVER, service_role_router));
    TEST_ASSERT_TRUE (auto_connect_type_allows_role (
      ZLINK_AUTO_CONNECT_CLIENT_SERVER, service_role_dealer));
    TEST_ASSERT_TRUE (auto_connect_type_allows_role (
      ZLINK_AUTO_CONNECT_DEALER_MESH, service_role_dealer));
    TEST_ASSERT_TRUE (auto_connect_type_allows_role (
      ZLINK_AUTO_CONNECT_FANOUT, service_role_pub));
    TEST_ASSERT_TRUE (auto_connect_type_allows_role (
      ZLINK_AUTO_CONNECT_FANOUT, service_role_sub));
    TEST_ASSERT_TRUE (auto_connect_type_allows_role (
      ZLINK_AUTO_CONNECT_SPOT_MESH, service_role_spot));
    TEST_ASSERT_FALSE (auto_connect_type_allows_role (
      ZLINK_AUTO_CONNECT_SPOT_MESH, service_role_router));
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

    TEST_ASSERT_TRUE (auto_connect_type_allows_raw_socket (
      ZLINK_AUTO_CONNECT_CLIENT_SERVER));
    TEST_ASSERT_FALSE (auto_connect_type_allows_raw_socket (
      ZLINK_AUTO_CONNECT_SPOT_MESH));
}

void test_discovery_protocol_applies_socket_auto_connect_policy ()
{
    using namespace zlink::discovery_protocol;

    zlink_routing_id_t local_rid;
    zlink_routing_id_t remote_rid;
    memset (&local_rid, 0, sizeof (local_rid));
    memset (&remote_rid, 0, sizeof (remote_rid));
    local_rid.size = 1;
    remote_rid.size = 1;
    local_rid.data[0] = 1;
    remote_rid.data[0] = 2;

    TEST_ASSERT_TRUE (socket_auto_connect_target_matches (
      ZLINK_AUTO_CONNECT_ROUTE_MESH, service_role_router, service_role_router,
      local_rid, remote_rid, "tcp://a", "tcp://b"));
    TEST_ASSERT_FALSE (socket_auto_connect_target_matches (
      ZLINK_AUTO_CONNECT_ROUTE_MESH, service_role_router, service_role_dealer,
      local_rid, remote_rid, "tcp://a", "tcp://b"));

    TEST_ASSERT_TRUE (socket_auto_connect_target_matches (
      ZLINK_AUTO_CONNECT_FANOUT, service_role_sub, service_role_pub,
      local_rid, remote_rid, "", "tcp://pub"));
    TEST_ASSERT_FALSE (socket_auto_connect_target_matches (
      ZLINK_AUTO_CONNECT_FANOUT, service_role_sub, service_role_sub,
      local_rid, remote_rid, "", "tcp://sub"));
    TEST_ASSERT_FALSE (socket_auto_connect_target_matches (
      ZLINK_AUTO_CONNECT_FANOUT, service_role_pub, service_role_sub,
      local_rid, remote_rid, "tcp://pub", ""));

    TEST_ASSERT_TRUE (socket_auto_connect_target_matches (
      ZLINK_AUTO_CONNECT_CLIENT_SERVER, service_role_dealer,
      service_role_router, local_rid, remote_rid, "", "tcp://router"));
    TEST_ASSERT_FALSE (socket_auto_connect_target_matches (
      ZLINK_AUTO_CONNECT_CLIENT_SERVER, service_role_dealer,
      service_role_dealer, local_rid, remote_rid, "", "tcp://dealer"));

    TEST_ASSERT_TRUE (socket_auto_connect_target_matches (
      ZLINK_AUTO_CONNECT_DEALER_MESH, service_role_dealer,
      service_role_dealer, local_rid, remote_rid, "tcp://a", "tcp://b"));
}

void test_discovery_new_accepts_socket_family ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *discovery =
      zlink_discovery_new (ctx, ZLINK_AUTO_CONNECT_CLIENT_SERVER, "socket-svc");
    TEST_ASSERT_NOT_NULL (discovery);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_socket_attach_discovery_rejects_unsupported_socket_type ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *discovery =
      zlink_discovery_new (ctx, ZLINK_AUTO_CONNECT_CLIENT_SERVER, "socket-svc");
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
      zlink_discovery_new (ctx, ZLINK_AUTO_CONNECT_CLIENT_SERVER, "socket-svc");
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
      zlink_discovery_new (ctx, ZLINK_AUTO_CONNECT_CLIENT_SERVER, "socket-svc");
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

void test_discovery_new_rejects_invalid_auto_connect_type ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *discovery =
      zlink_discovery_new (ctx, ZLINK_AUTO_CONNECT_INVALID, "socket-svc");
    TEST_ASSERT_NULL (discovery);
    TEST_ASSERT_EQUAL_INT (EINVAL, zlink_errno ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_socket_attach_discovery_rejects_type_role_mismatch ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *discovery =
      zlink_discovery_new (ctx, ZLINK_AUTO_CONNECT_FANOUT, "fanout-svc");
    TEST_ASSERT_NOT_NULL (discovery);
    void *dealer = zlink_socket (ctx, ZLINK_SOCKET_DEALER);
    TEST_ASSERT_NOT_NULL (dealer);

    TEST_ASSERT_NOT_EQUAL (
      ZLINK_CONFIG_OK, zlink_socket_attach_discovery (dealer, discovery));
    TEST_ASSERT_EQUAL_INT (ENOTSUP, zlink_errno ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (dealer));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_spot_node_attach_discovery_rejects_unsupported_socket_role ()
{
    using namespace zlink::discovery_owned_service;
    using namespace zlink::discovery_protocol;

    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *node = zlink_spot_node_new (ctx, NULL);
    TEST_ASSERT_NOT_NULL (node);

    void *registry = zlink_registry_new (ctx);
    TEST_ASSERT_NOT_NULL (registry);

    std::string registry_pub_endpoint;
    std::string registry_router_endpoint;
    TEST_ASSERT_TRUE (bind_registry_test_endpoints (
      registry, &registry_pub_endpoint, &registry_router_endpoint));

    void *discovery =
      zlink_discovery_new (ctx, ZLINK_AUTO_CONNECT_CLIENT_SERVER, "svc-dealer");
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_TRUE (connect_discovery_registry_with_retry_local (
      discovery, registry_router_endpoint.c_str (), 3000));

    std::string registered_endpoint;
    TEST_ASSERT_SUCCESS_ERRNO (register_endpoint (
      static_cast<zlink::discovery_t *> (discovery),
      "tcp://127.0.0.1:39111", &registered_endpoint, NULL,
      service_role_dealer));

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

    void *discovery = zlink_discovery_new (ctx, ZLINK_AUTO_CONNECT_SPOT_MESH,
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
      zlink_discovery_new (ctx, ZLINK_AUTO_CONNECT_CLIENT_SERVER, "socket-svc");
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

void test_spot_service_send_and_request_fail_for_missing_service ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *node = zlink_spot_node_new (ctx, NULL);
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
      zlink_spot_send_channel (spot, "missing", &part0, 1, ZLINK_DONTWAIT));
    TEST_ASSERT_EQUAL_INT (ENOENT, zlink_errno ());

    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_NOT_FOUND,
      zlink_spot_request_channel (spot, "missing", &part1, 1,
                                  &noop_reply_handler, NULL,
                                  ZLINK_DONTWAIT, 10));
    TEST_ASSERT_EQUAL_INT (ENOENT, zlink_errno ());
    zlink_msg_close (&part0);
    zlink_msg_close (&part1);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_spot_send_channel_rejects_inactive_dealer_attachment ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *node = zlink_spot_node_new (ctx, NULL);
    TEST_ASSERT_NOT_NULL (node);
    void *spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (spot);

    void *dealer = zlink_socket (ctx, ZLINK_SOCKET_DEALER);
    TEST_ASSERT_NOT_NULL (dealer);
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK,
      zlink_spot_node_attach_channel_dealer_manual (node, "svc-router",
                                                    dealer));

    zlink_msg_t part;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&part, 4));
    memcpy (zlink_msg_data (&part), "ping", 4);

    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_NOT_CONNECTED,
      zlink_spot_send_channel (spot, "svc-router", &part, 1, ZLINK_DONTWAIT));
    TEST_ASSERT_EQUAL_INT (ENOTCONN, zlink_errno ());

    zlink_msg_close (&part);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    close_zero_linger (dealer);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_spot_node_attach_channel_dealer_manual_rejects_duplicate_channel ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *node = zlink_spot_node_new (ctx, NULL);
    void *dealer_a = zlink_socket (ctx, ZLINK_SOCKET_DEALER);
    void *dealer_b = zlink_socket (ctx, ZLINK_SOCKET_DEALER);
    TEST_ASSERT_NOT_NULL (node);
    TEST_ASSERT_NOT_NULL (dealer_a);
    TEST_ASSERT_NOT_NULL (dealer_b);

    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK,
      zlink_spot_node_attach_channel_dealer_manual (node, "dup-chan",
                                                    dealer_a));
    TEST_ASSERT_NOT_EQUAL (
      ZLINK_CONFIG_OK,
      zlink_spot_node_attach_channel_dealer_manual (node, "dup-chan",
                                                    dealer_b));
    TEST_ASSERT_EQUAL_INT (EBUSY, zlink_errno ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    close_zero_linger (dealer_b);
    close_zero_linger (dealer_a);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_socket_channel_name_metadata_roundtrip ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *dealer = zlink_socket (ctx, ZLINK_SOCKET_DEALER);
    TEST_ASSERT_NOT_NULL (dealer);

    char buf[32];
    size_t len = sizeof (buf);
    TEST_ASSERT_NOT_EQUAL (ZLINK_CONFIG_OK,
                           zlink_socket_get_channel_name (dealer, buf,
                                                          sizeof (buf), &len));
    TEST_ASSERT_EQUAL_INT (ENOENT, zlink_errno ());

    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK,
                           zlink_socket_set_channel_name (dealer,
                                                          "fixed-chan"));

    len = sizeof (buf);
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK,
                           zlink_socket_get_channel_name (dealer, buf,
                                                          sizeof (buf), &len));
    TEST_ASSERT_EQUAL_UINT (10, len);
    TEST_ASSERT_EQUAL_STRING_LEN ("fixed-chan", buf, len);

    close_zero_linger (dealer);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_spot_node_attach_channel_dealer_manual_rejects_channel_name_mismatch ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *node = zlink_spot_node_new (ctx, NULL);
    void *dealer = zlink_socket (ctx, ZLINK_SOCKET_DEALER);
    TEST_ASSERT_NOT_NULL (node);
    TEST_ASSERT_NOT_NULL (dealer);

    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK,
                           zlink_socket_set_channel_name (dealer,
                                                          "preset-chan"));
    TEST_ASSERT_NOT_EQUAL (
      ZLINK_CONFIG_OK,
      zlink_spot_node_attach_channel_dealer_manual (node, "other-chan",
                                                    dealer));
    TEST_ASSERT_EQUAL_INT (EINVAL, zlink_errno ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    close_zero_linger (dealer);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_spot_node_attach_pub_ingress_accepts_single_pub ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *node = zlink_spot_node_new (ctx, NULL);
    void *pub_a = zlink_socket (ctx, ZLINK_SOCKET_PUB);
    void *pub_b = zlink_socket (ctx, ZLINK_SOCKET_PUB);
    TEST_ASSERT_NOT_NULL (node);
    TEST_ASSERT_NOT_NULL (pub_a);
    TEST_ASSERT_NOT_NULL (pub_b);

    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK, zlink_spot_node_attach_pub_ingress (node, pub_a));
    TEST_ASSERT_NOT_EQUAL (
      ZLINK_CONFIG_OK, zlink_spot_node_attach_pub_ingress (node, pub_b));
    TEST_ASSERT_EQUAL_INT (EBUSY, zlink_errno ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    close_zero_linger (pub_b);
    close_zero_linger (pub_a);
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
    RUN_TEST (test_spot_node_discovery_attach_allows_multiple_spot_facades);
    RUN_TEST (test_spot_node_attach_discovery_allows_preexisting_multiple_facades);
    RUN_TEST (test_spot_node_attach_discovery_rejects_duplicate_discovery);
    RUN_TEST (test_spot_publish_uses_bound_spot_topic);
    RUN_TEST (test_spot_publish_part_final_keeps_single_message_contract);
    RUN_TEST (test_spot_node_manual_service_attachment_allows_multiple_spot_facades);
    RUN_TEST (test_spot_service_send_and_request_fail_for_missing_service);
    RUN_TEST (test_spot_send_channel_rejects_inactive_dealer_attachment);
    RUN_TEST (test_spot_node_attach_channel_dealer_manual_rejects_duplicate_channel);
    RUN_TEST (test_socket_channel_name_metadata_roundtrip);
    RUN_TEST (
      test_spot_node_attach_channel_dealer_manual_rejects_channel_name_mismatch);
    RUN_TEST (test_spot_node_attach_pub_ingress_accepts_single_pub);
    RUN_TEST (test_discovery_protocol_accepts_socket_family_and_roles);
    RUN_TEST (test_discovery_protocol_derives_socket_roles_and_matching);
    RUN_TEST (test_discovery_protocol_applies_socket_auto_connect_policy);
    RUN_TEST (test_discovery_new_accepts_socket_family);
    RUN_TEST (test_socket_attach_discovery_rejects_unsupported_socket_type);
    RUN_TEST (test_socket_attach_discovery_gates_manual_peer_apis);
    RUN_TEST (test_socket_attach_discovery_fails_after_bind_without_registry);
    RUN_TEST (test_discovery_new_rejects_invalid_auto_connect_type);
    RUN_TEST (test_socket_attach_discovery_rejects_type_role_mismatch);
    RUN_TEST (test_spot_node_attach_discovery_rejects_unsupported_socket_role);
    RUN_TEST (test_discovery_resolve_spot_rejects_invalid_arguments);
    RUN_TEST (test_discovery_resolve_spot_rejects_socket_service_view);
    RUN_TEST (test_stream_send_ready_is_independent_from_recv_callback);
    RUN_TEST (test_generic_monitor_poller_accepts_non_pollin_events);

    return UNITY_END ();
}
