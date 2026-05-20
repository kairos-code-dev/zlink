/* SPDX-License-Identifier: MPL-2.0 */

#include "testutil.hpp"
#include "testutil_unity.hpp"

#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

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

bool wait_for_spot_request (spot_request_probe_t *probe_)
{
    std::unique_lock<std::mutex> lock (probe_->mutex);
    return probe_->cv.wait_for (
      lock, std::chrono::milliseconds (2000),
      [probe_]() { return probe_->invoked; });
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
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_handler (spot, &capture_spot_request, &request_probe));

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
    RUN_TEST (test_spot_node_router_channel_duplicate_manual_connect_is_idempotent);
    RUN_TEST (test_spot_node_router_channel_manual_request_spot);
    RUN_TEST (test_spot_node_router_channel_rejects_invalid_channel_name);
    return UNITY_END ();
}
