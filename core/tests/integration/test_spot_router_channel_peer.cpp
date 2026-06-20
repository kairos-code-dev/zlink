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
extern "C" int zlink_spot_drain_external_router_ingress (void *node_);

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
    zlink_routing_id_t source_spot_rid;
    uint64_t request_seq = 0;
    std::string payload;

    spot_request_probe_t ()
    {
        memset (&source_rid, 0, sizeof (source_rid));
        memset (&source_spot_rid, 0, sizeof (source_spot_rid));
    }
};

struct spot_request_record_t
{
    zlink_routing_id_t source_rid;
    zlink_routing_id_t source_spot_rid;
    uint64_t request_seq = 0;
    std::string payload;

    spot_request_record_t ()
    {
        memset (&source_rid, 0, sizeof (source_rid));
        memset (&source_spot_rid, 0, sizeof (source_spot_rid));
    }
};

struct spot_request_list_probe_t
{
    std::mutex mutex;
    std::condition_variable cv;
    std::vector<spot_request_record_t> records;
};

void init_string_part (zlink_msg_t *part_, const char *text_)
{
    const size_t size = strlen (text_);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (part_, size));
    memcpy (zlink_msg_data (part_), text_, size);
}

void set_routing_id_text (void *handle_, const char *text_)
{
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (handle_, text_, strlen (text_)));
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
    const auto deadline = std::chrono::steady_clock::now () + std::chrono::milliseconds (1500);
    while (std::chrono::steady_clock::now () < deadline) {
        const zlink_routing_id_t *source_node_rid = NULL;
        const zlink_routing_id_t *source_spot_rid = NULL;
        uint64_t request_seq = 0;
        zlink_msg_t *parts = NULL;
        size_t part_count = 0;
        const zlink_recv_result_t rc =
          zlink_spot_recv (spot_, &source_node_rid, &source_spot_rid, &request_seq, &parts,
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
      zlink_get_option (router_, ZLINK_OPT_LAST_ENDPOINT, endpoint_, &endpoint_len));
}

int drain_completion_via_poller (void *subject_)
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
    const auto deadline = std::chrono::steady_clock::now () + std::chrono::milliseconds (2000);
    while (std::chrono::steady_clock::now () < deadline) {
        {
            std::unique_lock<std::mutex> lock (probe_->mutex);
            if (probe_->cv.wait_for (lock, std::chrono::milliseconds (10),
                                     [probe_] () { return probe_->done; }))
                return true;
        }
        if (probe_->progress_handle)
            (void) drain_completion_via_poller (probe_->progress_handle);
    }
    return false;
}

void capture_spot_request (const zlink_routing_id_t *source_rid_,
                           const zlink_routing_id_t *spot_rid_,
                           uint64_t request_seq_,
                           zlink_msg_t *parts_,
                           size_t part_count_,
                           void *userdata_)
{
    spot_request_probe_t *probe = static_cast<spot_request_probe_t *> (userdata_);
    if (!probe)
        return;

    {
        std::lock_guard<std::mutex> lock (probe->mutex);
        probe->invoked = true;
        if (source_rid_)
            probe->source_rid = *source_rid_;
        if (spot_rid_)
            probe->source_spot_rid = *spot_rid_;
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
    if (!info_ || info_->event != ZLINK_SPOT_DISPATCH_EVENT_ROUTED_READABLE)
        return;

    const zlink_routing_id_t *source_rid = NULL;
    const zlink_routing_id_t *spot_rid = NULL;
    uint64_t request_seq = 0;
    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    if (zlink_spot_recv (spot_, &source_rid, &spot_rid, &request_seq, &parts, &part_count,
                         ZLINK_DONTWAIT)
        != ZLINK_RECV_OK) {
        return;
    }
    capture_spot_request (source_rid, spot_rid, request_seq, parts, part_count, userdata_);
}

void capture_spot_request_list_dispatch (void *spot_,
                                         const zlink_spot_dispatch_info_t *info_,
                                         void *userdata_)
{
    spot_request_list_probe_t *probe =
      static_cast<spot_request_list_probe_t *> (userdata_);
    if (!probe || !info_ || info_->event != ZLINK_SPOT_DISPATCH_EVENT_ROUTED_READABLE)
        return;

    while (true) {
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

        {
            std::lock_guard<std::mutex> lock (probe->mutex);
            spot_request_record_t record;
            if (source_rid)
                record.source_rid = *source_rid;
            if (spot_rid)
                record.source_spot_rid = *spot_rid;
            record.request_seq = request_seq;
            if (parts && part_count > 0)
                record.payload = msg_to_string (&parts[0]);
            probe->records.push_back (record);
        }
        if (parts && part_count > 0)
            zlink_multipart_close (parts, part_count);
        probe->cv.notify_all ();
    }
}

void drain_source_reply_dispatch (void *spot_,
                                  const zlink_spot_dispatch_info_t *info_,
                                  void *userdata_)
{
    (void) userdata_;
    if (!info_ || info_->event != ZLINK_SPOT_DISPATCH_EVENT_CHANNEL_REPLY_READABLE)
        return;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_drain_reply (spot_));
}

bool wait_for_spot_request (spot_request_probe_t *probe_, void *progress_node_ = NULL)
{
    const auto deadline = std::chrono::steady_clock::now () + std::chrono::milliseconds (2000);
    while (std::chrono::steady_clock::now () < deadline) {
        {
            std::unique_lock<std::mutex> lock (probe_->mutex);
            if (probe_->cv.wait_for (lock, std::chrono::milliseconds (10),
                                     [probe_] () { return probe_->invoked; }))
                return true;
        }
        if (progress_node_)
            (void) zlink_spot_drain_external_router_ingress (progress_node_);
    }
    return false;
}

bool wait_for_spot_request_count (spot_request_list_probe_t *probe_,
                                  size_t count_,
                                  void *progress_node_ = NULL)
{
    const auto deadline = std::chrono::steady_clock::now () + std::chrono::milliseconds (3000);
    while (std::chrono::steady_clock::now () < deadline) {
        {
            std::unique_lock<std::mutex> lock (probe_->mutex);
            if (probe_->cv.wait_for (lock, std::chrono::milliseconds (10),
                                     [probe_, count_] () {
                                         return probe_->records.size () >= count_;
                                     }))
                return true;
        }
        if (progress_node_)
            (void) zlink_spot_drain_external_router_ingress (progress_node_);
    }
    return false;
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

bool bind_spot_router_endpoint (void *node_, char *endpoint_, size_t endpoint_size_)
{
    for (int attempt = 0; attempt < 32; ++attempt) {
        TEST_ASSERT_TRUE (allocate_loopback_tcp_endpoint (endpoint_, endpoint_size_));
        if (zlink_spot_node_set_router_bind (node_, endpoint_) == ZLINK_CONFIG_OK)
            return true;
        TEST_ASSERT_EQUAL_INT (EADDRINUSE, zlink_errno ());
    }
    return false;
}

bool bind_registry_test_endpoints (
  void *registry_, char *pub_out_, size_t pub_size_, char *router_out_, size_t router_size_)
{
    if (!registry_ || !pub_out_ || !router_out_ || pub_size_ == 0 || router_size_ == 0) {
        errno = EINVAL;
        return false;
    }

    for (int i = 0; i < 256; ++i) {
        if (!allocate_loopback_tcp_endpoint (pub_out_, pub_size_)
            || !allocate_loopback_tcp_endpoint (router_out_, router_size_)
            || strcmp (pub_out_, router_out_) == 0)
            continue;
        if (zlink_registry_bind (registry_, pub_out_, router_out_) == ZLINK_BIND_OK)
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
        return zlink_discovery_connect_registry (discovery_, endpoint_) == ZLINK_CONNECT_OK;
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
        if (zlink_discovery_member_peers (discovery_, entries, &count) != ZLINK_CONFIG_OK)
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
        if (zlink_spot_node_peers (node_, NULL, entries, &count) != ZLINK_CONFIG_OK)
            return false;
        for (size_t i = 0; i < count; ++i) {
            if (strcmp (entries[i].channel_name, channel_name_) == 0
                && strcmp (entries[i].peer_endpoint, endpoint_) == 0 && entries[i].source == source_
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
        if (zlink_spot_node_peers (node_, NULL, entries, &count) != ZLINK_CONFIG_OK)
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

    TEST_ASSERT_EQUAL (ZLINK_CONNECT_OK,
                       zlink_spot_node_connect_router_channel_peer (node, "api", endpoint));
    msleep (SETTLE_TIME);

    const zlink_routing_id_t node_rid = get_routing_id_value (node);
    const zlink_routing_id_t spot_rid = get_routing_id_value (spot);
    zlink_msg_t part;
    init_string_part (&part, "router-channel-payload");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_router_send_spot (router, &node_rid, &spot_rid, &part, 1, 0));

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

    TEST_ASSERT_EQUAL (ZLINK_CONNECT_OK,
                       zlink_spot_node_connect_router_channel_peer (node, "api", endpoint));
    msleep (SETTLE_TIME);

    const zlink_routing_id_t node_rid = get_routing_id_value (node);
    const zlink_routing_id_t spot_rid = get_routing_id_value (spot);
    zlink_msg_t part;
    init_string_part (&part, "router-channel-part-payload");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK,
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

    TEST_ASSERT_EQUAL (ZLINK_CONNECT_OK,
                       zlink_spot_node_connect_router_channel_peer (node, "api", endpoint));
    TEST_ASSERT_EQUAL (ZLINK_CONNECT_OK,
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

    TEST_ASSERT_EQUAL (ZLINK_CONNECT_OK,
                       zlink_spot_node_connect_router_channel_peer (node, "api", endpoint));
    msleep (SETTLE_TIME);

    spot_request_probe_t request_probe;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_dispatch_event_handler (spot, &capture_spot_request_dispatch, &request_probe));

    const zlink_routing_id_t node_rid = get_routing_id_value (node);
    const zlink_routing_id_t spot_rid = get_routing_id_value (spot);
    zlink_msg_t request_part;
    init_string_part (&request_part, "router-channel-request");
    reply_probe_t reply_probe;
    reply_probe.progress_handle = router;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_router_request_spot (
      router, &node_rid, &spot_rid, &request_part, 1, &capture_reply, &reply_probe, 0, 3000));

    TEST_ASSERT_TRUE (wait_for_spot_request (&request_probe));
    TEST_ASSERT_EQUAL_STRING ("router-channel-request", request_probe.payload.c_str ());

    zlink_msg_t reply_part;
    init_string_part (&reply_part, "router-channel-reply");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_reply_router (spot, &request_probe.source_rid,
                                                        request_probe.request_seq, &reply_part, 1));

    TEST_ASSERT_TRUE (wait_for_reply (&reply_probe));
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK, reply_probe.result);
    TEST_ASSERT_EQUAL_STRING ("router-channel-reply", reply_probe.payload.c_str ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (router));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_spot_node_router_channel_peer_spot_request_reply ()
{
    void *ctx = zlink_ctx_new ();
    zlink_spot_node_options_t options;
    options.mode = ZLINK_SPOT_NODE_MODE_ROUTED;
    void *source_node = zlink_spot_node_new (ctx, &options);
    void *target_node = zlink_spot_node_new (ctx, &options);
    TEST_ASSERT_NOT_NULL (ctx);
    TEST_ASSERT_NOT_NULL (source_node);
    TEST_ASSERT_NOT_NULL (target_node);

    set_routing_id_text (source_node, "spot-route-source-node");
    set_routing_id_text (target_node, "spot-route-target-node");

    void *source_entry = NULL;
    void *target_entry = NULL;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_spot_node_entry_spot (source_node, &source_entry));
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_spot_node_entry_spot (target_node, &target_entry));
    TEST_ASSERT_NOT_NULL (source_entry);
    TEST_ASSERT_NOT_NULL (target_entry);
    set_routing_id_text (source_entry, "spot-route-source-node");
    set_routing_id_text (target_entry, "spot-route-target-node");

    char source_endpoint[MAX_SOCKET_STRING];
    char target_endpoint[MAX_SOCKET_STRING];
    TEST_ASSERT_TRUE (
      bind_spot_router_endpoint (source_node, source_endpoint, sizeof (source_endpoint)));
    TEST_ASSERT_TRUE (
      bind_spot_router_endpoint (target_node, target_endpoint, sizeof (target_endpoint)));

    const zlink_routing_id_t source_node_rid = get_routing_id_value (source_node);
    const zlink_routing_id_t target_node_rid = get_routing_id_value (target_node);
    const zlink_routing_id_t target_spot_rid = get_routing_id_value (target_entry);

    TEST_ASSERT_EQUAL (ZLINK_CONNECT_OK,
                       zlink_spot_node_connect_router_channel_peer_rid (
                         source_node, "room.route", &target_node_rid, target_endpoint));
    TEST_ASSERT_EQUAL (ZLINK_CONNECT_OK,
                       zlink_spot_node_connect_router_channel_peer_rid (
                         target_node, "room.route", &source_node_rid, source_endpoint));
    TEST_ASSERT_TRUE (wait_for_router_channel_peer_snapshot (
      source_node, "room.route", target_endpoint, ZLINK_SPOT_PEER_SOURCE_MANUAL, 2000));
    TEST_ASSERT_TRUE (wait_for_router_channel_peer_snapshot (
      target_node, "room.route", source_endpoint, ZLINK_SPOT_PEER_SOURCE_MANUAL, 2000));
    msleep (SETTLE_TIME * 4);

    spot_request_probe_t request_probe;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_dispatch_event_handler (
      target_entry, &capture_spot_request_dispatch, &request_probe));

    zlink_msg_t request_part;
    init_string_part (&request_part, "peer-spot-request");
    reply_probe_t reply_probe;
    reply_probe.progress_handle = source_entry;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_request_spot_part (
      source_entry, &target_node_rid, &target_spot_rid, &request_part, &capture_reply,
      &reply_probe, ZLINK_DONTWAIT, ZLINK_PART_FINAL, 3000));

    TEST_ASSERT_TRUE (wait_for_spot_request (&request_probe, target_node));
    TEST_ASSERT_EQUAL_STRING ("peer-spot-request", request_probe.payload.c_str ());

    zlink_msg_t reply_part;
    init_string_part (&reply_part, "peer-spot-reply");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_reply_spot_part (
      target_entry, &request_probe.source_rid, &request_probe.source_spot_rid,
      request_probe.request_seq, &reply_part, ZLINK_PART_FINAL));

    TEST_ASSERT_TRUE (zlink_test_wait_until (2000, [&] {
        (void) zlink_spot_drain_external_router_ingress (source_node);
        return wait_for_reply (&reply_probe);
    }));
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK, reply_probe.result);
    TEST_ASSERT_EQUAL_STRING ("peer-spot-reply", reply_probe.payload.c_str ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&target_entry));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&source_entry));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&target_node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&source_node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_spot_node_router_channel_peer_spot_request_reply_with_source_dispatch ()
{
    void *ctx = zlink_ctx_new ();
    zlink_spot_node_options_t options;
    options.mode = ZLINK_SPOT_NODE_MODE_ROUTED;
    void *source_node = zlink_spot_node_new (ctx, &options);
    void *target_node = zlink_spot_node_new (ctx, &options);
    TEST_ASSERT_NOT_NULL (ctx);
    TEST_ASSERT_NOT_NULL (source_node);
    TEST_ASSERT_NOT_NULL (target_node);

    set_routing_id_text (source_node, "spot-route-source-dispatch-node");
    set_routing_id_text (target_node, "spot-route-target-dispatch-node");

    void *source_entry = NULL;
    void *target_entry = NULL;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_spot_node_entry_spot (source_node, &source_entry));
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_spot_node_entry_spot (target_node, &target_entry));
    TEST_ASSERT_NOT_NULL (source_entry);
    TEST_ASSERT_NOT_NULL (target_entry);
    set_routing_id_text (source_entry, "spot-route-source-dispatch-node");
    set_routing_id_text (target_entry, "spot-route-target-dispatch-node");

    char source_endpoint[MAX_SOCKET_STRING];
    char target_endpoint[MAX_SOCKET_STRING];
    TEST_ASSERT_TRUE (
      bind_spot_router_endpoint (source_node, source_endpoint, sizeof (source_endpoint)));
    TEST_ASSERT_TRUE (
      bind_spot_router_endpoint (target_node, target_endpoint, sizeof (target_endpoint)));

    const zlink_routing_id_t source_node_rid = get_routing_id_value (source_node);
    const zlink_routing_id_t target_node_rid = get_routing_id_value (target_node);
    const zlink_routing_id_t target_spot_rid = get_routing_id_value (target_entry);

    TEST_ASSERT_EQUAL (ZLINK_CONNECT_OK,
                       zlink_spot_node_connect_router_channel_peer_rid (
                         source_node, "room.route", &target_node_rid, target_endpoint));
    TEST_ASSERT_EQUAL (ZLINK_CONNECT_OK,
                       zlink_spot_node_connect_router_channel_peer_rid (
                         target_node, "room.route", &source_node_rid, source_endpoint));
    TEST_ASSERT_TRUE (wait_for_router_channel_peer_snapshot (
      source_node, "room.route", target_endpoint, ZLINK_SPOT_PEER_SOURCE_MANUAL, 2000));
    TEST_ASSERT_TRUE (wait_for_router_channel_peer_snapshot (
      target_node, "room.route", source_endpoint, ZLINK_SPOT_PEER_SOURCE_MANUAL, 2000));
    msleep (SETTLE_TIME * 4);

    spot_request_probe_t request_probe;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_dispatch_event_handler (source_entry, &drain_source_reply_dispatch, NULL));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_dispatch_event_handler (
      target_entry, &capture_spot_request_dispatch, &request_probe));

    zlink_msg_t request_part;
    init_string_part (&request_part, "peer-spot-request-source-dispatch");
    reply_probe_t reply_probe;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_request_spot_part (
      source_entry, &target_node_rid, &target_spot_rid, &request_part, &capture_reply,
      &reply_probe, ZLINK_DONTWAIT, ZLINK_PART_FINAL, 3000));

    TEST_ASSERT_TRUE (wait_for_spot_request (&request_probe, target_node));
    TEST_ASSERT_EQUAL_STRING ("peer-spot-request-source-dispatch", request_probe.payload.c_str ());

    zlink_msg_t reply_part;
    init_string_part (&reply_part, "peer-spot-reply-source-dispatch");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_reply_spot_part (
      target_entry, &request_probe.source_rid, &request_probe.source_spot_rid,
      request_probe.request_seq, &reply_part, ZLINK_PART_FINAL));

    TEST_ASSERT_TRUE (zlink_test_wait_until (2000, [&] {
        (void) zlink_spot_drain_external_router_ingress (source_node);
        return wait_for_reply (&reply_probe);
    }));
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK, reply_probe.result);
    TEST_ASSERT_EQUAL_STRING ("peer-spot-reply-source-dispatch", reply_probe.payload.c_str ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&target_entry));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&source_entry));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&target_node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&source_node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_spot_node_router_channel_peer_spot_two_inflight_requests_reply ()
{
    void *ctx = zlink_ctx_new ();
    zlink_spot_node_options_t options;
    options.mode = ZLINK_SPOT_NODE_MODE_ROUTED;
    void *source_node = zlink_spot_node_new (ctx, &options);
    void *target_node = zlink_spot_node_new (ctx, &options);
    TEST_ASSERT_NOT_NULL (ctx);
    TEST_ASSERT_NOT_NULL (source_node);
    TEST_ASSERT_NOT_NULL (target_node);

    set_routing_id_text (source_node, "spot-route-source-two-node");
    set_routing_id_text (target_node, "spot-route-target-two-node");

    void *source_entry = NULL;
    void *target_entry = NULL;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_spot_node_entry_spot (source_node, &source_entry));
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_spot_node_entry_spot (target_node, &target_entry));
    TEST_ASSERT_NOT_NULL (source_entry);
    TEST_ASSERT_NOT_NULL (target_entry);
    set_routing_id_text (source_entry, "spot-route-source-two-node");
    set_routing_id_text (target_entry, "spot-route-target-two-node");

    char source_endpoint[MAX_SOCKET_STRING];
    char target_endpoint[MAX_SOCKET_STRING];
    TEST_ASSERT_TRUE (
      bind_spot_router_endpoint (source_node, source_endpoint, sizeof (source_endpoint)));
    TEST_ASSERT_TRUE (
      bind_spot_router_endpoint (target_node, target_endpoint, sizeof (target_endpoint)));

    const zlink_routing_id_t source_node_rid = get_routing_id_value (source_node);
    const zlink_routing_id_t target_node_rid = get_routing_id_value (target_node);
    const zlink_routing_id_t target_spot_rid = get_routing_id_value (target_entry);

    TEST_ASSERT_EQUAL (ZLINK_CONNECT_OK,
                       zlink_spot_node_connect_router_channel_peer_rid (
                         source_node, "room.route", &target_node_rid, target_endpoint));
    TEST_ASSERT_EQUAL (ZLINK_CONNECT_OK,
                       zlink_spot_node_connect_router_channel_peer_rid (
                         target_node, "room.route", &source_node_rid, source_endpoint));
    TEST_ASSERT_TRUE (wait_for_router_channel_peer_snapshot (
      source_node, "room.route", target_endpoint, ZLINK_SPOT_PEER_SOURCE_MANUAL, 2000));
    TEST_ASSERT_TRUE (wait_for_router_channel_peer_snapshot (
      target_node, "room.route", source_endpoint, ZLINK_SPOT_PEER_SOURCE_MANUAL, 2000));
    msleep (SETTLE_TIME * 4);

    spot_request_list_probe_t request_probe;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_dispatch_event_handler (source_entry, &drain_source_reply_dispatch, NULL));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_dispatch_event_handler (
      target_entry, &capture_spot_request_list_dispatch, &request_probe));

    zlink_msg_t request_a;
    zlink_msg_t request_b;
    init_string_part (&request_a, "peer-spot-request-a");
    init_string_part (&request_b, "peer-spot-request-b");
    reply_probe_t reply_a;
    reply_probe_t reply_b;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_request_spot_part (
      source_entry, &target_node_rid, &target_spot_rid, &request_a, &capture_reply,
      &reply_a, ZLINK_DONTWAIT, ZLINK_PART_FINAL, 3000));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_request_spot_part (
      source_entry, &target_node_rid, &target_spot_rid, &request_b, &capture_reply,
      &reply_b, ZLINK_DONTWAIT, ZLINK_PART_FINAL, 3000));

    TEST_ASSERT_TRUE (wait_for_spot_request_count (&request_probe, 2, target_node));

    std::vector<spot_request_record_t> records;
    {
        std::lock_guard<std::mutex> lock (request_probe.mutex);
        records = request_probe.records;
    }
    TEST_ASSERT_EQUAL (2, records.size ());
    TEST_ASSERT_EQUAL_STRING ("peer-spot-request-a", records[0].payload.c_str ());
    TEST_ASSERT_EQUAL_STRING ("peer-spot-request-b", records[1].payload.c_str ());

    zlink_msg_t reply_part_a;
    zlink_msg_t reply_part_b;
    init_string_part (&reply_part_a, "peer-spot-reply-a");
    init_string_part (&reply_part_b, "peer-spot-reply-b");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_reply_spot_part (
      target_entry, &records[0].source_rid, &records[0].source_spot_rid,
      records[0].request_seq, &reply_part_a, ZLINK_PART_FINAL));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_reply_spot_part (
      target_entry, &records[1].source_rid, &records[1].source_spot_rid,
      records[1].request_seq, &reply_part_b, ZLINK_PART_FINAL));

    TEST_ASSERT_TRUE (zlink_test_wait_until (3000, [&] {
        (void) zlink_spot_drain_external_router_ingress (source_node);
        return wait_for_reply (&reply_a) && wait_for_reply (&reply_b);
    }));
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK, reply_a.result);
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK, reply_b.result);
    TEST_ASSERT_EQUAL_STRING ("peer-spot-reply-a", reply_a.payload.c_str ());
    TEST_ASSERT_EQUAL_STRING ("peer-spot-reply-b", reply_b.payload.c_str ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&target_entry));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&source_entry));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&target_node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&source_node));
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

    TEST_ASSERT_EQUAL (ZLINK_CONNECT_OK,
                       zlink_spot_node_connect_router_channel_peer (node, "api", endpoint));
    msleep (SETTLE_TIME);
    TEST_ASSERT_TRUE (wait_for_router_channel_peer_snapshot (node, "api", endpoint,
                                                             ZLINK_SPOT_PEER_SOURCE_MANUAL, 2000));

    const zlink_routing_id_t node_rid = get_routing_id_value (node);
    const zlink_routing_id_t spot_rid = get_routing_id_value (spot);
    zlink_msg_t first_part;
    init_string_part (&first_part, "before-disconnect");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK,
                       zlink_router_send_spot (router, &node_rid, &spot_rid, &first_part, 1, 0));

    std::string payload;
    TEST_ASSERT_TRUE (recv_spot_payload (spot, &payload));
    TEST_ASSERT_EQUAL_STRING ("before-disconnect", payload.c_str ());

    TEST_ASSERT_EQUAL (ZLINK_CONNECT_OK,
                       zlink_spot_node_disconnect_router_channel_peer (node, "api", endpoint));
    TEST_ASSERT_TRUE (wait_for_router_channel_peer_absent (node, "api", endpoint, 2000));

    TEST_ASSERT_TRUE (zlink_test_wait_until (2000, [=] {
        zlink_msg_t blocked_part;
        init_string_part (&blocked_part, "after-disconnect");
        const zlink_submit_result_t rc =
          zlink_router_send_spot (router, &node_rid, &spot_rid, &blocked_part, 1, ZLINK_DONTWAIT);
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
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_set_broadcast_interval (registry, 50));

    char registry_pub[MAX_SOCKET_STRING];
    char registry_router[MAX_SOCKET_STRING];
    TEST_ASSERT_TRUE (bind_registry_test_endpoints (registry, registry_pub, sizeof (registry_pub),
                                                    registry_router, sizeof (registry_router)));

    void *router_discovery = zlink_discovery_new (ctx, ZLINK_AUTO_CONNECT_CLIENT_SERVER, "api");
    void *node_discovery = zlink_discovery_new (ctx, ZLINK_AUTO_CONNECT_CLIENT_SERVER, "api");
    TEST_ASSERT_NOT_NULL (router_discovery);
    TEST_ASSERT_NOT_NULL (node_discovery);
    TEST_ASSERT_TRUE (
      connect_discovery_registry_with_retry (router_discovery, registry_router, 3000));
    TEST_ASSERT_TRUE (
      connect_discovery_registry_with_retry (node_discovery, registry_router, 3000));

    set_routing_id_text (node, "spot-route-auto-node");
    set_routing_id_text (spot, "spot-route-auto-target");
    set_routing_id_text (router, "spot-route-auto-router");

    TEST_ASSERT_SUCCESS_ERRNO (zlink_socket_attach_discovery (router, router_discovery));
    char endpoint[MAX_SOCKET_STRING];
    bind_router (router, endpoint, sizeof (endpoint));
    const zlink_routing_id_t router_rid = get_routing_id_value (router);

    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_spot_node_attach_router_channel_discovery (
                                          node, "api", node_discovery));
    TEST_ASSERT_TRUE (
      wait_for_discovery_member_role_count (node_discovery, ZLINK_SERVICE_ROLE_ROUTER, 1, 10000));
    TEST_ASSERT_TRUE (wait_for_router_channel_peer_snapshot (
      node, "api", endpoint, ZLINK_SPOT_PEER_SOURCE_DISCOVERY, 10000));

    const zlink_routing_id_t node_rid = get_routing_id_value (node);
    const zlink_routing_id_t spot_rid = get_routing_id_value (spot);
    zlink_msg_t part;
    init_string_part (&part, "discovery-router-channel");
    TEST_ASSERT_EQUAL (ZLINK_SUBMIT_OK,
                       zlink_router_send_spot (router, &node_rid, &spot_rid, &part, 1, 0));

    std::string payload;
    TEST_ASSERT_TRUE (recv_spot_payload (spot, &payload));
    TEST_ASSERT_EQUAL_STRING ("discovery-router-channel", payload.c_str ());

    TEST_ASSERT_EQUAL (ZLINK_CONNECT_OK, zlink_spot_node_disconnect_router_channel_peer_rid (
                                           node, "api", &router_rid));
    TEST_ASSERT_TRUE (wait_for_router_channel_peer_absent (node, "api", endpoint, 2000));
    TEST_ASSERT_TRUE (zlink_test_wait_until (2000, [=] {
        zlink_msg_t blocked_part;
        init_string_part (&blocked_part, "after-discovery-rid-disconnect");
        const zlink_submit_result_t rc =
          zlink_router_send_spot (router, &node_rid, &spot_rid, &blocked_part, 1, ZLINK_DONTWAIT);
        return rc != ZLINK_SUBMIT_OK;
    }));

    TEST_ASSERT_TRUE (destroy_discovery_with_retry (&node_discovery, 3000));
    TEST_ASSERT_TRUE (destroy_discovery_with_retry (&router_discovery, 3000));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_spot_node_router_channel_discovery_peer_spot_request_reply ()
{
    if (!zlink_has ("tcp")) {
        TEST_IGNORE_MESSAGE ("TCP not available");
        return;
    }

    void *ctx = zlink_ctx_new ();
    void *registry = zlink_registry_new (ctx);
    zlink_spot_node_options_t options;
    options.mode = ZLINK_SPOT_NODE_MODE_ROUTED;
    void *source_node = zlink_spot_node_new (ctx, &options);
    void *target_node = zlink_spot_node_new (ctx, &options);
    TEST_ASSERT_NOT_NULL (ctx);
    TEST_ASSERT_NOT_NULL (registry);
    TEST_ASSERT_NOT_NULL (source_node);
    TEST_ASSERT_NOT_NULL (target_node);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_set_broadcast_interval (registry, 50));

    char registry_pub[MAX_SOCKET_STRING];
    char registry_router[MAX_SOCKET_STRING];
    TEST_ASSERT_TRUE (bind_registry_test_endpoints (registry, registry_pub, sizeof (registry_pub),
                                                    registry_router, sizeof (registry_router)));

    set_routing_id_text (source_node, "spot-route-discovery-source-node");
    set_routing_id_text (target_node, "spot-route-discovery-target-node");

    void *source_entry = NULL;
    void *target_entry = NULL;
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_spot_node_entry_spot (source_node, &source_entry));
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK, zlink_spot_node_entry_spot (target_node, &target_entry));
    TEST_ASSERT_NOT_NULL (source_entry);
    TEST_ASSERT_NOT_NULL (target_entry);
    set_routing_id_text (source_entry, "spot-route-discovery-source-node");
    set_routing_id_text (target_entry, "spot-route-discovery-target-node");

    char source_endpoint[MAX_SOCKET_STRING];
    char target_endpoint[MAX_SOCKET_STRING];
    TEST_ASSERT_TRUE (
      bind_spot_router_endpoint (source_node, source_endpoint, sizeof (source_endpoint)));
    TEST_ASSERT_TRUE (
      bind_spot_router_endpoint (target_node, target_endpoint, sizeof (target_endpoint)));

    void *source_discovery =
      zlink_discovery_new (ctx, ZLINK_AUTO_CONNECT_ROUTE_MESH, "room.route.discovery");
    void *target_discovery =
      zlink_discovery_new (ctx, ZLINK_AUTO_CONNECT_ROUTE_MESH, "room.route.discovery");
    TEST_ASSERT_NOT_NULL (source_discovery);
    TEST_ASSERT_NOT_NULL (target_discovery);
    TEST_ASSERT_TRUE (
      connect_discovery_registry_with_retry (source_discovery, registry_router, 3000));
    TEST_ASSERT_TRUE (
      connect_discovery_registry_with_retry (target_discovery, registry_router, 3000));

    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_spot_node_attach_router_channel_discovery (
                         source_node, "room.route.discovery", source_discovery));
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_OK,
                       zlink_spot_node_attach_router_channel_discovery (
                         target_node, "room.route.discovery", target_discovery));

    TEST_ASSERT_TRUE (wait_for_discovery_member_role_count (
      source_discovery, ZLINK_SERVICE_ROLE_ROUTER, 1, 10000));
    TEST_ASSERT_TRUE (wait_for_discovery_member_role_count (
      target_discovery, ZLINK_SERVICE_ROLE_ROUTER, 1, 10000));
    TEST_ASSERT_TRUE (wait_for_router_channel_peer_snapshot (
      source_node, "room.route.discovery", target_endpoint, ZLINK_SPOT_PEER_SOURCE_DISCOVERY,
      10000));
    TEST_ASSERT_TRUE (wait_for_router_channel_peer_snapshot (
      target_node, "room.route.discovery", source_endpoint, ZLINK_SPOT_PEER_SOURCE_DISCOVERY,
      10000));

    const zlink_routing_id_t target_node_rid = get_routing_id_value (target_node);
    const zlink_routing_id_t target_spot_rid = get_routing_id_value (target_entry);

    spot_request_probe_t request_probe;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_dispatch_event_handler (source_entry, &drain_source_reply_dispatch, NULL));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_dispatch_event_handler (
      target_entry, &capture_spot_request_dispatch, &request_probe));

    zlink_msg_t request_part;
    init_string_part (&request_part, "discovery-peer-spot-request");
    reply_probe_t reply_probe;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_request_spot_part (
      source_entry, &target_node_rid, &target_spot_rid, &request_part, &capture_reply,
      &reply_probe, ZLINK_DONTWAIT, ZLINK_PART_FINAL, 3000));

    TEST_ASSERT_TRUE (wait_for_spot_request (&request_probe, target_node));
    TEST_ASSERT_EQUAL_STRING ("discovery-peer-spot-request", request_probe.payload.c_str ());

    zlink_msg_t reply_part;
    init_string_part (&reply_part, "discovery-peer-spot-reply");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_reply_spot_part (
      target_entry, &request_probe.source_rid, &request_probe.source_spot_rid,
      request_probe.request_seq, &reply_part, ZLINK_PART_FINAL));

    TEST_ASSERT_TRUE (zlink_test_wait_until (3000, [&] {
        (void) zlink_spot_drain_external_router_ingress (source_node);
        return wait_for_reply (&reply_probe);
    }));
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK, reply_probe.result);
    TEST_ASSERT_EQUAL_STRING ("discovery-peer-spot-reply", reply_probe.payload.c_str ());

    TEST_ASSERT_TRUE (destroy_discovery_with_retry (&target_discovery, 3000));
    TEST_ASSERT_TRUE (destroy_discovery_with_retry (&source_discovery, 3000));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&target_entry));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&source_entry));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&target_node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&source_node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_spot_node_router_channel_manual_and_discovery_conflict ()
{
    void *ctx = zlink_ctx_new ();
    void *node = zlink_spot_node_new (ctx, NULL);
    void *router = zlink_socket (ctx, ZLINK_SOCKET_ROUTER);
    void *discovery = zlink_discovery_new (ctx, ZLINK_AUTO_CONNECT_CLIENT_SERVER, "api");
    TEST_ASSERT_NOT_NULL (ctx);
    TEST_ASSERT_NOT_NULL (node);
    TEST_ASSERT_NOT_NULL (router);
    TEST_ASSERT_NOT_NULL (discovery);

    set_routing_id_text (node, "spot-route-conflict-node");
    set_routing_id_text (router, "spot-route-conflict-router");

    char endpoint[MAX_SOCKET_STRING];
    bind_router (router, endpoint, sizeof (endpoint));

    TEST_ASSERT_EQUAL (ZLINK_CONNECT_OK,
                       zlink_spot_node_connect_router_channel_peer (node, "api", endpoint));
    TEST_ASSERT_EQUAL (ZLINK_CONFIG_INVALID_STATE,
                       zlink_spot_node_attach_router_channel_discovery (node, "api", discovery));
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

    TEST_ASSERT_EQUAL (ZLINK_CONNECT_INVALID_ARGUMENT,
                       zlink_spot_node_connect_router_channel_peer (node, "", "tcp://127.0.0.1:1"));
    TEST_ASSERT_EQUAL (EINVAL, zlink_errno ());
    TEST_ASSERT_EQUAL (ZLINK_CONNECT_INVALID_ARGUMENT, zlink_spot_node_connect_router_channel_peer (
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
    RUN_TEST (test_spot_node_router_channel_peer_spot_request_reply);
    RUN_TEST (test_spot_node_router_channel_peer_spot_request_reply_with_source_dispatch);
    RUN_TEST (test_spot_node_router_channel_peer_spot_two_inflight_requests_reply);
    RUN_TEST (test_spot_node_router_channel_disconnect_blocks_delivery);
    RUN_TEST (test_spot_node_router_channel_discovery_connects_new_peer);
    RUN_TEST (test_spot_node_router_channel_discovery_peer_spot_request_reply);
    RUN_TEST (test_spot_node_router_channel_manual_and_discovery_conflict);
    RUN_TEST (test_spot_node_router_channel_rejects_invalid_channel_name);
    return UNITY_END ();
}
