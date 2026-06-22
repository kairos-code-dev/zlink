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
struct route_reply_probe_t
{
    bool done;
    zlink_request_result_t result;
    std::string payload;
    void *progress_socket;

    route_reply_probe_t () :
        done (false), result (ZLINK_REQUEST_PROTOCOL_ERROR), progress_socket (NULL)
    {
    }
};

void capture_route_reply_local (zlink_request_result_t result_,
                                zlink_msg_t *parts_,
                                size_t part_count_,
                                void *userdata_)
{
    route_reply_probe_t *probe = static_cast<route_reply_probe_t *> (userdata_);
    probe->result = result_;
    probe->done = true;
    if (result_ == ZLINK_REQUEST_OK && part_count_ == 1) {
        probe->payload.assign (static_cast<const char *> (zlink_msg_data (&parts_[0])),
                               zlink_msg_size (&parts_[0]));
    }
}

int drain_completion_via_poller_local (void *subject_)
{
    void *poller = zlink_poller_new ();
    if (!poller)
        return -1;
    int rc = -1;
    if (zlink_poller_add (poller, subject_, NULL, ZLINK_POLLCOMPLETION) == ZLINK_CONFIG_OK) {
        zlink_poller_event_t event;
        rc = zlink_poller_wait (poller, &event, 1, 0, NULL);
        (void) zlink_poller_remove (poller, subject_);
    }
    (void) zlink_poller_destroy (&poller);
    return rc;
}

bool allocate_loopback_tcp_endpoint_local (char *endpoint_out_, size_t endpoint_size_)
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
        setsockopt (fd, SOL_SOCKET, SO_REUSEADDR, as_setsockopt_opt_t (&reuse), sizeof (reuse));

        struct sockaddr_in addr;
        memset (&addr, 0, sizeof (addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl (INADDR_LOOPBACK);
        addr.sin_port = 0;

        if (bind (fd, reinterpret_cast<struct sockaddr *> (&addr), sizeof (addr)) == 0) {
#if defined ZLINK_HAVE_WINDOWS
            int addr_len = sizeof (addr);
#else
            socklen_t addr_len = sizeof (addr);
#endif
            if (getsockname (fd, reinterpret_cast<struct sockaddr *> (&addr), &addr_len) == 0) {
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

bool allocate_loopback_endpoint_local (const char *transport_,
                                       char *endpoint_out_,
                                       size_t endpoint_size_)
{
    char tcp_endpoint[MAX_SOCKET_STRING];
    if (!allocate_loopback_tcp_endpoint_local (tcp_endpoint, sizeof (tcp_endpoint)))
        return false;

    const char tcp_prefix[] = "tcp://";
    if (strncmp (tcp_endpoint, tcp_prefix, strlen (tcp_prefix)) != 0) {
        errno = EINVAL;
        return false;
    }

    const int written = snprintf (endpoint_out_, endpoint_size_, "%s://%s", transport_,
                                  tcp_endpoint + strlen (tcp_prefix));
    if (written < 0 || static_cast<size_t> (written) >= endpoint_size_) {
        errno = ENAMETOOLONG;
        return false;
    }
    return true;
}

bool rewrite_endpoint_host_local (const char *endpoint_,
                                  const char *host_,
                                  char *endpoint_out_,
                                  size_t endpoint_size_)
{
    if (!endpoint_ || !host_ || !endpoint_out_ || endpoint_size_ == 0) {
        errno = EINVAL;
        return false;
    }

    const char *scheme_end = strstr (endpoint_, "://");
    if (!scheme_end) {
        errno = EINVAL;
        return false;
    }

    const char *authority = scheme_end + 3;
    const char *port = strchr (authority, ':');
    if (!port) {
        errno = EINVAL;
        return false;
    }

    const size_t scheme_len = static_cast<size_t> (authority - endpoint_);
    const int written = snprintf (endpoint_out_, endpoint_size_, "%.*s%s%s",
                                  static_cast<int> (scheme_len), endpoint_, host_, port);
    if (written < 0 || static_cast<size_t> (written) >= endpoint_size_) {
        errno = ENAMETOOLONG;
        return false;
    }
    return true;
}

bool bind_registry_test_endpoints_local (void *registry_,
                                         int base_port_,
                                         char *pub_out_,
                                         size_t pub_size_,
                                         char *router_out_,
                                         size_t router_size_)
{
    if (!registry_ || !pub_out_ || !router_out_ || pub_size_ == 0 || router_size_ == 0) {
        errno = EINVAL;
        return false;
    }

    LIBZLINK_UNUSED (base_port_);

    for (int i = 0; i < 256; ++i) {
        if (!allocate_loopback_tcp_endpoint_local (pub_out_, pub_size_)
            || !allocate_loopback_tcp_endpoint_local (router_out_, router_size_)
            || strcmp (pub_out_, router_out_) == 0) {
            continue;
        }
        if (zlink_registry_bind (registry_, pub_out_, router_out_) == ZLINK_BIND_OK)
            return true;
    }

    errno = EADDRINUSE;
    return false;
}

bool bind_registry_test_endpoints_transport_local (void *registry_,
                                                   const char *transport_,
                                                   char *pub_out_,
                                                   size_t pub_size_,
                                                   char *router_out_,
                                                   size_t router_size_)
{
    if (!registry_ || !transport_ || !pub_out_ || !router_out_ || pub_size_ == 0
        || router_size_ == 0) {
        errno = EINVAL;
        return false;
    }

    for (int i = 0; i < 256; ++i) {
        if (!allocate_loopback_endpoint_local (transport_, pub_out_, pub_size_)
            || !allocate_loopback_endpoint_local (transport_, router_out_, router_size_)
            || strcmp (pub_out_, router_out_) == 0) {
            continue;
        }
        if (zlink_registry_bind (registry_, pub_out_, router_out_) == ZLINK_BIND_OK)
            return true;
    }

    errno = EADDRINUSE;
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

    LIBZLINK_UNUSED (base_port_);

    for (int i = 0; i < 256; ++i) {
        if (!allocate_loopback_tcp_endpoint_local (endpoint_out_, endpoint_size_))
            continue;
        if (zlink_bind (socket_, endpoint_out_) == ZLINK_BIND_OK)
            return true;
    }

    errno = EADDRINUSE;
    return false;
}

bool connect_discovery_registry_with_retry_local (void *discovery_,
                                                  const char *endpoint_,
                                                  int timeout_ms_)
{
    return zlink_test_wait_until (timeout_ms_, [=] {
        if (zlink_discovery_connect_registry (discovery_, endpoint_) == ZLINK_CONNECT_OK) {
            return true;
        }
        return false;
    });
}

bool destroy_discovery_with_retry_local (void **discovery_p_, int timeout_ms_)
{
    return zlink_test_wait_until_result (timeout_ms_, [=] {
        if (zlink_discovery_destroy (discovery_p_) == ZLINK_CLOSE_OK)
            return zlink_test_wait_done;
        if (zlink_errno () != EBUSY)
            return zlink_test_wait_failed;
        return zlink_test_wait_retry;
    });
}

bool wait_for_discovery_member_role_count_local (void *discovery_,
                                                 zlink_service_role_t service_role_,
                                                 size_t expected_count_,
                                                 int timeout_ms_)
{
    return zlink_test_wait_until (timeout_ms_, [=] {
        zlink_member_peer_entry_t entries[8];
        size_t count = 8;
        if (zlink_discovery_member_peers (discovery_, entries, &count) == ZLINK_CONFIG_OK) {
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

bool wait_for_topology_entry_local (void *registry_,
                                    const zlink_registry_topology_filter_t *filter_,
                                    zlink_registry_topology_entry_t *entry_out_,
                                    uint32_t desired_count_,
                                    int timeout_ms_)
{
    return zlink_test_wait_until (timeout_ms_, [=] {
        zlink_registry_topology_entry_t entries[8];
        size_t count = 8;
        if (zlink_registry_topology (registry_, filter_, entries, &count) == ZLINK_CONFIG_OK) {
            for (size_t j = 0; j < count; ++j) {
                if (entries[j].desired_count != desired_count_)
                    continue;
                if (entry_out_)
                    *entry_out_ = entries[j];
                return true;
            }
        }
        return false;
    });
}

bool wait_for_registry_member_count_local (void *registry_,
                                           const char *channel_name_,
                                           size_t expected_count_,
                                           int timeout_ms_)
{
    return zlink_test_wait_until (timeout_ms_, [=] {
        zlink_member_peer_entry_t entries[8];
        size_t count = 8;
        memset (entries, 0, sizeof (entries));
        if (zlink_registry_member_peers (registry_, channel_name_, entries, &count)
              == ZLINK_CONFIG_OK
            && count == expected_count_)
            return true;
        return false;
    });
}

bool wait_for_service_summary_count_local (void *registry_,
                                           const zlink_registry_service_summary_filter_t *filter_,
                                           size_t expected_count_,
                                           int timeout_ms_)
{
    return zlink_test_wait_until (timeout_ms_, [=] {
        zlink_registry_service_summary_entry_t entries[8];
        size_t count = 8;
        memset (entries, 0, sizeof (entries));
        if (zlink_registry_service_summary (registry_, filter_, entries, &count) == ZLINK_CONFIG_OK
            && count == expected_count_)
            return true;
        return false;
    });
}

bool wait_for_topology_desired_total_local (void *registry_,
                                            const zlink_registry_topology_filter_t *filter_,
                                            size_t min_count_,
                                            uint32_t expected_desired_total_,
                                            int timeout_ms_)
{
    return zlink_test_wait_until (timeout_ms_, [=] {
        zlink_registry_topology_entry_t entries[8];
        size_t count = 8;
        memset (entries, 0, sizeof (entries));
        if (zlink_registry_topology (registry_, filter_, entries, &count) == ZLINK_CONFIG_OK
            && count >= min_count_) {
            uint32_t desired_total = 0;
            for (size_t j = 0; j < count; ++j)
                desired_total += entries[j].desired_count;
            if (desired_total == expected_desired_total_)
                return true;
        }
        return false;
    });
}

bool routing_id_equal_local (const zlink_routing_id_t &left_, const zlink_routing_id_t &right_)
{
    return left_.size == right_.size && memcmp (left_.data, right_.data, left_.size) == 0;
}

bool wait_for_route_bind_ok_local (void *discovery_,
                                   zlink_route_kind_t kind_,
                                   const void *key_,
                                   size_t key_size_,
                                   const void *value_,
                                   size_t value_size_,
                                   int timeout_ms_)
{
    return zlink_test_wait_until (timeout_ms_, [=] {
        return zlink_discovery_bind_route (discovery_, kind_, key_, key_size_, value_, value_size_)
               == ZLINK_CONFIG_OK;
    });
}

bool wait_for_route_resolve_local (void *discovery_,
                                   zlink_route_kind_t kind_,
                                   const void *key_,
                                   size_t key_size_,
                                   const zlink_routing_id_t &expected_owner_,
                                   const void *expected_value_,
                                   size_t expected_value_size_,
                                   int timeout_ms_)
{
    return zlink_test_wait_until (timeout_ms_, [=] {
        zlink_routing_id_t owner;
        zlink_msg_t value;
        memset (&owner, 0, sizeof (owner));
        memset (&value, 0, sizeof (value));
        if (zlink_discovery_resolve_route (discovery_, kind_, key_, key_size_, &owner, &value)
            != ZLINK_CONFIG_OK)
            return false;

        const bool matched =
          routing_id_equal_local (owner, expected_owner_)
          && zlink_msg_size (&value) == expected_value_size_
          && (expected_value_size_ == 0
              || memcmp (zlink_msg_data (&value), expected_value_, expected_value_size_) == 0);
        (void) zlink_msg_close (&value);
        return matched;
    });
}

bool wait_for_route_not_found_local (
  void *discovery_, zlink_route_kind_t kind_, const void *key_, size_t key_size_, int timeout_ms_)
{
    return zlink_test_wait_until (timeout_ms_, [=] {
        zlink_routing_id_t owner;
        zlink_msg_t value;
        memset (&owner, 0, sizeof (owner));
        memset (&value, 0, sizeof (value));
        return zlink_discovery_resolve_route (discovery_, kind_, key_, key_size_, &owner, &value)
               == ZLINK_CONFIG_NOT_FOUND;
    });
}

bool wait_for_actor_route_not_found_local (void *discovery_, const char *actor_id_, int timeout_ms_)
{
    return zlink_test_wait_until (timeout_ms_, [=] {
        zlink_actor_route_t route;
        memset (&route, 0, sizeof (route));
        errno = 0;
        return zlink_discovery_resolve_actor (discovery_, actor_id_, &route)
                 == ZLINK_CONFIG_NOT_FOUND
               && zlink_errno () == ENOENT;
    });
}

void init_socket_topology_filter_local (zlink_registry_topology_filter_t *filter_,
                                        const char *channel_name_,
                                        zlink_service_role_t service_role_,
                                        const zlink_routing_id_t *routing_id_)
{
    memset (filter_, 0, sizeof (*filter_));
    filter_->service_kind = ZLINK_SERVICE_KIND_SOCKET;
    filter_->service_role = service_role_;
    filter_->source = ZLINK_TOPOLOGY_SOURCE_DISCOVERY;
    strncpy (filter_->channel_name, channel_name_, sizeof (filter_->channel_name) - 1);
    if (routing_id_)
        filter_->routing_id = *routing_id_;
}

void init_summary_filter_local (zlink_registry_service_summary_filter_t *filter_,
                                zlink_auto_connect_type_t auto_connect_type_,
                                const char *channel_name_,
                                zlink_service_role_t service_role_)
{
    memset (filter_, 0, sizeof (*filter_));
    filter_->auto_connect_type = auto_connect_type_;
    filter_->service_role = service_role_;
    if (channel_name_)
        strncpy (filter_->channel_name, channel_name_, sizeof (filter_->channel_name) - 1);
}

bool connect_discovery_expect_errno_local (void *discovery_,
                                           const char *endpoint_,
                                           int expected_errno_,
                                           int timeout_ms_)
{
    return zlink_test_wait_until (timeout_ms_, [=] {
        errno = 0;
        if (zlink_discovery_connect_registry (discovery_, endpoint_) != ZLINK_CONNECT_OK
            && zlink_errno () == expected_errno_)
            return true;
        return false;
    });
}

bool wait_for_router_payload_local (void *router_, const char *expected_, int timeout_ms_)
{
    return zlink_test_wait_until (timeout_ms_, [=] {
        const zlink_routing_id_t *source_node_rid = NULL;
        const zlink_routing_id_t *source_spot_rid = NULL;
        uint64_t request_seq = 0;
        zlink_msg_t part;
        zlink_part_flag_t more = ZLINK_PART_FINAL;
        memset (&part, 0, sizeof (part));
        const zlink_recv_result_t rc =
          zlink_router_recv_part (router_, &source_node_rid, &source_spot_rid, &request_seq, &part,
                                  &more, static_cast<zlink_recv_flags_t> (ZLINK_DONTWAIT));
        if (rc == ZLINK_RECV_OK) {
            const bool matched =
              more == ZLINK_PART_FINAL && zlink_msg_size (&part) == strlen (expected_)
              && memcmp (zlink_msg_data (&part), expected_, strlen (expected_)) == 0;
            zlink_msg_close (&part);
            if (matched)
                return true;
        }
        return false;
    });
}

bool wait_for_router_rid_payload_local (void *sender_,
                                        void *receiver_,
                                        const zlink_routing_id_t &receiver_rid_,
                                        const char *text_,
                                        int timeout_ms_)
{
    return zlink_test_wait_until (timeout_ms_, [=, &receiver_rid_] {
        zlink_msg_t outbound;
        TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&outbound, strlen (text_)));
        memcpy (zlink_msg_data (&outbound), text_, strlen (text_));
        const zlink_submit_result_t submit_rc =
          zlink_send_rid (sender_, &receiver_rid_, &outbound, 1, ZLINK_DONTWAIT);
        if (submit_rc != ZLINK_SUBMIT_OK)
            zlink_msg_close (&outbound);
        if (wait_for_router_payload_local (receiver_, text_, zlink_test_poll_step_ms))
            return true;
        return false;
    });
}

bool wait_for_route_mesh_request_reply_local (void *client_,
                                              void *server_,
                                              const zlink_routing_id_t &server_rid_,
                                              const char *request_,
                                              const char *reply_,
                                              int timeout_ms_)
{
    zlink_msg_t request_part;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&request_part, strlen (request_)));
    memcpy (zlink_msg_data (&request_part), request_, strlen (request_));

    route_reply_probe_t reply_probe;
    reply_probe.progress_socket = client_;
    if (zlink_router_request (client_, &server_rid_, &request_part, 1, &capture_route_reply_local,
                              &reply_probe, 0, timeout_ms_)
        != ZLINK_SUBMIT_OK) {
        zlink_msg_close (&request_part);
        return false;
    }

    return zlink_test_wait_until (timeout_ms_, [&] {
        const zlink_routing_id_t *source_node_rid = NULL;
        const zlink_routing_id_t *source_spot_rid = NULL;
        uint64_t request_seq = 0;
        zlink_msg_t part;
        zlink_part_flag_t more = ZLINK_PART_FINAL;
        memset (&part, 0, sizeof (part));
        const zlink_recv_result_t recv_rc =
          zlink_router_recv_part (server_, &source_node_rid, &source_spot_rid, &request_seq, &part,
                                  &more, static_cast<zlink_recv_flags_t> (ZLINK_DONTWAIT));
        if (recv_rc == ZLINK_RECV_OK) {
            const bool matched =
              more == ZLINK_PART_FINAL && request_seq != 0
              && zlink_msg_size (&part) == strlen (request_)
              && memcmp (zlink_msg_data (&part), request_, strlen (request_)) == 0;
            zlink_msg_close (&part);
            if (matched) {
                zlink_msg_t reply_part;
                TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&reply_part, strlen (reply_)));
                memcpy (zlink_msg_data (&reply_part), reply_, strlen (reply_));
                TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK,
                                   zlink_router_reply (server_, source_node_rid, request_seq,
                                                       &reply_part, 1));
            }
        }
        if (reply_probe.progress_socket)
            (void) drain_completion_via_poller_local (reply_probe.progress_socket);
        return reply_probe.done && reply_probe.result == ZLINK_REQUEST_OK
               && reply_probe.payload == reply_;
    });
}

bool wait_for_dealer_router_delivery_local (void *dealer_,
                                            void *router_,
                                            const char *text_,
                                            int timeout_ms_)
{
    return zlink_test_wait_until (timeout_ms_, [=] {
        (void) zlink_send (dealer_, text_, strlen (text_), ZLINK_DONTWAIT);
        if (wait_for_router_payload_local (router_, text_, zlink_test_poll_step_ms))
            return true;
        return false;
    });
}

bool wait_for_spot_channel_router_delivery_local (
  void *spot_, const char *channel_name_, void *router_, const char *text_, int timeout_ms_)
{
    return zlink_test_wait_until (timeout_ms_, [=] {
        zlink_msg_t part;
        TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&part, strlen (text_)));
        memcpy (zlink_msg_data (&part), text_, strlen (text_));
        const zlink_submit_result_t submit_rc =
          zlink_spot_send_channel (spot_, channel_name_, &part, 1, ZLINK_DONTWAIT);
        if (submit_rc != ZLINK_SUBMIT_OK)
            zlink_msg_close (&part);
        if (wait_for_router_payload_local (router_, text_, zlink_test_poll_step_ms))
            return true;
        return false;
    });
}

bool wait_for_pubsub_delivery_local (void *pub_, void *sub_, const char *text_, int timeout_ms_)
{
    return zlink_test_wait_until (timeout_ms_, [=] {
        (void) zlink_send (pub_, text_, strlen (text_), ZLINK_DONTWAIT);
        char buffer[128];
        memset (buffer, 0, sizeof (buffer));
        const int rc = zlink_recv (sub_, buffer, sizeof (buffer), ZLINK_DONTWAIT);
        if (rc == static_cast<int> (strlen (text_)) && memcmp (buffer, text_, strlen (text_)) == 0)
            return true;
        return false;
    });
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
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_set_broadcast_interval (registry, 50));

    char registry_pub[MAX_SOCKET_STRING];
    char registry_router[MAX_SOCKET_STRING];
    char router_endpoint[MAX_SOCKET_STRING];
    char dealer_endpoint[MAX_SOCKET_STRING];
    TEST_ASSERT_TRUE (bind_registry_test_endpoints_local (registry, 5960, registry_pub,
                                                          sizeof (registry_pub), registry_router,
                                                          sizeof (registry_router)));

    void *router_discovery =
      zlink_discovery_new (ctx, ZLINK_AUTO_CONNECT_CLIENT_SERVER, "socket-auto-router");
    void *dealer_discovery =
      zlink_discovery_new (ctx, ZLINK_AUTO_CONNECT_CLIENT_SERVER, "socket-auto-router");
    TEST_ASSERT_NOT_NULL (router_discovery);
    TEST_ASSERT_NOT_NULL (dealer_discovery);
    TEST_ASSERT_TRUE (
      connect_discovery_registry_with_retry_local (router_discovery, registry_router, 3000));
    TEST_ASSERT_TRUE (
      connect_discovery_registry_with_retry_local (dealer_discovery, registry_router, 3000));

    void *router = zlink_socket (ctx, ZLINK_SOCKET_ROUTER);
    void *dealer = zlink_socket (ctx, ZLINK_SOCKET_DEALER);
    TEST_ASSERT_NOT_NULL (router);
    TEST_ASSERT_NOT_NULL (dealer);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (router, "router-a", 8));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (dealer, "dealer-a", 8));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_socket_attach_discovery (router, router_discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_socket_attach_discovery (dealer, dealer_discovery));
    TEST_ASSERT_TRUE (
      bind_socket_test_endpoint_local (router, 5962, router_endpoint, sizeof (router_endpoint)));
    TEST_ASSERT_TRUE (
      bind_socket_test_endpoint_local (dealer, 5963, dealer_endpoint, sizeof (dealer_endpoint)));

    zlink_routing_id_t router_rid;
    zlink_routing_id_t dealer_rid;
    memset (&router_rid, 0, sizeof (router_rid));
    memset (&dealer_rid, 0, sizeof (dealer_rid));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_routing_id (router, &router_rid));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_routing_id (dealer, &dealer_rid));
    TEST_ASSERT_TRUE (wait_for_discovery_member_role_count_local (
      dealer_discovery, ZLINK_SERVICE_ROLE_ROUTER, 1, 10000));
    TEST_ASSERT_TRUE (wait_for_discovery_member_role_count_local (
      router_discovery, ZLINK_SERVICE_ROLE_DEALER, 1, 10000));

    zlink_registry_topology_filter_t dealer_filter;
    init_socket_topology_filter_local (&dealer_filter, "socket-auto-router",
                                       ZLINK_SERVICE_ROLE_DEALER, &dealer_rid);
    zlink_registry_topology_entry_t dealer_entry;
    memset (&dealer_entry, 0, sizeof (dealer_entry));
    TEST_ASSERT_TRUE (
      wait_for_topology_entry_local (registry, &dealer_filter, &dealer_entry, 1, 60000));
    TEST_ASSERT_EQUAL_UINT32 (1u, dealer_entry.desired_count);
    TEST_ASSERT_EQUAL (ZLINK_SPOT_KIND_INVALID, dealer_entry.spot_kind);

    zlink_registry_topology_filter_t router_filter;
    init_socket_topology_filter_local (&router_filter, "socket-auto-router",
                                       ZLINK_SERVICE_ROLE_ROUTER, &router_rid);
    zlink_registry_topology_entry_t router_entry;
    memset (&router_entry, 0, sizeof (router_entry));
    TEST_ASSERT_TRUE (
      wait_for_topology_entry_local (registry, &router_filter, &router_entry, 0, 60000));
    TEST_ASSERT_EQUAL_UINT32 (0u, router_entry.desired_count);
    TEST_ASSERT_EQUAL (ZLINK_SPOT_KIND_INVALID, router_entry.spot_kind);

    TEST_ASSERT_TRUE (destroy_discovery_with_retry_local (&dealer_discovery, 3000));
    TEST_ASSERT_TRUE (destroy_discovery_with_retry_local (&router_discovery, 3000));
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
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_set_broadcast_interval (registry, 50));

    char registry_pub[MAX_SOCKET_STRING];
    char registry_router[MAX_SOCKET_STRING];
    char dealer_a_endpoint[MAX_SOCKET_STRING];
    char dealer_b_endpoint[MAX_SOCKET_STRING];
    TEST_ASSERT_TRUE (bind_registry_test_endpoints_local (registry, 5970, registry_pub,
                                                          sizeof (registry_pub), registry_router,
                                                          sizeof (registry_router)));

    void *discovery_a =
      zlink_discovery_new (ctx, ZLINK_AUTO_CONNECT_CLIENT_SERVER, "socket-auto-default");
    void *discovery_b =
      zlink_discovery_new (ctx, ZLINK_AUTO_CONNECT_CLIENT_SERVER, "socket-auto-default");
    TEST_ASSERT_NOT_NULL (discovery_a);
    TEST_ASSERT_NOT_NULL (discovery_b);
    TEST_ASSERT_TRUE (
      connect_discovery_registry_with_retry_local (discovery_a, registry_router, 3000));
    TEST_ASSERT_TRUE (
      connect_discovery_registry_with_retry_local (discovery_b, registry_router, 3000));

    void *dealer_a = zlink_socket (ctx, ZLINK_SOCKET_DEALER);
    void *dealer_b = zlink_socket (ctx, ZLINK_SOCKET_DEALER);
    TEST_ASSERT_NOT_NULL (dealer_a);
    TEST_ASSERT_NOT_NULL (dealer_b);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (dealer_a, "dealer-b0", 9));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (dealer_b, "dealer-b1", 9));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_socket_attach_discovery (dealer_a, discovery_a));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_socket_attach_discovery (dealer_b, discovery_b));
    TEST_ASSERT_TRUE (bind_socket_test_endpoint_local (dealer_a, 5972, dealer_a_endpoint,
                                                       sizeof (dealer_a_endpoint)));
    TEST_ASSERT_TRUE (bind_socket_test_endpoint_local (dealer_b, 5973, dealer_b_endpoint,
                                                       sizeof (dealer_b_endpoint)));

    zlink_routing_id_t dealer_a_rid;
    zlink_routing_id_t dealer_b_rid;
    memset (&dealer_a_rid, 0, sizeof (dealer_a_rid));
    memset (&dealer_b_rid, 0, sizeof (dealer_b_rid));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_routing_id (dealer_a, &dealer_a_rid));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_routing_id (dealer_b, &dealer_b_rid));
    TEST_ASSERT_TRUE (wait_for_discovery_member_role_count_local (
      discovery_a, ZLINK_SERVICE_ROLE_DEALER, 1, 10000));
    TEST_ASSERT_TRUE (wait_for_discovery_member_role_count_local (
      discovery_b, ZLINK_SERVICE_ROLE_DEALER, 1, 10000));

    zlink_registry_topology_filter_t filter_a;
    init_socket_topology_filter_local (&filter_a, "socket-auto-default", ZLINK_SERVICE_ROLE_DEALER,
                                       &dealer_a_rid);
    zlink_registry_topology_entry_t entry_a;
    memset (&entry_a, 0, sizeof (entry_a));
    TEST_ASSERT_TRUE (wait_for_topology_entry_local (registry, &filter_a, &entry_a, 0, 20000));

    zlink_registry_topology_filter_t filter_b;
    init_socket_topology_filter_local (&filter_b, "socket-auto-default", ZLINK_SERVICE_ROLE_DEALER,
                                       &dealer_b_rid);
    zlink_registry_topology_entry_t entry_b;
    memset (&entry_b, 0, sizeof (entry_b));
    TEST_ASSERT_TRUE (wait_for_topology_entry_local (registry, &filter_b, &entry_b, 0, 20000));

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
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_set_broadcast_interval (registry, 50));

    char registry_pub[MAX_SOCKET_STRING];
    char registry_router[MAX_SOCKET_STRING];
    char dealer_a_endpoint[MAX_SOCKET_STRING];
    char dealer_b_endpoint[MAX_SOCKET_STRING];
    TEST_ASSERT_TRUE (bind_registry_test_endpoints_local (registry, 5980, registry_pub,
                                                          sizeof (registry_pub), registry_router,
                                                          sizeof (registry_router)));

    void *discovery_a =
      zlink_discovery_new (ctx, ZLINK_AUTO_CONNECT_DEALER_MESH, "socket-auto-dealer");
    void *discovery_b =
      zlink_discovery_new (ctx, ZLINK_AUTO_CONNECT_DEALER_MESH, "socket-auto-dealer");
    TEST_ASSERT_NOT_NULL (discovery_a);
    TEST_ASSERT_NOT_NULL (discovery_b);
    TEST_ASSERT_TRUE (
      connect_discovery_registry_with_retry_local (discovery_a, registry_router, 3000));
    TEST_ASSERT_TRUE (
      connect_discovery_registry_with_retry_local (discovery_b, registry_router, 3000));

    void *dealer_a = zlink_socket (ctx, ZLINK_SOCKET_DEALER);
    void *dealer_b = zlink_socket (ctx, ZLINK_SOCKET_DEALER);
    TEST_ASSERT_NOT_NULL (dealer_a);
    TEST_ASSERT_NOT_NULL (dealer_b);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (dealer_a, "dealer-c0", 9));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (dealer_b, "dealer-c1", 9));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_socket_attach_discovery (dealer_a, discovery_a));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_socket_attach_discovery (dealer_b, discovery_b));
    TEST_ASSERT_TRUE (bind_socket_test_endpoint_local (dealer_a, 5982, dealer_a_endpoint,
                                                       sizeof (dealer_a_endpoint)));
    TEST_ASSERT_TRUE (bind_socket_test_endpoint_local (dealer_b, 5983, dealer_b_endpoint,
                                                       sizeof (dealer_b_endpoint)));

    zlink_routing_id_t dealer_a_rid;
    zlink_routing_id_t dealer_b_rid;
    memset (&dealer_a_rid, 0, sizeof (dealer_a_rid));
    memset (&dealer_b_rid, 0, sizeof (dealer_b_rid));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_routing_id (dealer_a, &dealer_a_rid));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_routing_id (dealer_b, &dealer_b_rid));
    TEST_ASSERT_TRUE (wait_for_discovery_member_role_count_local (
      discovery_a, ZLINK_SERVICE_ROLE_DEALER, 1, 10000));
    TEST_ASSERT_TRUE (wait_for_discovery_member_role_count_local (
      discovery_b, ZLINK_SERVICE_ROLE_DEALER, 1, 10000));

    zlink_registry_topology_filter_t filter;
    init_socket_topology_filter_local (&filter, "socket-auto-dealer", ZLINK_SERVICE_ROLE_DEALER,
                                       NULL);
    bool saw_dealer_mesh_topology =
      wait_for_topology_desired_total_local (registry, &filter, 2, 1, 10000);
    TEST_ASSERT_TRUE (saw_dealer_mesh_topology);

    TEST_ASSERT_TRUE (destroy_discovery_with_retry_local (&discovery_b, 3000));
    TEST_ASSERT_TRUE (destroy_discovery_with_retry_local (&discovery_a, 3000));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_socket_discovery_router_router_route_mesh_initiates_both_directions ()
{
    if (!zlink_has ("tcp")) {
        TEST_IGNORE_MESSAGE ("TCP not available");
        return;
    }

    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *registry = zlink_registry_new (ctx);
    TEST_ASSERT_NOT_NULL (registry);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_set_broadcast_interval (registry, 50));

    char registry_pub[MAX_SOCKET_STRING];
    char registry_router[MAX_SOCKET_STRING];
    char router_a_endpoint[MAX_SOCKET_STRING];
    char router_b_endpoint[MAX_SOCKET_STRING];
    TEST_ASSERT_TRUE (bind_registry_test_endpoints_local (registry, 5984, registry_pub,
                                                          sizeof (registry_pub), registry_router,
                                                          sizeof (registry_router)));

    void *discovery_a =
      zlink_discovery_new (ctx, ZLINK_AUTO_CONNECT_ROUTE_MESH, "socket-auto-router-router");
    void *discovery_b =
      zlink_discovery_new (ctx, ZLINK_AUTO_CONNECT_ROUTE_MESH, "socket-auto-router-router");
    TEST_ASSERT_NOT_NULL (discovery_a);
    TEST_ASSERT_NOT_NULL (discovery_b);
    TEST_ASSERT_TRUE (
      connect_discovery_registry_with_retry_local (discovery_a, registry_router, 3000));
    TEST_ASSERT_TRUE (
      connect_discovery_registry_with_retry_local (discovery_b, registry_router, 3000));

    void *router_a = zlink_socket (ctx, ZLINK_SOCKET_ROUTER);
    void *router_b = zlink_socket (ctx, ZLINK_SOCKET_ROUTER);
    TEST_ASSERT_NOT_NULL (router_a);
    TEST_ASSERT_NOT_NULL (router_b);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (router_a, "router-aa", 9));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (router_b, "router-bb", 9));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_socket_attach_discovery (router_a, discovery_a));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_socket_attach_discovery (router_b, discovery_b));
    TEST_ASSERT_TRUE (bind_socket_test_endpoint_local (router_a, 5986, router_a_endpoint,
                                                       sizeof (router_a_endpoint)));
    TEST_ASSERT_TRUE (bind_socket_test_endpoint_local (router_b, 5987, router_b_endpoint,
                                                       sizeof (router_b_endpoint)));

    zlink_routing_id_t rid_a;
    zlink_routing_id_t rid_b;
    memset (&rid_a, 0, sizeof (rid_a));
    memset (&rid_b, 0, sizeof (rid_b));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_routing_id (router_a, &rid_a));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_routing_id (router_b, &rid_b));
    TEST_ASSERT_TRUE (wait_for_discovery_member_role_count_local (
      discovery_a, ZLINK_SERVICE_ROLE_ROUTER, 1, 10000));
    TEST_ASSERT_TRUE (wait_for_discovery_member_role_count_local (
      discovery_b, ZLINK_SERVICE_ROLE_ROUTER, 1, 10000));

    zlink_registry_topology_filter_t filter;
    init_socket_topology_filter_local (&filter, "socket-auto-router-router",
                                       ZLINK_SERVICE_ROLE_ROUTER, NULL);
    bool both_routers_initiate =
      wait_for_topology_desired_total_local (registry, &filter, 2, 2, 10000);

    TEST_ASSERT_TRUE (both_routers_initiate);

    TEST_ASSERT_TRUE (destroy_discovery_with_retry_local (&discovery_b, 3000));
    TEST_ASSERT_TRUE (destroy_discovery_with_retry_local (&discovery_a, 3000));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_route_mesh_discovery_router_router_delivers_payload_both_directions ()
{
    if (!zlink_has ("tcp")) {
        TEST_IGNORE_MESSAGE ("TCP not available");
        return;
    }

    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *registry = zlink_registry_new (ctx);
    TEST_ASSERT_NOT_NULL (registry);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_set_broadcast_interval (registry, 50));

    char registry_pub[MAX_SOCKET_STRING];
    char registry_router[MAX_SOCKET_STRING];
    char router_a_endpoint[MAX_SOCKET_STRING];
    char router_b_endpoint[MAX_SOCKET_STRING];
    TEST_ASSERT_TRUE (bind_registry_test_endpoints_local (registry, 5988, registry_pub,
                                                          sizeof (registry_pub), registry_router,
                                                          sizeof (registry_router)));

    void *discovery_a =
      zlink_discovery_new (ctx, ZLINK_AUTO_CONNECT_ROUTE_MESH, "route-mesh-payload");
    void *discovery_b =
      zlink_discovery_new (ctx, ZLINK_AUTO_CONNECT_ROUTE_MESH, "route-mesh-payload");
    TEST_ASSERT_NOT_NULL (discovery_a);
    TEST_ASSERT_NOT_NULL (discovery_b);
    TEST_ASSERT_TRUE (
      connect_discovery_registry_with_retry_local (discovery_a, registry_router, 3000));
    TEST_ASSERT_TRUE (
      connect_discovery_registry_with_retry_local (discovery_b, registry_router, 3000));

    void *router_a = zlink_socket (ctx, ZLINK_SOCKET_ROUTER);
    void *router_b = zlink_socket (ctx, ZLINK_SOCKET_ROUTER);
    TEST_ASSERT_NOT_NULL (router_a);
    TEST_ASSERT_NOT_NULL (router_b);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (router_a, "route-pa", 8));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (router_b, "route-pb", 8));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_socket_attach_discovery (router_a, discovery_a));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_socket_attach_discovery (router_b, discovery_b));
    TEST_ASSERT_TRUE (bind_socket_test_endpoint_local (router_a, 5989, router_a_endpoint,
                                                       sizeof (router_a_endpoint)));
    TEST_ASSERT_TRUE (bind_socket_test_endpoint_local (router_b, 5991, router_b_endpoint,
                                                       sizeof (router_b_endpoint)));

    zlink_routing_id_t rid_a;
    zlink_routing_id_t rid_b;
    memset (&rid_a, 0, sizeof (rid_a));
    memset (&rid_b, 0, sizeof (rid_b));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_routing_id (router_a, &rid_a));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_routing_id (router_b, &rid_b));
    TEST_ASSERT_TRUE (wait_for_discovery_member_role_count_local (
      discovery_a, ZLINK_SERVICE_ROLE_ROUTER, 1, 10000));
    TEST_ASSERT_TRUE (wait_for_discovery_member_role_count_local (
      discovery_b, ZLINK_SERVICE_ROLE_ROUTER, 1, 10000));

    TEST_ASSERT_TRUE (
      wait_for_router_rid_payload_local (router_a, router_b, rid_b, "route-a-to-b", 10000));
    TEST_ASSERT_TRUE (
      wait_for_router_rid_payload_local (router_b, router_a, rid_a, "route-b-to-a", 10000));
    TEST_ASSERT_TRUE (wait_for_route_mesh_request_reply_local (
      router_b, router_a, rid_a, "route-request-b-to-a", "route-reply-a-to-b", 10000));

    TEST_ASSERT_TRUE (destroy_discovery_with_retry_local (&discovery_b, 3000));
    TEST_ASSERT_TRUE (destroy_discovery_with_retry_local (&discovery_a, 3000));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_route_mesh_discovery_three_routers_deliver_high_to_middle ()
{
    if (!zlink_has ("tcp")) {
        TEST_IGNORE_MESSAGE ("TCP not available");
        return;
    }

    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *registry = zlink_registry_new (ctx);
    TEST_ASSERT_NOT_NULL (registry);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_set_broadcast_interval (registry, 50));

    char registry_pub[MAX_SOCKET_STRING];
    char registry_router[MAX_SOCKET_STRING];
    char endpoint_session[MAX_SOCKET_STRING];
    char endpoint_play[MAX_SOCKET_STRING];
    char endpoint_api[MAX_SOCKET_STRING];
    TEST_ASSERT_TRUE (bind_registry_test_endpoints_local (registry, 5992, registry_pub,
                                                          sizeof (registry_pub), registry_router,
                                                          sizeof (registry_router)));

    void *discovery_session =
      zlink_discovery_new (ctx, ZLINK_AUTO_CONNECT_ROUTE_MESH, "route-mesh-three");
    void *discovery_play =
      zlink_discovery_new (ctx, ZLINK_AUTO_CONNECT_ROUTE_MESH, "route-mesh-three");
    void *discovery_api =
      zlink_discovery_new (ctx, ZLINK_AUTO_CONNECT_ROUTE_MESH, "route-mesh-three");
    TEST_ASSERT_NOT_NULL (discovery_session);
    TEST_ASSERT_NOT_NULL (discovery_play);
    TEST_ASSERT_NOT_NULL (discovery_api);
    TEST_ASSERT_TRUE (
      connect_discovery_registry_with_retry_local (discovery_session, registry_router, 3000));
    TEST_ASSERT_TRUE (
      connect_discovery_registry_with_retry_local (discovery_play, registry_router, 3000));
    TEST_ASSERT_TRUE (
      connect_discovery_registry_with_retry_local (discovery_api, registry_router, 3000));

    void *router_session = zlink_socket (ctx, ZLINK_SOCKET_ROUTER);
    void *router_play = zlink_socket (ctx, ZLINK_SOCKET_ROUTER);
    void *router_api = zlink_socket (ctx, ZLINK_SOCKET_ROUTER);
    TEST_ASSERT_NOT_NULL (router_session);
    TEST_ASSERT_NOT_NULL (router_play);
    TEST_ASSERT_NOT_NULL (router_api);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (router_session, "1201", 4));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (router_play, "2201", 4));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (router_api, "3301", 4));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_socket_attach_discovery (router_session, discovery_session));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_socket_attach_discovery (router_play, discovery_play));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_socket_attach_discovery (router_api, discovery_api));
    TEST_ASSERT_TRUE (bind_socket_test_endpoint_local (router_session, 5993, endpoint_session,
                                                       sizeof (endpoint_session)));
    TEST_ASSERT_TRUE (bind_socket_test_endpoint_local (router_play, 5994, endpoint_play,
                                                       sizeof (endpoint_play)));
    TEST_ASSERT_TRUE (bind_socket_test_endpoint_local (router_api, 5995, endpoint_api,
                                                       sizeof (endpoint_api)));

    zlink_routing_id_t rid_play;
    memset (&rid_play, 0, sizeof (rid_play));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_routing_id (router_play, &rid_play));

    TEST_ASSERT_TRUE (wait_for_discovery_member_role_count_local (
      discovery_session, ZLINK_SERVICE_ROLE_ROUTER, 2, 10000));
    TEST_ASSERT_TRUE (wait_for_discovery_member_role_count_local (
      discovery_play, ZLINK_SERVICE_ROLE_ROUTER, 2, 10000));
    TEST_ASSERT_TRUE (wait_for_discovery_member_role_count_local (
      discovery_api, ZLINK_SERVICE_ROLE_ROUTER, 2, 10000));

    zlink_registry_topology_filter_t filter;
    init_socket_topology_filter_local (&filter, "route-mesh-three",
                                       ZLINK_SERVICE_ROLE_ROUTER, NULL);
    TEST_ASSERT_TRUE (wait_for_topology_desired_total_local (registry, &filter, 3, 6, 10000));

    TEST_ASSERT_TRUE (wait_for_route_mesh_request_reply_local (
      router_session, router_play, rid_play, "session-to-play", "play-to-session", 10000));
    TEST_ASSERT_TRUE (wait_for_route_mesh_request_reply_local (
      router_api, router_play, rid_play, "api-to-play", "play-to-api", 10000));

    TEST_ASSERT_TRUE (destroy_discovery_with_retry_local (&discovery_api, 3000));
    TEST_ASSERT_TRUE (destroy_discovery_with_retry_local (&discovery_play, 3000));
    TEST_ASSERT_TRUE (destroy_discovery_with_retry_local (&discovery_session, 3000));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_route_mesh_discovery_router_router_delivers_payload_across_contexts ()
{
    if (!zlink_has ("tcp")) {
        TEST_IGNORE_MESSAGE ("TCP not available");
        return;
    }

    void *registry_ctx = zlink_ctx_new ();
    void *ctx_a = zlink_ctx_new ();
    void *ctx_b = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (registry_ctx);
    TEST_ASSERT_NOT_NULL (ctx_a);
    TEST_ASSERT_NOT_NULL (ctx_b);

    void *registry = zlink_registry_new (registry_ctx);
    TEST_ASSERT_NOT_NULL (registry);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_set_broadcast_interval (registry, 50));

    char registry_pub[MAX_SOCKET_STRING];
    char registry_router[MAX_SOCKET_STRING];
    char router_a_endpoint[MAX_SOCKET_STRING];
    char router_b_endpoint[MAX_SOCKET_STRING];
    TEST_ASSERT_TRUE (bind_registry_test_endpoints_local (registry, 5999, registry_pub,
                                                          sizeof (registry_pub), registry_router,
                                                          sizeof (registry_router)));

    void *discovery_a =
      zlink_discovery_new (ctx_a, ZLINK_AUTO_CONNECT_ROUTE_MESH, "route-mesh-cross-ctx");
    void *discovery_b =
      zlink_discovery_new (ctx_b, ZLINK_AUTO_CONNECT_ROUTE_MESH, "route-mesh-cross-ctx");
    TEST_ASSERT_NOT_NULL (discovery_a);
    TEST_ASSERT_NOT_NULL (discovery_b);
    TEST_ASSERT_TRUE (
      connect_discovery_registry_with_retry_local (discovery_a, registry_router, 3000));
    TEST_ASSERT_TRUE (
      connect_discovery_registry_with_retry_local (discovery_b, registry_router, 3000));

    void *router_a = zlink_socket (ctx_a, ZLINK_SOCKET_ROUTER);
    void *router_b = zlink_socket (ctx_b, ZLINK_SOCKET_ROUTER);
    TEST_ASSERT_NOT_NULL (router_a);
    TEST_ASSERT_NOT_NULL (router_b);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (router_a, "route-ca", 8));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (router_b, "route-cb", 8));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_socket_attach_discovery (router_a, discovery_a));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_socket_attach_discovery (router_b, discovery_b));
    TEST_ASSERT_TRUE (bind_socket_test_endpoint_local (router_a, 6003, router_a_endpoint,
                                                       sizeof (router_a_endpoint)));
    TEST_ASSERT_TRUE (bind_socket_test_endpoint_local (router_b, 6004, router_b_endpoint,
                                                       sizeof (router_b_endpoint)));

    zlink_routing_id_t rid_a;
    zlink_routing_id_t rid_b;
    memset (&rid_a, 0, sizeof (rid_a));
    memset (&rid_b, 0, sizeof (rid_b));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_routing_id (router_a, &rid_a));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_routing_id (router_b, &rid_b));
    TEST_ASSERT_TRUE (wait_for_discovery_member_role_count_local (
      discovery_a, ZLINK_SERVICE_ROLE_ROUTER, 1, 10000));
    TEST_ASSERT_TRUE (wait_for_discovery_member_role_count_local (
      discovery_b, ZLINK_SERVICE_ROLE_ROUTER, 1, 10000));

    TEST_ASSERT_TRUE (
      wait_for_router_rid_payload_local (router_a, router_b, rid_b, "cross-ctx-a-to-b", 10000));
    TEST_ASSERT_TRUE (
      wait_for_router_rid_payload_local (router_b, router_a, rid_a, "cross-ctx-b-to-a", 10000));

    TEST_ASSERT_TRUE (destroy_discovery_with_retry_local (&discovery_b, 3000));
    TEST_ASSERT_TRUE (destroy_discovery_with_retry_local (&discovery_a, 3000));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx_b));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx_a));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (registry_ctx));
}

void test_route_mesh_discovery_three_routers_deliver_high_to_middle_across_contexts ()
{
    if (!zlink_has ("tcp")) {
        TEST_IGNORE_MESSAGE ("TCP not available");
        return;
    }

    void *registry_ctx = zlink_ctx_new ();
    void *session_ctx = zlink_ctx_new ();
    void *play_ctx = zlink_ctx_new ();
    void *api_ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (registry_ctx);
    TEST_ASSERT_NOT_NULL (session_ctx);
    TEST_ASSERT_NOT_NULL (play_ctx);
    TEST_ASSERT_NOT_NULL (api_ctx);

    void *registry = zlink_registry_new (registry_ctx);
    TEST_ASSERT_NOT_NULL (registry);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_set_broadcast_interval (registry, 50));

    char registry_pub[MAX_SOCKET_STRING];
    char registry_router[MAX_SOCKET_STRING];
    char endpoint_session[MAX_SOCKET_STRING];
    char endpoint_play[MAX_SOCKET_STRING];
    char endpoint_api[MAX_SOCKET_STRING];
    TEST_ASSERT_TRUE (bind_registry_test_endpoints_local (registry, 6005, registry_pub,
                                                          sizeof (registry_pub), registry_router,
                                                          sizeof (registry_router)));

    void *discovery_session =
      zlink_discovery_new (session_ctx, ZLINK_AUTO_CONNECT_ROUTE_MESH, "route-mesh-three-ctx");
    void *discovery_play =
      zlink_discovery_new (play_ctx, ZLINK_AUTO_CONNECT_ROUTE_MESH, "route-mesh-three-ctx");
    void *discovery_api =
      zlink_discovery_new (api_ctx, ZLINK_AUTO_CONNECT_ROUTE_MESH, "route-mesh-three-ctx");
    TEST_ASSERT_NOT_NULL (discovery_session);
    TEST_ASSERT_NOT_NULL (discovery_play);
    TEST_ASSERT_NOT_NULL (discovery_api);
    TEST_ASSERT_TRUE (
      connect_discovery_registry_with_retry_local (discovery_session, registry_router, 3000));
    TEST_ASSERT_TRUE (
      connect_discovery_registry_with_retry_local (discovery_play, registry_router, 3000));
    TEST_ASSERT_TRUE (
      connect_discovery_registry_with_retry_local (discovery_api, registry_router, 3000));

    void *router_session = zlink_socket (session_ctx, ZLINK_SOCKET_ROUTER);
    void *router_play = zlink_socket (play_ctx, ZLINK_SOCKET_ROUTER);
    void *router_api = zlink_socket (api_ctx, ZLINK_SOCKET_ROUTER);
    TEST_ASSERT_NOT_NULL (router_session);
    TEST_ASSERT_NOT_NULL (router_play);
    TEST_ASSERT_NOT_NULL (router_api);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (router_session, "1201", 4));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (router_play, "2201", 4));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (router_api, "3301", 4));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_socket_attach_discovery (router_session, discovery_session));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_socket_attach_discovery (router_play, discovery_play));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_socket_attach_discovery (router_api, discovery_api));
    TEST_ASSERT_TRUE (bind_socket_test_endpoint_local (router_session, 6006, endpoint_session,
                                                       sizeof (endpoint_session)));
    TEST_ASSERT_TRUE (bind_socket_test_endpoint_local (router_play, 6007, endpoint_play,
                                                       sizeof (endpoint_play)));
    TEST_ASSERT_TRUE (bind_socket_test_endpoint_local (router_api, 6008, endpoint_api,
                                                       sizeof (endpoint_api)));

    zlink_routing_id_t rid_play;
    memset (&rid_play, 0, sizeof (rid_play));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_routing_id (router_play, &rid_play));

    TEST_ASSERT_TRUE (wait_for_discovery_member_role_count_local (
      discovery_session, ZLINK_SERVICE_ROLE_ROUTER, 2, 10000));
    TEST_ASSERT_TRUE (wait_for_discovery_member_role_count_local (
      discovery_play, ZLINK_SERVICE_ROLE_ROUTER, 2, 10000));
    TEST_ASSERT_TRUE (wait_for_discovery_member_role_count_local (
      discovery_api, ZLINK_SERVICE_ROLE_ROUTER, 2, 10000));

    zlink_registry_topology_filter_t filter;
    init_socket_topology_filter_local (&filter, "route-mesh-three-ctx",
                                       ZLINK_SERVICE_ROLE_ROUTER, NULL);
    TEST_ASSERT_TRUE (wait_for_topology_desired_total_local (registry, &filter, 3, 6, 10000));

    TEST_ASSERT_TRUE (wait_for_route_mesh_request_reply_local (
      router_session, router_play, rid_play, "ctx-session-to-play", "ctx-play-to-session", 10000));
    TEST_ASSERT_TRUE (wait_for_route_mesh_request_reply_local (
      router_api, router_play, rid_play, "ctx-api-to-play", "ctx-play-to-api", 10000));

    TEST_ASSERT_TRUE (destroy_discovery_with_retry_local (&discovery_api, 3000));
    TEST_ASSERT_TRUE (destroy_discovery_with_retry_local (&discovery_play, 3000));
    TEST_ASSERT_TRUE (destroy_discovery_with_retry_local (&discovery_session, 3000));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (api_ctx));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (play_ctx));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (session_ctx));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (registry_ctx));
}

void test_discovery_channel_contract_rejects_type_conflict_and_persists ()
{
    if (!zlink_has ("tcp")) {
        TEST_IGNORE_MESSAGE ("TCP not available");
        return;
    }

    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *registry = zlink_registry_new (ctx);
    TEST_ASSERT_NOT_NULL (registry);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_set_broadcast_interval (registry, 50));

    char registry_pub[MAX_SOCKET_STRING];
    char registry_router[MAX_SOCKET_STRING];
    TEST_ASSERT_TRUE (bind_registry_test_endpoints_local (registry, 5990, registry_pub,
                                                          sizeof (registry_pub), registry_router,
                                                          sizeof (registry_router)));

    void *first =
      zlink_discovery_new (ctx, ZLINK_AUTO_CONNECT_CLIENT_SERVER, "socket-contract-stable");
    void *same =
      zlink_discovery_new (ctx, ZLINK_AUTO_CONNECT_CLIENT_SERVER, "socket-contract-stable");
    void *conflict = zlink_discovery_new (ctx, ZLINK_AUTO_CONNECT_FANOUT, "socket-contract-stable");
    TEST_ASSERT_NOT_NULL (first);
    TEST_ASSERT_NOT_NULL (same);
    TEST_ASSERT_NOT_NULL (conflict);
    TEST_ASSERT_TRUE (connect_discovery_registry_with_retry_local (first, registry_router, 3000));
    TEST_ASSERT_TRUE (connect_discovery_registry_with_retry_local (same, registry_router, 3000));
    TEST_ASSERT_TRUE (
      connect_discovery_expect_errno_local (conflict, registry_router, EEXIST, 3000));

    TEST_ASSERT_TRUE (destroy_discovery_with_retry_local (&same, 3000));
    TEST_ASSERT_TRUE (destroy_discovery_with_retry_local (&first, 3000));

    void *after_destroy =
      zlink_discovery_new (ctx, ZLINK_AUTO_CONNECT_FANOUT, "socket-contract-stable");
    TEST_ASSERT_NOT_NULL (after_destroy);
    TEST_ASSERT_TRUE (
      connect_discovery_expect_errno_local (after_destroy, registry_router, EEXIST, 3000));

    size_t count = 8;
    zlink_member_peer_entry_t entries[8];
    memset (entries, 0, sizeof (entries));
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK, zlink_registry_member_peers (
                                              registry, "socket-contract-stable", entries, &count));
    TEST_ASSERT_EQUAL_UINT32 (0u, static_cast<uint32_t> (count));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&after_destroy));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&conflict));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_client_server_dealer_connects_all_routers_and_registry_queries_use_channel ()
{
    if (!zlink_has ("tcp")) {
        TEST_IGNORE_MESSAGE ("TCP not available");
        return;
    }

    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *registry = zlink_registry_new (ctx);
    TEST_ASSERT_NOT_NULL (registry);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_set_broadcast_interval (registry, 50));

    char registry_pub[MAX_SOCKET_STRING];
    char registry_router[MAX_SOCKET_STRING];
    char router_a_endpoint[MAX_SOCKET_STRING];
    char router_b_endpoint[MAX_SOCKET_STRING];
    char dealer_endpoint[MAX_SOCKET_STRING];
    TEST_ASSERT_TRUE (bind_registry_test_endpoints_local (registry, 5992, registry_pub,
                                                          sizeof (registry_pub), registry_router,
                                                          sizeof (registry_router)));

    void *router_a_discovery =
      zlink_discovery_new (ctx, ZLINK_AUTO_CONNECT_CLIENT_SERVER, "socket-client-server-all");
    void *router_b_discovery =
      zlink_discovery_new (ctx, ZLINK_AUTO_CONNECT_CLIENT_SERVER, "socket-client-server-all");
    void *dealer_discovery =
      zlink_discovery_new (ctx, ZLINK_AUTO_CONNECT_CLIENT_SERVER, "socket-client-server-all");
    TEST_ASSERT_NOT_NULL (router_a_discovery);
    TEST_ASSERT_NOT_NULL (router_b_discovery);
    TEST_ASSERT_NOT_NULL (dealer_discovery);
    TEST_ASSERT_TRUE (
      connect_discovery_registry_with_retry_local (router_a_discovery, registry_router, 3000));
    TEST_ASSERT_TRUE (
      connect_discovery_registry_with_retry_local (router_b_discovery, registry_router, 3000));
    TEST_ASSERT_TRUE (
      connect_discovery_registry_with_retry_local (dealer_discovery, registry_router, 3000));

    void *router_a = zlink_socket (ctx, ZLINK_SOCKET_ROUTER);
    void *router_b = zlink_socket (ctx, ZLINK_SOCKET_ROUTER);
    void *dealer = zlink_socket (ctx, ZLINK_SOCKET_DEALER);
    TEST_ASSERT_NOT_NULL (router_a);
    TEST_ASSERT_NOT_NULL (router_b);
    TEST_ASSERT_NOT_NULL (dealer);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (router_a, "router-ca", 9));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (router_b, "router-cb", 9));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (dealer, "dealer-cs", 9));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_socket_attach_discovery (router_a, router_a_discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_socket_attach_discovery (router_b, router_b_discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_socket_attach_discovery (dealer, dealer_discovery));
    TEST_ASSERT_TRUE (bind_socket_test_endpoint_local (router_a, 5993, router_a_endpoint,
                                                       sizeof (router_a_endpoint)));
    TEST_ASSERT_TRUE (bind_socket_test_endpoint_local (router_b, 5994, router_b_endpoint,
                                                       sizeof (router_b_endpoint)));
    TEST_ASSERT_TRUE (
      bind_socket_test_endpoint_local (dealer, 5995, dealer_endpoint, sizeof (dealer_endpoint)));

    TEST_ASSERT_TRUE (
      wait_for_registry_member_count_local (registry, "socket-client-server-all", 3, 10000));

    zlink_member_peer_entry_t members[4];
    size_t member_count = 4;
    memset (members, 0, sizeof (members));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK,
      zlink_registry_member_peers (registry, "socket-client-server-all", members, &member_count));
    TEST_ASSERT_EQUAL_UINT32 (3u, static_cast<uint32_t> (member_count));
    size_t router_rows = 0;
    size_t dealer_rows = 0;
    for (size_t i = 0; i < member_count; ++i) {
        TEST_ASSERT_EQUAL_INT (ZLINK_AUTO_CONNECT_CLIENT_SERVER, members[i].auto_connect_type);
        TEST_ASSERT_EQUAL_STRING ("socket-client-server-all", members[i].channel_name);
        if (members[i].service_role == ZLINK_SERVICE_ROLE_ROUTER)
            ++router_rows;
        if (members[i].service_role == ZLINK_SERVICE_ROLE_DEALER)
            ++dealer_rows;
    }
    TEST_ASSERT_EQUAL_UINT32 (2u, static_cast<uint32_t> (router_rows));
    TEST_ASSERT_EQUAL_UINT32 (1u, static_cast<uint32_t> (dealer_rows));

    zlink_registry_topology_filter_t topology_filter;
    init_socket_topology_filter_local (&topology_filter, "socket-client-server-all",
                                       ZLINK_SERVICE_ROLE_DEALER, NULL);
    topology_filter.auto_connect_type = ZLINK_AUTO_CONNECT_CLIENT_SERVER;
    TEST_ASSERT_TRUE (
      wait_for_topology_desired_total_local (registry, &topology_filter, 1, 2, 30000));
    topology_filter.auto_connect_type = ZLINK_AUTO_CONNECT_FANOUT;
    TEST_ASSERT_TRUE (
      wait_for_topology_desired_total_local (registry, &topology_filter, 0, 0, 1000));

    zlink_registry_service_summary_filter_t summary_filter;
    init_summary_filter_local (&summary_filter, ZLINK_AUTO_CONNECT_CLIENT_SERVER,
                               "socket-client-server-all", ZLINK_SERVICE_ROLE_ROUTER);
    TEST_ASSERT_TRUE (wait_for_service_summary_count_local (registry, &summary_filter, 1, 10000));
    init_summary_filter_local (&summary_filter, ZLINK_AUTO_CONNECT_FANOUT,
                               "socket-client-server-all", ZLINK_SERVICE_ROLE_ROUTER);
    TEST_ASSERT_TRUE (wait_for_service_summary_count_local (registry, &summary_filter, 0, 1000));

    TEST_ASSERT_TRUE (destroy_discovery_with_retry_local (&dealer_discovery, 3000));
    TEST_ASSERT_TRUE (destroy_discovery_with_retry_local (&router_b_discovery, 3000));
    TEST_ASSERT_TRUE (destroy_discovery_with_retry_local (&router_a_discovery, 3000));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_endpointless_client_server_dealer_is_not_member_but_reports_connect_intent ()
{
    if (!zlink_has ("tcp")) {
        TEST_IGNORE_MESSAGE ("TCP not available");
        return;
    }

    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *registry = zlink_registry_new (ctx);
    TEST_ASSERT_NOT_NULL (registry);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_set_broadcast_interval (registry, 50));

    char registry_pub[MAX_SOCKET_STRING];
    char registry_router[MAX_SOCKET_STRING];
    char router_endpoint[MAX_SOCKET_STRING];
    TEST_ASSERT_TRUE (bind_registry_test_endpoints_local (registry, 5995, registry_pub,
                                                          sizeof (registry_pub), registry_router,
                                                          sizeof (registry_router)));

    void *router_discovery =
      zlink_discovery_new (ctx, ZLINK_AUTO_CONNECT_CLIENT_SERVER, "socket-client-no-endpoint");
    void *dealer_discovery =
      zlink_discovery_new (ctx, ZLINK_AUTO_CONNECT_CLIENT_SERVER, "socket-client-no-endpoint");
    TEST_ASSERT_NOT_NULL (router_discovery);
    TEST_ASSERT_NOT_NULL (dealer_discovery);
    TEST_ASSERT_TRUE (
      connect_discovery_registry_with_retry_local (router_discovery, registry_router, 3000));
    TEST_ASSERT_TRUE (
      connect_discovery_registry_with_retry_local (dealer_discovery, registry_router, 3000));

    void *router = zlink_socket (ctx, ZLINK_SOCKET_ROUTER);
    void *dealer = zlink_socket (ctx, ZLINK_SOCKET_DEALER);
    TEST_ASSERT_NOT_NULL (router);
    TEST_ASSERT_NOT_NULL (dealer);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (router, "router-ne", 9));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (dealer, "dealer-ne", 9));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_socket_attach_discovery (router, router_discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_socket_attach_discovery (dealer, dealer_discovery));
    TEST_ASSERT_TRUE (
      bind_socket_test_endpoint_local (router, 5996, router_endpoint, sizeof (router_endpoint)));

    TEST_ASSERT_TRUE (
      wait_for_registry_member_count_local (registry, "socket-client-no-endpoint", 1, 10000));
    TEST_ASSERT_TRUE (
      wait_for_dealer_router_delivery_local (dealer, router, "endpointless-client", 10000));

    TEST_ASSERT_TRUE (destroy_discovery_with_retry_local (&dealer_discovery, 3000));
    TEST_ASSERT_TRUE (destroy_discovery_with_retry_local (&router_discovery, 3000));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_fanout_sub_connects_pub_and_endpointless_sub_is_not_member ()
{
    if (!zlink_has ("tcp")) {
        TEST_IGNORE_MESSAGE ("TCP not available");
        return;
    }

    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *registry = zlink_registry_new (ctx);
    TEST_ASSERT_NOT_NULL (registry);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_set_broadcast_interval (registry, 50));

    char registry_pub[MAX_SOCKET_STRING];
    char registry_router[MAX_SOCKET_STRING];
    char pub_endpoint[MAX_SOCKET_STRING];
    TEST_ASSERT_TRUE (bind_registry_test_endpoints_local (registry, 5997, registry_pub,
                                                          sizeof (registry_pub), registry_router,
                                                          sizeof (registry_router)));

    void *pub_discovery =
      zlink_discovery_new (ctx, ZLINK_AUTO_CONNECT_FANOUT, "socket-fanout-events");
    void *sub_discovery =
      zlink_discovery_new (ctx, ZLINK_AUTO_CONNECT_FANOUT, "socket-fanout-events");
    TEST_ASSERT_NOT_NULL (pub_discovery);
    TEST_ASSERT_NOT_NULL (sub_discovery);
    TEST_ASSERT_TRUE (
      connect_discovery_registry_with_retry_local (pub_discovery, registry_router, 3000));
    TEST_ASSERT_TRUE (
      connect_discovery_registry_with_retry_local (sub_discovery, registry_router, 3000));

    void *pub = zlink_socket (ctx, ZLINK_SOCKET_PUB);
    void *sub = zlink_socket (ctx, ZLINK_SOCKET_SUB);
    TEST_ASSERT_NOT_NULL (pub);
    TEST_ASSERT_NOT_NULL (sub);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_subscription (sub, ""));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_socket_attach_discovery (pub, pub_discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_socket_attach_discovery (sub, sub_discovery));
    TEST_ASSERT_TRUE (
      bind_socket_test_endpoint_local (pub, 5998, pub_endpoint, sizeof (pub_endpoint)));

    TEST_ASSERT_TRUE (
      wait_for_registry_member_count_local (registry, "socket-fanout-events", 1, 10000));
    TEST_ASSERT_TRUE (wait_for_pubsub_delivery_local (pub, sub, "fanout-event", 10000));

    zlink_registry_topology_filter_t pub_filter;
    init_socket_topology_filter_local (&pub_filter, "socket-fanout-events", ZLINK_SERVICE_ROLE_PUB,
                                       NULL);
    pub_filter.auto_connect_type = ZLINK_AUTO_CONNECT_FANOUT;
    TEST_ASSERT_TRUE (wait_for_topology_desired_total_local (registry, &pub_filter, 1, 0, 10000));

    TEST_ASSERT_TRUE (destroy_discovery_with_retry_local (&sub_discovery, 3000));
    TEST_ASSERT_TRUE (destroy_discovery_with_retry_local (&pub_discovery, 3000));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_registry_peer_sync_conflict_keeps_deterministic_winner_projection ()
{
    if (!zlink_has ("tcp")) {
        TEST_IGNORE_MESSAGE ("TCP not available");
        return;
    }

    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *winner_registry = zlink_registry_new (ctx);
    void *loser_registry = zlink_registry_new (ctx);
    TEST_ASSERT_NOT_NULL (winner_registry);
    TEST_ASSERT_NOT_NULL (loser_registry);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_set_id (winner_registry, 10));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_set_id (loser_registry, 20));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_set_broadcast_interval (winner_registry, 50));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_set_broadcast_interval (loser_registry, 50));

    char winner_pub[MAX_SOCKET_STRING];
    char winner_router[MAX_SOCKET_STRING];
    char loser_pub[MAX_SOCKET_STRING];
    char loser_router[MAX_SOCKET_STRING];
    char winner_endpoint[MAX_SOCKET_STRING];
    char loser_endpoint[MAX_SOCKET_STRING];
    TEST_ASSERT_TRUE (bind_registry_test_endpoints_local (winner_registry, 6000, winner_pub,
                                                          sizeof (winner_pub), winner_router,
                                                          sizeof (winner_router)));
    TEST_ASSERT_TRUE (bind_registry_test_endpoints_local (
      loser_registry, 6002, loser_pub, sizeof (loser_pub), loser_router, sizeof (loser_router)));
    void *winner_discovery =
      zlink_discovery_new (ctx, ZLINK_AUTO_CONNECT_CLIENT_SERVER, "registry-sync-conflict");
    void *loser_discovery =
      zlink_discovery_new (ctx, ZLINK_AUTO_CONNECT_FANOUT, "registry-sync-conflict");
    TEST_ASSERT_NOT_NULL (winner_discovery);
    TEST_ASSERT_NOT_NULL (loser_discovery);
    TEST_ASSERT_TRUE (
      connect_discovery_registry_with_retry_local (winner_discovery, winner_router, 3000));
    TEST_ASSERT_TRUE (
      connect_discovery_registry_with_retry_local (loser_discovery, loser_router, 3000));

    void *winner_router_socket = zlink_socket (ctx, ZLINK_SOCKET_ROUTER);
    void *loser_pub_socket = zlink_socket (ctx, ZLINK_SOCKET_PUB);
    TEST_ASSERT_NOT_NULL (winner_router_socket);
    TEST_ASSERT_NOT_NULL (loser_pub_socket);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_socket_attach_discovery (winner_router_socket, winner_discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_socket_attach_discovery (loser_pub_socket, loser_discovery));
    TEST_ASSERT_TRUE (bind_socket_test_endpoint_local (winner_router_socket, 6004, winner_endpoint,
                                                       sizeof (winner_endpoint)));
    TEST_ASSERT_TRUE (bind_socket_test_endpoint_local (loser_pub_socket, 6005, loser_endpoint,
                                                       sizeof (loser_endpoint)));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_add_peer (winner_registry, loser_pub));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_add_peer (loser_registry, winner_pub));

    TEST_ASSERT_TRUE (
      wait_for_registry_member_count_local (winner_registry, "registry-sync-conflict", 1, 10000));
    bool saw_winner_projection = zlink_test_wait_until (10000, [=] {
        zlink_member_peer_entry_t entries[4];
        size_t count = 4;
        memset (entries, 0, sizeof (entries));
        if (zlink_registry_member_peers (loser_registry, "registry-sync-conflict", entries, &count)
              == ZLINK_CONFIG_OK
            && count == 1 && entries[0].auto_connect_type == ZLINK_AUTO_CONNECT_CLIENT_SERVER
            && entries[0].service_role == ZLINK_SERVICE_ROLE_ROUTER
            && strcmp (entries[0].endpoint, winner_endpoint) == 0) {
            return true;
        }
        return false;
    });
    TEST_ASSERT_TRUE (saw_winner_projection);

    void *late_loser =
      zlink_discovery_new (ctx, ZLINK_AUTO_CONNECT_FANOUT, "registry-sync-conflict");
    TEST_ASSERT_NOT_NULL (late_loser);
    TEST_ASSERT_TRUE (
      connect_discovery_expect_errno_local (late_loser, loser_router, EEXIST, 3000));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&late_loser));
    TEST_ASSERT_TRUE (destroy_discovery_with_retry_local (&loser_discovery, 3000));
    TEST_ASSERT_TRUE (destroy_discovery_with_retry_local (&winner_discovery, 3000));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&loser_registry));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&winner_registry));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_wss_discovery_destroy_releases_canonicalized_bootstrap_dealer ()
{
#if defined ZLINK_HAVE_WS && defined ZLINK_HAVE_WSS
    if (!zlink_has ("wss")) {
        TEST_IGNORE_MESSAGE ("WSS not available");
        return;
    }

    const tls_test_files_t files = make_tls_test_files ();

    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *registry = zlink_registry_new (ctx);
    TEST_ASSERT_NOT_NULL (registry);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_tls_server (registry, files.server_cert.c_str (), files.server_key.c_str (), 0));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_set_broadcast_interval (registry, 50));

    char registry_pub[MAX_SOCKET_STRING];
    char registry_router[MAX_SOCKET_STRING];
    TEST_ASSERT_TRUE (bind_registry_test_endpoints_transport_local (
      registry, "wss", registry_pub, sizeof (registry_pub), registry_router,
      sizeof (registry_router)));

    char localhost_router[MAX_SOCKET_STRING];
    TEST_ASSERT_TRUE (rewrite_endpoint_host_local (registry_router, "localhost", localhost_router,
                                                   sizeof (localhost_router)));

    void *discovery =
      zlink_discovery_new (ctx, ZLINK_AUTO_CONNECT_CLIENT_SERVER, "wss-canonical-bootstrap");
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_tls_client (discovery, files.ca_cert.c_str (), "localhost", 0));
    TEST_ASSERT_TRUE (
      connect_discovery_registry_with_retry_local (discovery, localhost_router, 5000));

    const std::chrono::steady_clock::time_point before = std::chrono::steady_clock::now ();
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    const std::chrono::steady_clock::time_point after = std::chrono::steady_clock::now ();
    const long elapsed_ms = static_cast<long> (
      std::chrono::duration_cast<std::chrono::milliseconds> (after - before).count ());
    TEST_ASSERT_LESS_THAN (3000, elapsed_ms);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
    cleanup_tls_test_files (files);
#else
    TEST_IGNORE_MESSAGE ("WSS not enabled");
#endif
}

void test_discovery_route_binding_follows_owner_provider_lifecycle ()
{
    if (!zlink_has ("tcp")) {
        TEST_IGNORE_MESSAGE ("TCP not available");
        return;
    }

    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *registry = zlink_registry_new (ctx);
    TEST_ASSERT_NOT_NULL (registry);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_set_broadcast_interval (registry, 50));

    char registry_pub[MAX_SOCKET_STRING];
    char registry_router[MAX_SOCKET_STRING];
    char owner_endpoint[MAX_SOCKET_STRING];
    TEST_ASSERT_TRUE (bind_registry_test_endpoints_local (registry, 6010, registry_pub,
                                                          sizeof (registry_pub), registry_router,
                                                          sizeof (registry_router)));

    const char channel[] = "route-lifecycle";
    void *owner_discovery = zlink_discovery_new (ctx, ZLINK_AUTO_CONNECT_CLIENT_SERVER, channel);
    void *resolver_discovery = zlink_discovery_new (ctx, ZLINK_AUTO_CONNECT_CLIENT_SERVER, channel);
    TEST_ASSERT_NOT_NULL (owner_discovery);
    TEST_ASSERT_NOT_NULL (resolver_discovery);
    TEST_ASSERT_TRUE (
      connect_discovery_registry_with_retry_local (owner_discovery, registry_router, 3000));
    TEST_ASSERT_TRUE (
      connect_discovery_registry_with_retry_local (resolver_discovery, registry_router, 3000));

    void *owner = zlink_socket (ctx, ZLINK_SOCKET_ROUTER);
    TEST_ASSERT_NOT_NULL (owner);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (owner, "route-own", 9));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_socket_attach_discovery (owner, owner_discovery));
    TEST_ASSERT_TRUE (
      bind_socket_test_endpoint_local (owner, 6012, owner_endpoint, sizeof (owner_endpoint)));

    zlink_routing_id_t owner_rid;
    memset (&owner_rid, 0, sizeof (owner_rid));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_routing_id (owner, &owner_rid));
    TEST_ASSERT_TRUE (wait_for_registry_member_count_local (registry, channel, 1, 10000));

    const char route_key[] = "actor-a";
    const char route_value[] = "session-route-v1";
    TEST_ASSERT_TRUE (wait_for_route_bind_ok_local (owner_discovery, ZLINK_ROUTE_KIND_ACTOR_SESSION,
                                                    route_key, strlen (route_key), route_value,
                                                    strlen (route_value), 10000));
    TEST_ASSERT_TRUE (wait_for_route_resolve_local (
      resolver_discovery, ZLINK_ROUTE_KIND_ACTOR_SESSION, route_key, strlen (route_key), owner_rid,
      route_value, strlen (route_value), 10000));

    TEST_ASSERT_TRUE (destroy_discovery_with_retry_local (&owner_discovery, 3000));
    TEST_ASSERT_TRUE (wait_for_route_not_found_local (
      resolver_discovery, ZLINK_ROUTE_KIND_ACTOR_SESSION, route_key, strlen (route_key), 10000));

    TEST_ASSERT_TRUE (destroy_discovery_with_retry_local (&resolver_discovery, 3000));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_discovery_resolve_actor_rejects_invalid_route_rows ()
{
    if (!zlink_has ("tcp")) {
        TEST_IGNORE_MESSAGE ("TCP not available");
        return;
    }

    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *registry = zlink_registry_new (ctx);
    TEST_ASSERT_NOT_NULL (registry);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_set_broadcast_interval (registry, 50));

    char registry_pub[MAX_SOCKET_STRING];
    char registry_router[MAX_SOCKET_STRING];
    char owner_endpoint[MAX_SOCKET_STRING];
    TEST_ASSERT_TRUE (bind_registry_test_endpoints_local (registry, 6030, registry_pub,
                                                          sizeof (registry_pub), registry_router,
                                                          sizeof (registry_router)));

    const char channel[] = "actor-route-invalid";
    void *owner_discovery = zlink_discovery_new (ctx, ZLINK_AUTO_CONNECT_CLIENT_SERVER, channel);
    void *resolver_discovery = zlink_discovery_new (ctx, ZLINK_AUTO_CONNECT_CLIENT_SERVER, channel);
    TEST_ASSERT_NOT_NULL (owner_discovery);
    TEST_ASSERT_NOT_NULL (resolver_discovery);
    TEST_ASSERT_TRUE (
      connect_discovery_registry_with_retry_local (owner_discovery, registry_router, 3000));
    TEST_ASSERT_TRUE (
      connect_discovery_registry_with_retry_local (resolver_discovery, registry_router, 3000));

    void *owner = zlink_socket (ctx, ZLINK_SOCKET_ROUTER);
    TEST_ASSERT_NOT_NULL (owner);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (owner, "actor-own", 9));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_socket_attach_discovery (owner, owner_discovery));
    TEST_ASSERT_TRUE (
      bind_socket_test_endpoint_local (owner, 6032, owner_endpoint, sizeof (owner_endpoint)));
    TEST_ASSERT_TRUE (wait_for_registry_member_count_local (registry, channel, 1, 10000));

    const char actor_id[] = "actor-invalid-route";
    const char short_value[] = "old-node-only";
    TEST_ASSERT_TRUE (wait_for_route_bind_ok_local (owner_discovery, ZLINK_ROUTE_KIND_ACTOR,
                                                    actor_id, strlen (actor_id), short_value,
                                                    strlen (short_value), 10000));
    TEST_ASSERT_TRUE (wait_for_actor_route_not_found_local (resolver_discovery, actor_id, 10000));

    zlink_actor_route_t route;
    memset (&route, 0, sizeof (route));
    route.actor.node_rid.size = 9;
    memcpy (route.actor.node_rid.data, "actor-own", 9);
    strncpy (route.actor.actor_id, "different-actor", sizeof (route.actor.actor_id) - 1);
    route.current_spot_rid.size = 8;
    memcpy (route.current_spot_rid.data, "spot-rid", 8);
    route.current_spot_kind = ZLINK_SPOT_KIND_USER;
    TEST_ASSERT_TRUE (wait_for_route_bind_ok_local (owner_discovery, ZLINK_ROUTE_KIND_ACTOR,
                                                    actor_id, strlen (actor_id), &route,
                                                    sizeof (route), 10000));
    TEST_ASSERT_TRUE (wait_for_actor_route_not_found_local (resolver_discovery, actor_id, 10000));

    memset (&route, 0, sizeof (route));
    route.actor.node_rid.size = 9;
    memcpy (route.actor.node_rid.data, "actor-own", 9);
    strncpy (route.actor.actor_id, actor_id, sizeof (route.actor.actor_id) - 1);
    route.current_spot_rid.size = 8;
    memcpy (route.current_spot_rid.data, "spot-rid", 8);
    route.current_spot_kind = ZLINK_SPOT_KIND_INVALID;
    TEST_ASSERT_TRUE (wait_for_route_bind_ok_local (owner_discovery, ZLINK_ROUTE_KIND_ACTOR,
                                                    actor_id, strlen (actor_id), &route,
                                                    sizeof (route), 10000));
    TEST_ASSERT_TRUE (wait_for_actor_route_not_found_local (resolver_discovery, actor_id, 10000));

    TEST_ASSERT_TRUE (destroy_discovery_with_retry_local (&resolver_discovery, 3000));
    TEST_ASSERT_TRUE (destroy_discovery_with_retry_local (&owner_discovery, 3000));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_registry_peer_sync_propagates_route_binding_snapshot ()
{
    if (!zlink_has ("tcp")) {
        TEST_IGNORE_MESSAGE ("TCP not available");
        return;
    }

    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *source_registry = zlink_registry_new (ctx);
    void *peer_registry = zlink_registry_new (ctx);
    TEST_ASSERT_NOT_NULL (source_registry);
    TEST_ASSERT_NOT_NULL (peer_registry);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_set_id (source_registry, 31));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_set_id (peer_registry, 32));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_set_broadcast_interval (source_registry, 50));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_set_broadcast_interval (peer_registry, 50));

    char source_pub[MAX_SOCKET_STRING];
    char source_router[MAX_SOCKET_STRING];
    char peer_pub[MAX_SOCKET_STRING];
    char peer_router[MAX_SOCKET_STRING];
    char owner_endpoint[MAX_SOCKET_STRING];
    TEST_ASSERT_TRUE (bind_registry_test_endpoints_local (source_registry, 6020, source_pub,
                                                          sizeof (source_pub), source_router,
                                                          sizeof (source_router)));
    TEST_ASSERT_TRUE (bind_registry_test_endpoints_local (
      peer_registry, 6022, peer_pub, sizeof (peer_pub), peer_router, sizeof (peer_router)));

    const char channel[] = "route-peer-sync";
    void *owner_discovery = zlink_discovery_new (ctx, ZLINK_AUTO_CONNECT_CLIENT_SERVER, channel);
    void *resolver_discovery = zlink_discovery_new (ctx, ZLINK_AUTO_CONNECT_CLIENT_SERVER, channel);
    TEST_ASSERT_NOT_NULL (owner_discovery);
    TEST_ASSERT_NOT_NULL (resolver_discovery);
    TEST_ASSERT_TRUE (
      connect_discovery_registry_with_retry_local (owner_discovery, source_router, 3000));
    TEST_ASSERT_TRUE (
      connect_discovery_registry_with_retry_local (resolver_discovery, peer_router, 3000));

    void *owner = zlink_socket (ctx, ZLINK_SOCKET_ROUTER);
    TEST_ASSERT_NOT_NULL (owner);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (owner, "route-src", 9));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_socket_attach_discovery (owner, owner_discovery));
    TEST_ASSERT_TRUE (
      bind_socket_test_endpoint_local (owner, 6024, owner_endpoint, sizeof (owner_endpoint)));

    zlink_routing_id_t owner_rid;
    memset (&owner_rid, 0, sizeof (owner_rid));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_routing_id (owner, &owner_rid));
    TEST_ASSERT_TRUE (wait_for_registry_member_count_local (source_registry, channel, 1, 10000));

    const char route_key[] = "spot-name:room-a";
    const char route_value[] = "spot-rid-room-a";
    TEST_ASSERT_TRUE (wait_for_route_bind_ok_local (owner_discovery, ZLINK_ROUTE_KIND_SPOT_NAME,
                                                    route_key, strlen (route_key), route_value,
                                                    strlen (route_value), 10000));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_add_peer (source_registry, peer_pub));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_add_peer (peer_registry, source_pub));

    TEST_ASSERT_TRUE (wait_for_route_resolve_local (resolver_discovery, ZLINK_ROUTE_KIND_SPOT_NAME,
                                                    route_key, strlen (route_key), owner_rid,
                                                    route_value, strlen (route_value), 10000));

    TEST_ASSERT_TRUE (destroy_discovery_with_retry_local (&resolver_discovery, 3000));
    TEST_ASSERT_TRUE (destroy_discovery_with_retry_local (&owner_discovery, 3000));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&peer_registry));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&source_registry));
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
    RUN_TEST (test_socket_discovery_router_router_route_mesh_initiates_both_directions);
    RUN_TEST (test_route_mesh_discovery_router_router_delivers_payload_both_directions);
    RUN_TEST (test_route_mesh_discovery_three_routers_deliver_high_to_middle);
    RUN_TEST (test_route_mesh_discovery_router_router_delivers_payload_across_contexts);
    RUN_TEST (test_route_mesh_discovery_three_routers_deliver_high_to_middle_across_contexts);
    RUN_TEST (test_discovery_channel_contract_rejects_type_conflict_and_persists);
    RUN_TEST (test_client_server_dealer_connects_all_routers_and_registry_queries_use_channel);
    RUN_TEST (test_endpointless_client_server_dealer_is_not_member_but_reports_connect_intent);
    RUN_TEST (test_fanout_sub_connects_pub_and_endpointless_sub_is_not_member);
    RUN_TEST (test_registry_peer_sync_conflict_keeps_deterministic_winner_projection);
    RUN_TEST (test_wss_discovery_destroy_releases_canonicalized_bootstrap_dealer);
    RUN_TEST (test_discovery_route_binding_follows_owner_provider_lifecycle);
    RUN_TEST (test_discovery_resolve_actor_rejects_invalid_route_rows);
    RUN_TEST (test_registry_peer_sync_propagates_route_binding_snapshot);
    return UNITY_END ();
}
