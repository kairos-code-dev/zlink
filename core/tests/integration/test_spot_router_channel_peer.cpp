/* SPDX-License-Identifier: MPL-2.0 */

#include "testutil.hpp"
#include "testutil_unity.hpp"

#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace
{
struct reply_probe_t
{
    std::mutex mutex;
    std::condition_variable cv;
    bool done = false;
    zlink_request_result_t result = ZLINK_REQUEST_PROTOCOL_ERROR;
    std::string payload;
    void *progress_handle = NULL;
};

struct spot_request_probe_t
{
    std::mutex mutex;
    std::condition_variable cv;
    bool invoked = false;
    zlink_routing_id_t source_rid;
    uint64_t request_seq = 0;
    std::string payload;

    spot_request_probe_t ()
    {
        memset (&source_rid, 0, sizeof (source_rid));
    }
};

void init_string_part (zlink_msg_t *part_, const char *text_)
{
    const size_t size = strlen (text_);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (part_, size));
    memcpy (zlink_msg_data (part_), text_, size);
}

void set_routing_id_text (void *handle_, const char *text_)
{
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_routing_id (handle_, text_, strlen (text_)));
}

zlink_routing_id_t get_routing_id_value (void *handle_)
{
    zlink_routing_id_t rid;
    memset (&rid, 0, sizeof (rid));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_routing_id (handle_, &rid));
    return rid;
}

std::string msg_to_string (const zlink_msg_t *part_)
{
    zlink_msg_t *mutable_part = const_cast<zlink_msg_t *> (part_);
    return std::string (static_cast<const char *> (zlink_msg_data (mutable_part)),
                        zlink_msg_size (mutable_part));
}

bool recv_spot_payload (void *spot_, std::string *payload_out_)
{
    const auto deadline =
      std::chrono::steady_clock::now () + std::chrono::milliseconds (1500);
    while (std::chrono::steady_clock::now () < deadline) {
        const zlink_routing_id_t *source_node_rid = NULL;
        const zlink_routing_id_t *source_spot_rid = NULL;
        uint64_t request_seq = 0;
        zlink_msg_t *parts = NULL;
        size_t part_count = 0;
        const zlink_recv_result_t rc = zlink_spot_recv (
          spot_, &source_node_rid, &source_spot_rid, &request_seq, &parts,
          &part_count, ZLINK_DONTWAIT);
        if (rc == ZLINK_RECV_OK) {
            if (payload_out_ && part_count > 0)
                *payload_out_ = msg_to_string (&parts[0]);
            zlink_multipart_close (parts, part_count);
            return true;
        }
        TEST_ASSERT_EQUAL_INT (EAGAIN, zlink_errno ());
        std::this_thread::sleep_for (std::chrono::milliseconds (10));
    }
    return false;
}

void bind_router (void *router_, char *endpoint_, size_t endpoint_capacity_)
{
    TEST_ASSERT_SUCCESS_ERRNO (zlink_bind (router_, "tcp://127.0.0.1:*"));
    size_t endpoint_len = endpoint_capacity_;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_get_option (router_, ZLINK_OPT_LAST_ENDPOINT, endpoint_,
                        &endpoint_len));
}

int drain_completion_via_poller (void *subject_)
{
    void *poller = zlink_poller_new ();
    if (!poller)
        return -1;
    int rc = -1;
    if (zlink_poller_add (poller, subject_, NULL, ZLINK_POLLCOMPLETION)
        == ZLINK_CONFIG_OK) {
        zlink_poller_event_t event;
        rc = zlink_poller_wait (poller, &event, 1, 0, NULL);
        (void) zlink_poller_remove (poller, subject_);
    }
    (void) zlink_poller_destroy (&poller);
    return rc;
}

void capture_reply (zlink_request_result_t result_,
                    zlink_msg_t *parts_,
                    size_t part_count_,
                    void *userdata_)
{
    reply_probe_t *probe = static_cast<reply_probe_t *> (userdata_);
    if (!probe)
        return;

    {
        std::lock_guard<std::mutex> lock (probe->mutex);
        probe->done = true;
        probe->result = result_;
        if (parts_ && part_count_ > 0)
            probe->payload = msg_to_string (&parts_[0]);
    }
    if (parts_ && part_count_ > 0)
        zlink_multipart_close (parts_, part_count_);
    probe->cv.notify_all ();
}

bool wait_for_reply (reply_probe_t *probe_)
{
    const auto deadline =
      std::chrono::steady_clock::now () + std::chrono::milliseconds (2000);
    while (std::chrono::steady_clock::now () < deadline) {
        {
            std::unique_lock<std::mutex> lock (probe_->mutex);
            if (probe_->cv.wait_for (
                  lock, std::chrono::milliseconds (10),
                  [probe_]() { return probe_->done; }))
                return true;
        }
        if (probe_->progress_handle)
            (void) drain_completion_via_poller (probe_->progress_handle);
    }
    return false;
}

void capture_spot_request (const zlink_routing_id_t *source_rid_,
                           const zlink_routing_id_t *,
                           uint64_t request_seq_,
                           zlink_msg_t *parts_,
                           size_t part_count_,
                           void *userdata_)
{
    spot_request_probe_t *probe =
      static_cast<spot_request_probe_t *> (userdata_);
    if (!probe)
        return;

    {
        std::lock_guard<std::mutex> lock (probe->mutex);
        probe->invoked = true;
        if (source_rid_)
            probe->source_rid = *source_rid_;
        probe->request_seq = request_seq_;
        if (parts_ && part_count_ > 0)
            probe->payload = msg_to_string (&parts_[0]);
    }
    if (parts_ && part_count_ > 0)
    zlink_multipart_close (parts_, part_count_);
    probe->cv.notify_all ();
}

void capture_spot_request_dispatch (void *spot_,
                                    const zlink_spot_dispatch_info_t *info_,
                                    void *userdata_)
{
    if (!info_
        || info_->event != ZLINK_SPOT_DISPATCH_EVENT_ROUTED_READABLE)
        return;

    const zlink_routing_id_t *source_rid = NULL;
    const zlink_routing_id_t *spot_rid = NULL;
    uint64_t request_seq = 0;
    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    if (zlink_spot_recv (spot_, &source_rid, &spot_rid, &request_seq, &parts,
                         &part_count, ZLINK_DONTWAIT)
        != ZLINK_RECV_OK) {
        return;
    }
    capture_spot_request (source_rid, spot_rid, request_seq, parts, part_count,
                          userdata_);
}

bool wait_for_spot_request (spot_request_probe_t *probe_)
{
    std::unique_lock<std::mutex> lock (probe_->mutex);
    return probe_->cv.wait_for (
      lock, std::chrono::milliseconds (2000),
      [probe_]() { return probe_->invoked; });
}

bool allocate_loopback_tcp_endpoint (char *endpoint_out_,
                                     size_t endpoint_size_)
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

bool bind_registry_test_endpoints (void *registry_,
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

    for (int i = 0; i < 256; ++i) {
        if (!allocate_loopback_tcp_endpoint (pub_out_, pub_size_)
            || !allocate_loopback_tcp_endpoint (router_out_, router_size_)
            || strcmp (pub_out_, router_out_) == 0)
            continue;
        if (zlink_registry_bind (registry_, pub_out_, router_out_)
            == ZLINK_BIND_OK)
            return true;
    }

    errno = EADDRINUSE;
    return false;
}

bool connect_discovery_registry_with_retry (void *discovery_,
                                            const char *endpoint_,
                                            int timeout_ms_)
{
    return zlink_test_wait_until (timeout_ms_, [=] {
        return zlink_discovery_connect_registry (discovery_, endpoint_)
               == ZLINK_CONNECT_OK;
    });
}

bool destroy_discovery_with_retry (void **discovery_p_, int timeout_ms_)
{
    return zlink_test_wait_until_result (timeout_ms_, [=] {
        if (zlink_discovery_destroy (discovery_p_) == ZLINK_CLOSE_OK)
            return zlink_test_wait_done;
        if (zlink_errno () != EBUSY)
            return zlink_test_wait_failed;
        return zlink_test_wait_retry;
    });
}

bool wait_for_discovery_member_role_count (void *discovery_,
                                           zlink_service_role_t service_role_,
                                           size_t expected_count_,
                                           int timeout_ms_)
{
    return zlink_test_wait_until (timeout_ms_, [=] {
        zlink_member_peer_entry_t entries[8];
        size_t count = 8;
        memset (entries, 0, sizeof (entries));
        if (zlink_discovery_member_peers (discovery_, entries, &count)
            != ZLINK_CONFIG_OK)
            return false;
        size_t matched = 0;
        for (size_t i = 0; i < count; ++i) {
            if (entries[i].service_role == service_role_)
                ++matched;
        }
        return matched >= expected_count_;
    });
}

bool wait_for_router_channel_peer_snapshot (void *node_,
                                            const char *channel_name_,
                                            const char *endpoint_,
                                            zlink_spot_peer_source_t source_,
                                            int timeout_ms_)
{
    return zlink_test_wait_until (timeout_ms_, [=] {
        zlink_spot_node_peer_entry_t entries[8];
        size_t count = 8;
        memset (entries, 0, sizeof (entries));
        if (zlink_spot_node_peers (node_, NULL, entries, &count)
            != ZLINK_CONFIG_OK)
            return false;
        for (size_t i = 0; i < count; ++i) {
            if (strcmp (entries[i].channel_name, channel_name_) == 0
                && strcmp (entries[i].peer_endpoint, endpoint_) == 0
                && entries[i].source == source_
                && entries[i].kind == ZLINK_SPOT_PEER_KIND_ROUTER_CHANNEL)
                return true;
        }
        return false;
    });
}

bool wait_for_router_channel_peer_absent (void *node_,
                                          const char *channel_name_,
                                          const char *endpoint_,
                                          int timeout_ms_)
{
    return zlink_test_wait_until (timeout_ms_, [=] {
        zlink_spot_node_peer_entry_t entries[8];
        size_t count = 8;
        memset (entries, 0, sizeof (entries));
        if (zlink_spot_node_peers (node_, NULL, entries, &count)
            != ZLINK_CONFIG_OK)
            return false;
        for (size_t i = 0; i < count; ++i) {
            if (strcmp (entries[i].channel_name, channel_name_) == 0
                && strcmp (entries[i].peer_endpoint, endpoint_) == 0
                && entries[i].kind == ZLINK_SPOT_PEER_KIND_ROUTER_CHANNEL)
                return false;
        }
        return true;
    });
}
}

void test_spot_node_router_channel_manual_send_spot ()
{
    void *ctx = zlink_ctx_new ();
    void *node = zlink_spot_node_new (ctx, NULL);
    void *spot = zlink_spot_new (node);
    void *router = zlink_socket (ctx, ZLINK_SOCKET_ROUTER);
    TEST_ASSERT_NOT_NULL (ctx);
    TEST_ASSERT_NOT_NULL (node);
    TEST_ASSERT_NOT_NULL (spot);
    TEST_ASSERT_NOT_NULL (router);

    set_routing_id_text (node, "spot-route-node");
    set_routing_id_text (spot, "spot-route-target");
    set_routing_id_text (router, "spot-route-router");

    char endpoint[MAX_SOCKET_STRING];
    bind_router (router, endpoint, sizeof (endpoint));

    TEST_ASSERT_EQUAL (
      ZLINK_CONNECT_OK,
      zlink_spot_node_connect_router_channel_peer (node, "api", endpoint));
    msleep (SETTLE_TIME);

    const zlink_routing_id_t node_rid = get_routing_id_value (node);
    const zlink_routing_id_t spot_rid = get_routing_id_value (spot);
    zlink_msg_t part;
    init_string_part (&part, "router-channel-payload");
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_router_send_spot (router, &node_rid, &spot_rid, &part, 1, 0));

    std::string payload;
    TEST_ASSERT_TRUE (recv_spot_payload (spot, &payload));
    TEST_ASSERT_EQUAL_STRING ("router-channel-payload", payload.c_str ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (router));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_spot_node_router_channel_manual_send_spot_part ()
{
    void *ctx = zlink_ctx_new ();
    void *node = zlink_spot_node_new (ctx, NULL);
    void *spot = zlink_spot_new (node);
    void *router = zlink_socket (ctx, ZLINK_SOCKET_ROUTER);
    TEST_ASSERT_NOT_NULL (ctx);
    TEST_ASSERT_NOT_NULL (node);
    TEST_ASSERT_NOT_NULL (spot);
    TEST_ASSERT_NOT_NULL (router);

    set_routing_id_text (node, "spot-route-part-node");
    set_routing_id_text (spot, "spot-route-part-target");
    set_routing_id_text (router, "spot-route-part-router");

    char endpoint[MAX_SOCKET_STRING];
    bind_router (router, endpoint, sizeof (endpoint));

    TEST_ASSERT_EQUAL (
      ZLINK_CONNECT_OK,
      zlink_spot_node_connect_router_channel_peer (node, "api", endpoint));
    msleep (SETTLE_TIME);

    const zlink_routing_id_t node_rid = get_routing_id_value (node);
    const zlink_routing_id_t spot_rid = get_routing_id_value (spot);
    zlink_msg_t part;
    init_string_part (&part, "router-channel-part-payload");
    TEST_ASSERT_EQUAL (
      ZLINK_SUBMIT_OK,
      zlink_router_send_spot_part (router, &node_rid, &spot_rid, &part,
                                   ZLINK_SEND_FLAGS_NONE, ZLINK_PART_FINAL));

    std::string payload;
    TEST_ASSERT_TRUE (recv_spot_payload (spot, &payload));
    TEST_ASSERT_EQUAL_STRING ("router-channel-part-payload", payload.c_str ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (router));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_spot_node_router_channel_duplicate_manual_connect_is_idempotent ()
{
    void *ctx = zlink_ctx_new ();
    void *node = zlink_spot_node_new (ctx, NULL);
    void *router = zlink_socket (ctx, ZLINK_SOCKET_ROUTER);
    TEST_ASSERT_NOT_NULL (ctx);
    TEST_ASSERT_NOT_NULL (node);
    TEST_ASSERT_NOT_NULL (router);

    set_routing_id_text (node, "dup-route-node");
    set_routing_id_text (router, "dup-route-router");

    char endpoint[MAX_SOCKET_STRING];
    bind_router (router, endpoint, sizeof (endpoint));

    TEST_ASSERT_EQUAL (
      ZLINK_CONNECT_OK,
      zlink_spot_node_connect_router_channel_peer (node, "api", endpoint));
    TEST_ASSERT_EQUAL (
      ZLINK_CONNECT_OK,
      zlink_spot_node_connect_router_channel_peer (node, "api", endpoint));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (router));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_spot_node_router_channel_manual_request_spot ()
{
    void *ctx = zlink_ctx_new ();
    void *node = zlink_spot_node_new (ctx, NULL);
    void *spot = zlink_spot_new (node);
    void *router = zlink_socket (ctx, ZLINK_SOCKET_ROUTER);
    TEST_ASSERT_NOT_NULL (ctx);
    TEST_ASSERT_NOT_NULL (node);
    TEST_ASSERT_NOT_NULL (spot);
    TEST_ASSERT_NOT_NULL (router);

    set_routing_id_text (node, "spot-route-req-node");
    set_routing_id_text (spot, "spot-route-req-target");
    set_routing_id_text (router, "spot-route-req-router");

    char endpoint[MAX_SOCKET_STRING];
    bind_router (router, endpoint, sizeof (endpoint));

    TEST_ASSERT_EQUAL (
      ZLINK_CONNECT_OK,
      zlink_spot_node_connect_router_channel_peer (node, "api", endpoint));
    msleep (SETTLE_TIME);

    spot_request_probe_t request_probe;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_dispatch_event_handler (
      spot, &capture_spot_request_dispatch, &request_probe));

    const zlink_routing_id_t node_rid = get_routing_id_value (node);
    const zlink_routing_id_t spot_rid = get_routing_id_value (spot);
    zlink_msg_t request_part;
    init_string_part (&request_part, "router-channel-request");
    reply_probe_t reply_probe;
    reply_probe.progress_handle = router;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_router_request_spot (
      router, &node_rid, &spot_rid, &request_part, 1, &capture_reply,
      &reply_probe, 0, 3000));

    TEST_ASSERT_TRUE (wait_for_spot_request (&request_probe));
    TEST_ASSERT_EQUAL_STRING ("router-channel-request",
                              request_probe.payload.c_str ());

    zlink_msg_t reply_part;
    init_string_part (&reply_part, "router-channel-reply");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_reply_router (
      spot, &request_probe.source_rid, request_probe.request_seq, &reply_part,
      1));

    TEST_ASSERT_TRUE (wait_for_reply (&reply_probe));
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK, reply_probe.result);
    TEST_ASSERT_EQUAL_STRING ("router-channel-reply",
                              reply_probe.payload.c_str ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (router));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_spot_node_router_channel_disconnect_blocks_delivery ()
{
    void *ctx = zlink_ctx_new ();
    void *node = zlink_spot_node_new (ctx, NULL);
    void *spot = zlink_spot_new (node);
    void *router = zlink_socket (ctx, ZLINK_SOCKET_ROUTER);
    TEST_ASSERT_NOT_NULL (ctx);
    TEST_ASSERT_NOT_NULL (node);
    TEST_ASSERT_NOT_NULL (spot);
    TEST_ASSERT_NOT_NULL (router);

    set_routing_id_text (node, "spot-route-disc-node");
    set_routing_id_text (spot, "spot-route-disc-target");
    set_routing_id_text (router, "spot-route-disc-router");

    char endpoint[MAX_SOCKET_STRING];
    bind_router (router, endpoint, sizeof (endpoint));

    TEST_ASSERT_EQUAL (
      ZLINK_CONNECT_OK,
      zlink_spot_node_connect_router_channel_peer (node, "api", endpoint));
    msleep (SETTLE_TIME);
    TEST_ASSERT_TRUE (wait_for_router_channel_peer_snapshot (
      node, "api", endpoint, ZLINK_SPOT_PEER_SOURCE_MANUAL, 2000));

    const zlink_routing_id_t node_rid = get_routing_id_value (node);
    const zlink_routing_id_t spot_rid = get_routing_id_value (spot);
    zlink_msg_t first_part;
    init_string_part (&first_part, "before-disconnect");
    TEST_ASSERT_EQUAL (
      ZLINK_SUBMIT_OK,
      zlink_router_send_spot (router, &node_rid, &spot_rid, &first_part, 1,
                              0));

    std::string payload;
    TEST_ASSERT_TRUE (recv_spot_payload (spot, &payload));
    TEST_ASSERT_EQUAL_STRING ("before-disconnect", payload.c_str ());

    TEST_ASSERT_EQUAL (
      ZLINK_CONNECT_OK,
      zlink_spot_node_disconnect_router_channel_peer (node, "api", endpoint));
    TEST_ASSERT_TRUE (
      wait_for_router_channel_peer_absent (node, "api", endpoint, 2000));

    TEST_ASSERT_TRUE (zlink_test_wait_until (2000, [=] {
        zlink_msg_t blocked_part;
        init_string_part (&blocked_part, "after-disconnect");
        const zlink_submit_result_t rc = zlink_router_send_spot (
          router, &node_rid, &spot_rid, &blocked_part, 1, ZLINK_DONTWAIT);
        return rc != ZLINK_SUBMIT_OK;
    }));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (router));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_spot_node_router_channel_discovery_connects_new_peer ()
{
    if (!zlink_has ("tcp")) {
        TEST_IGNORE_MESSAGE ("TCP not available");
        return;
    }

    void *ctx = zlink_ctx_new ();
    void *registry = zlink_registry_new (ctx);
    void *node = zlink_spot_node_new (ctx, NULL);
    void *spot = zlink_spot_new (node);
    void *router = zlink_socket (ctx, ZLINK_SOCKET_ROUTER);
    TEST_ASSERT_NOT_NULL (ctx);
    TEST_ASSERT_NOT_NULL (registry);
    TEST_ASSERT_NOT_NULL (node);
    TEST_ASSERT_NOT_NULL (spot);
    TEST_ASSERT_NOT_NULL (router);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_registry_set_broadcast_interval (registry, 50));

    char registry_pub[MAX_SOCKET_STRING];
    char registry_router[MAX_SOCKET_STRING];
    TEST_ASSERT_TRUE (bind_registry_test_endpoints (
      registry, registry_pub, sizeof (registry_pub), registry_router,
      sizeof (registry_router)));

    void *router_discovery =
      zlink_discovery_new (ctx, ZLINK_AUTO_CONNECT_CLIENT_SERVER, "api");
    void *node_discovery =
      zlink_discovery_new (ctx, ZLINK_AUTO_CONNECT_CLIENT_SERVER, "api");
    TEST_ASSERT_NOT_NULL (router_discovery);
    TEST_ASSERT_NOT_NULL (node_discovery);
    TEST_ASSERT_TRUE (connect_discovery_registry_with_retry (
      router_discovery, registry_router, 3000));
    TEST_ASSERT_TRUE (connect_discovery_registry_with_retry (
      node_discovery, registry_router, 3000));

    set_routing_id_text (node, "spot-route-auto-node");
    set_routing_id_text (spot, "spot-route-auto-target");
    set_routing_id_text (router, "spot-route-auto-router");

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_socket_attach_discovery (router, router_discovery));
    char endpoint[MAX_SOCKET_STRING];
    bind_router (router, endpoint, sizeof (endpoint));
    const zlink_routing_id_t router_rid = get_routing_id_value (router);

    TEST_ASSERT_EQUAL (
      ZLINK_CONFIG_OK,
      zlink_spot_node_attach_router_channel_discovery (
        node, "api", node_discovery));
    TEST_ASSERT_TRUE (wait_for_discovery_member_role_count (
      node_discovery, ZLINK_SERVICE_ROLE_ROUTER, 1, 10000));
    TEST_ASSERT_TRUE (wait_for_router_channel_peer_snapshot (
      node, "api", endpoint, ZLINK_SPOT_PEER_SOURCE_DISCOVERY, 10000));

    const zlink_routing_id_t node_rid = get_routing_id_value (node);
    const zlink_routing_id_t spot_rid = get_routing_id_value (spot);
    zlink_msg_t part;
    init_string_part (&part, "discovery-router-channel");
    TEST_ASSERT_EQUAL (
      ZLINK_SUBMIT_OK,
      zlink_router_send_spot (router, &node_rid, &spot_rid, &part, 1, 0));

    std::string payload;
    TEST_ASSERT_TRUE (recv_spot_payload (spot, &payload));
    TEST_ASSERT_EQUAL_STRING ("discovery-router-channel", payload.c_str ());

    TEST_ASSERT_EQUAL (
      ZLINK_CONNECT_OK,
      zlink_spot_node_disconnect_router_channel_peer_rid (
        node, "api", &router_rid));
    TEST_ASSERT_TRUE (
      wait_for_router_channel_peer_absent (node, "api", endpoint, 2000));
    TEST_ASSERT_TRUE (zlink_test_wait_until (2000, [=] {
        zlink_msg_t blocked_part;
        init_string_part (&blocked_part, "after-discovery-rid-disconnect");
        const zlink_submit_result_t rc = zlink_router_send_spot (
          router, &node_rid, &spot_rid, &blocked_part, 1, ZLINK_DONTWAIT);
        return rc != ZLINK_SUBMIT_OK;
    }));

    TEST_ASSERT_TRUE (destroy_discovery_with_retry (&node_discovery, 3000));
    TEST_ASSERT_TRUE (destroy_discovery_with_retry (&router_discovery, 3000));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_spot_node_router_channel_manual_and_discovery_conflict ()
{
    void *ctx = zlink_ctx_new ();
    void *node = zlink_spot_node_new (ctx, NULL);
    void *router = zlink_socket (ctx, ZLINK_SOCKET_ROUTER);
    void *discovery =
      zlink_discovery_new (ctx, ZLINK_AUTO_CONNECT_CLIENT_SERVER, "api");
    TEST_ASSERT_NOT_NULL (ctx);
    TEST_ASSERT_NOT_NULL (node);
    TEST_ASSERT_NOT_NULL (router);
    TEST_ASSERT_NOT_NULL (discovery);

    set_routing_id_text (node, "spot-route-conflict-node");
    set_routing_id_text (router, "spot-route-conflict-router");

    char endpoint[MAX_SOCKET_STRING];
    bind_router (router, endpoint, sizeof (endpoint));

    TEST_ASSERT_EQUAL (
      ZLINK_CONNECT_OK,
      zlink_spot_node_connect_router_channel_peer (node, "api", endpoint));
    TEST_ASSERT_EQUAL (
      ZLINK_CONFIG_INVALID_STATE,
      zlink_spot_node_attach_router_channel_discovery (node, "api",
                                                       discovery));
    TEST_ASSERT_EQUAL (EBUSY, zlink_errno ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (router));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_spot_node_router_channel_rejects_invalid_channel_name ()
{
    void *ctx = zlink_ctx_new ();
    void *node = zlink_spot_node_new (ctx, NULL);
    TEST_ASSERT_NOT_NULL (ctx);
    TEST_ASSERT_NOT_NULL (node);

    TEST_ASSERT_EQUAL (
      ZLINK_CONNECT_INVALID_ARGUMENT,
      zlink_spot_node_connect_router_channel_peer (
        node, "", "tcp://127.0.0.1:1"));
    TEST_ASSERT_EQUAL (EINVAL, zlink_errno ());
    TEST_ASSERT_EQUAL (
      ZLINK_CONNECT_INVALID_ARGUMENT,
      zlink_spot_node_connect_router_channel_peer (
        node, NULL, "tcp://127.0.0.1:1"));
    TEST_ASSERT_EQUAL (EINVAL, zlink_errno ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

int main ()
{
    setup_test_environment ();

    UNITY_BEGIN ();
    RUN_TEST (test_spot_node_router_channel_manual_send_spot);
    RUN_TEST (test_spot_node_router_channel_manual_send_spot_part);
    RUN_TEST (test_spot_node_router_channel_duplicate_manual_connect_is_idempotent);
    RUN_TEST (test_spot_node_router_channel_manual_request_spot);
    RUN_TEST (test_spot_node_router_channel_disconnect_blocks_delivery);
    RUN_TEST (test_spot_node_router_channel_discovery_connects_new_peer);
    RUN_TEST (test_spot_node_router_channel_manual_and_discovery_conflict);
    RUN_TEST (test_spot_node_router_channel_rejects_invalid_channel_name);
    return UNITY_END ();
}
