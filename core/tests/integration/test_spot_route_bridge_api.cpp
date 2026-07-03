/* SPDX-License-Identifier: MPL-2.0 */

#include "../testutil_unity.hpp"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <string.h>
#include <thread>
#include <unity.h>

void setUp ()
{
}

void tearDown ()
{
}

namespace
{
struct reply_probe_t
{
    std::mutex mutex;
    std::condition_variable cv;
    bool done;
    zlink_request_result_t result;
    std::string payload;
    size_t part_count;

    reply_probe_t () : done (false), result (ZLINK_REQUEST_PROTOCOL_ERROR), part_count (0) {}
};

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
        probe->part_count = part_count_;
        if (part_count_ > 0) {
            probe->payload.assign (static_cast<const char *> (zlink_msg_data (&parts_[0])),
                                   zlink_msg_size (&parts_[0]));
        }
    }
    zlink_multipart_close (parts_, part_count_);
    probe->cv.notify_all ();
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

bool wait_for_reply (reply_probe_t *probe_, void *progress_handle_)
{
    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now () + std::chrono::seconds (3);
    while (std::chrono::steady_clock::now () < deadline) {
        {
            std::unique_lock<std::mutex> lock (probe_->mutex);
            if (probe_->cv.wait_for (lock, std::chrono::milliseconds (10),
                                     [probe_] () { return probe_->done; }))
                return true;
        }
        (void) drain_completion_via_poller (progress_handle_);
    }
    return false;
}

bool wait_for_reply_with_poller (reply_probe_t *probe_, void *poller_)
{
    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now () + std::chrono::seconds (3);
    while (std::chrono::steady_clock::now () < deadline) {
        {
            std::unique_lock<std::mutex> lock (probe_->mutex);
            if (probe_->cv.wait_for (lock, std::chrono::milliseconds (10),
                                     [probe_] () { return probe_->done; }))
                return true;
        }
        zlink_poller_event_t event;
        (void) zlink_poller_wait (poller_, &event, 1, 10, NULL);
    }
    return false;
}

bool wait_for_reply_with_bridge_drain (reply_probe_t *probe_, void *bridge_)
{
    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now () + std::chrono::seconds (3);
    while (std::chrono::steady_clock::now () < deadline) {
        {
            std::unique_lock<std::mutex> lock (probe_->mutex);
            if (probe_->cv.wait_for (lock, std::chrono::milliseconds (10),
                                     [probe_] () { return probe_->done; }))
                return true;
        }
        (void) zlink_spot_route_bridge_drain (bridge_);
    }
    return false;
}

void drain_router_progress (void *router_)
{
    const zlink_routing_id_t *source_node_rid = NULL;
    const zlink_routing_id_t *source_spot_rid = NULL;
    uint64_t request_seq = 0;
    zlink_msg_t part;
    zlink_msg_init (&part);
    zlink_part_flag_t has_more = ZLINK_PART_FINAL;
    const zlink_recv_result_t rc =
      zlink_router_recv_part (router_, &source_node_rid, &source_spot_rid, &request_seq, &part,
                              &has_more, ZLINK_RECV_FLAGS_DONTWAIT);
    if (rc == ZLINK_RECV_OK)
        zlink_msg_close (&part);
    else
        zlink_msg_close (&part);
}

void init_text_part (zlink_msg_t *part_, const char *text_)
{
    const size_t size = strlen (text_);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (part_, size));
    memcpy (zlink_msg_data (part_), text_, size);
}

void init_buffer_part (zlink_msg_t *part_, const void *data_, size_t size_)
{
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (part_, size_));
    if (size_ > 0)
        memcpy (zlink_msg_data (part_), data_, size_);
}

void assert_recv_text_part (void *socket_, const char *expected_, zlink_part_flag_t expected_more_)
{
    const zlink_routing_id_t *source_node_rid = NULL;
    const zlink_routing_id_t *source_spot_rid = NULL;
    uint64_t request_seq = 0;
    zlink_msg_t part;
    zlink_part_flag_t has_more = ZLINK_PART_FINAL;
    zlink_recv_result_t rc = ZLINK_RECV_NO_DATA;
    for (int attempt = 0; attempt < 100; ++attempt) {
        zlink_msg_init (&part);
        rc = zlink_router_recv_part (socket_, &source_node_rid, &source_spot_rid, &request_seq,
                                     &part, &has_more, ZLINK_RECV_FLAGS_DONTWAIT);
        if (rc == ZLINK_RECV_OK)
            break;
        zlink_msg_close (&part);
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }
    TEST_ASSERT_EQUAL_INT (ZLINK_RECV_OK, rc);
    TEST_ASSERT_EQUAL_INT (strlen (expected_), zlink_msg_size (&part));
    TEST_ASSERT_EQUAL_MEMORY (expected_, zlink_msg_data (&part), strlen (expected_));
    TEST_ASSERT_EQUAL_INT (expected_more_, has_more);
    zlink_msg_close (&part);
}

void assert_recv_buffer_part (void *socket_,
                              const void *expected_,
                              size_t expected_size_,
                              zlink_part_flag_t expected_more_)
{
    const zlink_routing_id_t *source_node_rid = NULL;
    const zlink_routing_id_t *source_spot_rid = NULL;
    uint64_t request_seq = 0;
    zlink_msg_t part;
    zlink_part_flag_t has_more = ZLINK_PART_FINAL;
    zlink_recv_result_t rc = ZLINK_RECV_NO_DATA;
    for (int attempt = 0; attempt < 100; ++attempt) {
        zlink_msg_init (&part);
        rc = zlink_router_recv_part (socket_, &source_node_rid, &source_spot_rid, &request_seq,
                                     &part, &has_more, ZLINK_RECV_FLAGS_DONTWAIT);
        if (rc == ZLINK_RECV_OK)
            break;
        zlink_msg_close (&part);
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }
    TEST_ASSERT_EQUAL_INT (ZLINK_RECV_OK, rc);
    TEST_ASSERT_EQUAL_INT (expected_size_, zlink_msg_size (&part));
    TEST_ASSERT_EQUAL_MEMORY (expected_, zlink_msg_data (&part), expected_size_);
    TEST_ASSERT_EQUAL_INT (expected_more_, has_more);
    zlink_msg_close (&part);
}

void recv_router_multipart (void *socket_,
                            zlink_routing_id_t *source_node_rid_out_,
                            zlink_msg_t *parts_,
                            size_t part_count_)
{
    TEST_ASSERT_NOT_NULL (source_node_rid_out_);
    TEST_ASSERT_NOT_NULL (parts_);
    memset (source_node_rid_out_, 0, sizeof (*source_node_rid_out_));
    for (size_t index = 0; index < part_count_; ++index) {
        const zlink_routing_id_t *source_node_rid = NULL;
        const zlink_routing_id_t *source_spot_rid = NULL;
        uint64_t request_seq = 0;
        zlink_part_flag_t has_more = ZLINK_PART_FINAL;
        zlink_recv_result_t rc = ZLINK_RECV_NO_DATA;
        for (int attempt = 0; attempt < 100; ++attempt) {
            zlink_msg_init (&parts_[index]);
            rc = zlink_router_recv_part (socket_, &source_node_rid, &source_spot_rid,
                                         &request_seq, &parts_[index], &has_more,
                                         ZLINK_RECV_FLAGS_DONTWAIT);
            if (rc == ZLINK_RECV_OK)
                break;
            zlink_msg_close (&parts_[index]);
            std::this_thread::sleep_for (std::chrono::milliseconds (1));
        }
        TEST_ASSERT_EQUAL_INT (ZLINK_RECV_OK, rc);
        TEST_ASSERT_NOT_NULL (source_node_rid);
        if (index == 0)
            *source_node_rid_out_ = *source_node_rid;
        else
            TEST_ASSERT_EQUAL_MEMORY (source_node_rid_out_->data, source_node_rid->data,
                                      source_node_rid_out_->size);
        TEST_ASSERT_EQUAL_INT (index + 1 < part_count_ ? ZLINK_PART_MORE : ZLINK_PART_FINAL,
                               has_more);
        TEST_ASSERT_EQUAL_UINT64 (0, request_seq);
    }
}

uint64_t recv_router_request_multipart (void *socket_,
                                        zlink_routing_id_t *source_node_rid_out_,
                                        zlink_msg_t *parts_,
                                        size_t part_count_)
{
    TEST_ASSERT_NOT_NULL (source_node_rid_out_);
    TEST_ASSERT_NOT_NULL (parts_);
    memset (source_node_rid_out_, 0, sizeof (*source_node_rid_out_));
    uint64_t request_seq = 0;
    for (size_t index = 0; index < part_count_; ++index) {
        const zlink_routing_id_t *source_node_rid = NULL;
        const zlink_routing_id_t *source_spot_rid = NULL;
        uint64_t current_request_seq = 0;
        zlink_part_flag_t has_more = ZLINK_PART_FINAL;
        zlink_recv_result_t rc = ZLINK_RECV_NO_DATA;
        for (int attempt = 0; attempt < 100; ++attempt) {
            zlink_msg_init (&parts_[index]);
            rc = zlink_router_recv_part (socket_, &source_node_rid, &source_spot_rid,
                                         &current_request_seq, &parts_[index], &has_more,
                                         ZLINK_RECV_FLAGS_DONTWAIT);
            if (rc == ZLINK_RECV_OK)
                break;
            zlink_msg_close (&parts_[index]);
            std::this_thread::sleep_for (std::chrono::milliseconds (1));
        }
        TEST_ASSERT_EQUAL_INT (ZLINK_RECV_OK, rc);
        TEST_ASSERT_NOT_NULL (source_node_rid);
        TEST_ASSERT_NOT_EQUAL (0, current_request_seq);
        if (index == 0) {
            *source_node_rid_out_ = *source_node_rid;
            request_seq = current_request_seq;
        } else {
            TEST_ASSERT_EQUAL_MEMORY (source_node_rid_out_->data, source_node_rid->data,
                                      source_node_rid_out_->size);
            TEST_ASSERT_EQUAL_UINT64 (request_seq, current_request_seq);
        }
        TEST_ASSERT_EQUAL_INT (index + 1 < part_count_ ? ZLINK_PART_MORE : ZLINK_PART_FINAL,
                               has_more);
    }
    return request_seq;
}

void test_spot_node_publisher_publish_uses_local_topic_plane ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_set (ctx, ZLINK_MAX_SOCKETS, 128));

    void *node = zlink_spot_node_new (ctx, NULL);
    TEST_ASSERT_NOT_NULL (node);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_set_pub_bind (node, "inproc://publisher-smoke"));

    void *publisher = zlink_spot_node_publisher_new (node);
    TEST_ASSERT_NOT_NULL (publisher);

    zlink_msg_t part;
    init_text_part (&part, "payload");
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_publisher_publish (publisher, "topic.smoke", &part, 1, ZLINK_DONTWAIT));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_publisher_close (publisher));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_bridge_send_router_uses_target_node_rid ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_set (ctx, ZLINK_MAX_SOCKETS, 128));

    void *node = zlink_spot_node_new (ctx, NULL);
    TEST_ASSERT_NOT_NULL (node);
    void *client_router = zlink_socket (ctx, ZLINK_SOCKET_ROUTER);
    void *server_router = zlink_socket (ctx, ZLINK_SOCKET_ROUTER);
    TEST_ASSERT_NOT_NULL (client_router);
    TEST_ASSERT_NOT_NULL (server_router);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (client_router, "bridge-router-client", 20));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (server_router, "bridge-router-server", 20));

    int mandatory = 1;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_router_option (
      client_router, ZLINK_ROUTER_OPT_MANDATORY, &mandatory, sizeof (mandatory)));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_bind (server_router, "inproc://spot-route-bridge-router"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (client_router, "inproc://spot-route-bridge-router"));
    std::this_thread::sleep_for (std::chrono::milliseconds (50));

    void *bridge = zlink_spot_route_bridge_new (ctx, node, NULL);
    TEST_ASSERT_NOT_NULL (bridge);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_route_bridge_attach_router_channel (bridge, "mesh", client_router, NULL));

    zlink_routing_id_t target_node_rid;
    memset (&target_node_rid, 0, sizeof (target_node_rid));
    target_node_rid.size = 20;
    memcpy (target_node_rid.data, "bridge-router-server", 20);

    zlink_routing_id_t target_spot_rid;
    memset (&target_spot_rid, 0, sizeof (target_spot_rid));
    target_spot_rid.size = 11;
    memcpy (target_spot_rid.data, "target-spot", target_spot_rid.size);

    zlink_msg_t payload;
    init_text_part (&payload, "router-egress");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_route_bridge_send (
      bridge, "mesh", &target_node_rid, &target_spot_rid, &payload, 1, ZLINK_SEND_FLAGS_NONE));

    assert_recv_text_part (server_router, "__zlink.routed_spot.egress.send", ZLINK_PART_MORE);
    assert_recv_buffer_part (server_router, target_spot_rid.data, target_spot_rid.size,
                             ZLINK_PART_MORE);
    assert_recv_text_part (server_router, "router-egress", ZLINK_PART_FINAL);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_route_bridge_close (bridge));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (client_router));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (server_router));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_bridge_send_router_relay_delivers_to_target_bridge_spot ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_set (ctx, ZLINK_MAX_SOCKETS, 128));

    void *source_node = zlink_spot_node_new (ctx, NULL);
    void *target_node = zlink_spot_node_new (ctx, NULL);
    TEST_ASSERT_NOT_NULL (source_node);
    TEST_ASSERT_NOT_NULL (target_node);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_routing_id (source_node, "bridge-source-node", strlen ("bridge-source-node")));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_routing_id (target_node, "bridge-target-node", strlen ("bridge-target-node")));

    void *target_spot = NULL;
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK,
                           zlink_spot_node_entry_spot (target_node, &target_spot));
    TEST_ASSERT_NOT_NULL (target_spot);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_routing_id (target_spot, "bridge-target-node", strlen ("bridge-target-node")));

    void *source_router = zlink_socket (ctx, ZLINK_SOCKET_ROUTER);
    void *target_router = zlink_socket (ctx, ZLINK_SOCKET_ROUTER);
    TEST_ASSERT_NOT_NULL (source_router);
    TEST_ASSERT_NOT_NULL (target_router);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_routing_id (source_router, "bridge-source-node", strlen ("bridge-source-node")));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_routing_id (target_router, "bridge-target-node", strlen ("bridge-target-node")));

    int mandatory = 1;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_router_option (
      source_router, ZLINK_ROUTER_OPT_MANDATORY, &mandatory, sizeof (mandatory)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_bind (target_router, "inproc://spot-route-bridge-router-relay"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_connect (source_router, "inproc://spot-route-bridge-router-relay"));
    std::this_thread::sleep_for (std::chrono::milliseconds (50));

    void *source_bridge = zlink_spot_route_bridge_new (ctx, source_node, NULL);
    void *target_bridge = zlink_spot_route_bridge_new (ctx, target_node, NULL);
    TEST_ASSERT_NOT_NULL (source_bridge);
    TEST_ASSERT_NOT_NULL (target_bridge);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_route_bridge_attach_router_channel (source_bridge, "mesh", source_router, NULL));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_route_bridge_attach_router_channel (target_bridge, "mesh", target_router, NULL));

    zlink_routing_id_t target_node_rid;
    memset (&target_node_rid, 0, sizeof (target_node_rid));
    target_node_rid.size = strlen ("bridge-target-node");
    memcpy (target_node_rid.data, "bridge-target-node", target_node_rid.size);

    zlink_routing_id_t target_spot_rid;
    memset (&target_spot_rid, 0, sizeof (target_spot_rid));
    target_spot_rid.size = strlen ("bridge-target-node");
    memcpy (target_spot_rid.data, "bridge-target-node", target_spot_rid.size);

    zlink_msg_t payload;
    init_text_part (&payload, "bridge-relay");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_route_bridge_send (
      source_bridge, "mesh", &target_node_rid, &target_spot_rid, &payload, 1,
      ZLINK_SEND_FLAGS_NONE));

    zlink_msg_t relay_parts[3];
    zlink_routing_id_t source_node_rid;
    recv_router_multipart (target_router, &source_node_rid, relay_parts, 3);

    bool handled = false;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_route_bridge_handle_router_received (
      target_bridge, "mesh", &source_node_rid, 0, relay_parts, 3, &handled));
    TEST_ASSERT_TRUE (handled);

    const zlink_routing_id_t *recv_source_node_rid = NULL;
    const zlink_routing_id_t *recv_source_spot_rid = NULL;
    uint64_t request_seq = 1;
    zlink_msg_t received;
    zlink_msg_init (&received);
    zlink_part_flag_t has_more = ZLINK_PART_MORE;
    TEST_ASSERT_EQUAL_INT (ZLINK_RECV_OK,
                           zlink_spot_recv_part (target_spot, &recv_source_node_rid,
                                                 &recv_source_spot_rid, &request_seq,
                                                 &received, &has_more,
                                                 ZLINK_RECV_FLAGS_DONTWAIT));
    TEST_ASSERT_EQUAL_UINT64 (0, request_seq);
    TEST_ASSERT_EQUAL_INT (ZLINK_PART_FINAL, has_more);
    TEST_ASSERT_EQUAL_INT (strlen ("bridge-relay"), zlink_msg_size (&received));
    TEST_ASSERT_EQUAL_MEMORY ("bridge-relay", zlink_msg_data (&received),
                              strlen ("bridge-relay"));
    TEST_ASSERT_NOT_NULL (recv_source_node_rid);
    TEST_ASSERT_EQUAL_UINT8 (strlen ("bridge-source-node"), recv_source_node_rid->size);
    TEST_ASSERT_EQUAL_MEMORY ("bridge-source-node", recv_source_node_rid->data,
                              strlen ("bridge-source-node"));
    zlink_msg_close (&received);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_route_bridge_close (source_bridge));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_route_bridge_close (target_bridge));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (source_router));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (target_router));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&target_spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&source_node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&target_node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_bridge_request_router_uses_target_node_rid ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_set (ctx, ZLINK_MAX_SOCKETS, 128));

    void *node = zlink_spot_node_new (ctx, NULL);
    TEST_ASSERT_NOT_NULL (node);
    void *client_router = zlink_socket (ctx, ZLINK_SOCKET_ROUTER);
    void *server_router = zlink_socket (ctx, ZLINK_SOCKET_ROUTER);
    TEST_ASSERT_NOT_NULL (client_router);
    TEST_ASSERT_NOT_NULL (server_router);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (client_router, "bridge-req-router-client",
                                                     strlen ("bridge-req-router-client")));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (server_router, "bridge-req-router-server",
                                                     strlen ("bridge-req-router-server")));

    int mandatory = 1;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_router_option (
      client_router, ZLINK_ROUTER_OPT_MANDATORY, &mandatory, sizeof (mandatory)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_bind (server_router, "inproc://spot-route-bridge-router-request"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_connect (client_router, "inproc://spot-route-bridge-router-request"));
    std::this_thread::sleep_for (std::chrono::milliseconds (50));

    void *bridge = zlink_spot_route_bridge_new (ctx, node, NULL);
    TEST_ASSERT_NOT_NULL (bridge);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_route_bridge_attach_router_channel (bridge, "mesh", client_router, NULL));

    zlink_routing_id_t target_node_rid;
    memset (&target_node_rid, 0, sizeof (target_node_rid));
    target_node_rid.size = strlen ("bridge-req-router-server");
    memcpy (target_node_rid.data, "bridge-req-router-server", target_node_rid.size);

    zlink_routing_id_t target_spot_rid;
    memset (&target_spot_rid, 0, sizeof (target_spot_rid));
    target_spot_rid.size = 11;
    memcpy (target_spot_rid.data, "target-spot", target_spot_rid.size);

    reply_probe_t reply_probe;
    zlink_msg_t payload;
    init_text_part (&payload, "router-question");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_route_bridge_request (
      bridge, "mesh", &target_node_rid, &target_spot_rid, &payload, 1, &capture_reply, &reply_probe,
      ZLINK_SEND_FLAGS_NONE, 1000));

    const zlink_routing_id_t *source_node_rid = NULL;
    const zlink_routing_id_t *source_spot_rid = NULL;
    uint64_t request_seq = 0;
    zlink_msg_t part;
    zlink_part_flag_t has_more = ZLINK_PART_FINAL;
    zlink_msg_init (&part);
    TEST_ASSERT_EQUAL_INT (ZLINK_RECV_OK,
                           zlink_router_recv_part (server_router, &source_node_rid,
                                                   &source_spot_rid, &request_seq, &part,
                                                   &has_more, ZLINK_RECV_FLAGS_NONE));
    TEST_ASSERT_NOT_NULL (source_node_rid);
    TEST_ASSERT_NOT_EQUAL (0, request_seq);
    TEST_ASSERT_EQUAL_INT (strlen ("__zlink.routed_spot.egress.request"), zlink_msg_size (&part));
    TEST_ASSERT_EQUAL_MEMORY ("__zlink.routed_spot.egress.request", zlink_msg_data (&part),
                              strlen ("__zlink.routed_spot.egress.request"));
    TEST_ASSERT_EQUAL_INT (ZLINK_PART_MORE, has_more);
    zlink_msg_close (&part);

    zlink_msg_init (&part);
    TEST_ASSERT_EQUAL_INT (ZLINK_RECV_OK,
                           zlink_router_recv_part (server_router, &source_node_rid,
                                                   &source_spot_rid, &request_seq, &part,
                                                   &has_more, ZLINK_RECV_FLAGS_NONE));
    TEST_ASSERT_EQUAL_INT (target_spot_rid.size, zlink_msg_size (&part));
    TEST_ASSERT_EQUAL_MEMORY (target_spot_rid.data, zlink_msg_data (&part), target_spot_rid.size);
    TEST_ASSERT_EQUAL_INT (ZLINK_PART_MORE, has_more);
    zlink_msg_close (&part);

    zlink_msg_init (&part);
    TEST_ASSERT_EQUAL_INT (ZLINK_RECV_OK,
                           zlink_router_recv_part (server_router, &source_node_rid,
                                                   &source_spot_rid, &request_seq, &part,
                                                   &has_more, ZLINK_RECV_FLAGS_NONE));
    TEST_ASSERT_EQUAL_INT (strlen ("router-question"), zlink_msg_size (&part));
    TEST_ASSERT_EQUAL_MEMORY ("router-question", zlink_msg_data (&part),
                              strlen ("router-question"));
    TEST_ASSERT_EQUAL_INT (ZLINK_PART_FINAL, has_more);
    zlink_msg_close (&part);

    zlink_msg_t reply;
    init_text_part (&reply, "router-answer");
    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_OK,
      zlink_router_reply_part (server_router, source_node_rid, request_seq, &reply,
                               ZLINK_PART_FINAL));
    TEST_ASSERT_TRUE (wait_for_reply (&reply_probe, client_router));
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK, reply_probe.result);
    TEST_ASSERT_EQUAL_INT (1, reply_probe.part_count);
    TEST_ASSERT_EQUAL_STRING ("router-answer", reply_probe.payload.c_str ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_route_bridge_close (bridge));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (client_router));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (server_router));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_bridge_request_to_missing_spot_replies_not_found ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_set (ctx, ZLINK_MAX_SOCKETS, 128));

    void *client_node = zlink_spot_node_new (ctx, NULL);
    void *server_node = zlink_spot_node_new (ctx, NULL);
    TEST_ASSERT_NOT_NULL (client_node);
    TEST_ASSERT_NOT_NULL (server_node);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (server_node, "missing-spot-server-node",
                                                     strlen ("missing-spot-server-node")));

    void *client_router = zlink_socket (ctx, ZLINK_SOCKET_ROUTER);
    void *server_router = zlink_socket (ctx, ZLINK_SOCKET_ROUTER);
    TEST_ASSERT_NOT_NULL (client_router);
    TEST_ASSERT_NOT_NULL (server_router);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (client_router, "missing-spot-client",
                                                     strlen ("missing-spot-client")));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (server_router, "missing-spot-server",
                                                     strlen ("missing-spot-server")));

    int mandatory = 1;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_router_option (
      client_router, ZLINK_ROUTER_OPT_MANDATORY, &mandatory, sizeof (mandatory)));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_bind (server_router, "inproc://spot-route-bridge-missing"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_connect (client_router, "inproc://spot-route-bridge-missing"));
    std::this_thread::sleep_for (std::chrono::milliseconds (50));

    void *client_bridge = zlink_spot_route_bridge_new (ctx, client_node, NULL);
    void *server_bridge = zlink_spot_route_bridge_new (ctx, server_node, NULL);
    TEST_ASSERT_NOT_NULL (client_bridge);
    TEST_ASSERT_NOT_NULL (server_bridge);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_route_bridge_attach_router_channel (client_bridge, "mesh", client_router, NULL));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_route_bridge_attach_router_channel (server_bridge, "mesh", server_router, NULL));

    zlink_routing_id_t target_node_rid;
    memset (&target_node_rid, 0, sizeof (target_node_rid));
    target_node_rid.size = strlen ("missing-spot-server");
    memcpy (target_node_rid.data, "missing-spot-server", target_node_rid.size);

    zlink_routing_id_t missing_spot_rid;
    memset (&missing_spot_rid, 0, sizeof (missing_spot_rid));
    missing_spot_rid.size = strlen ("no-such-spot");
    memcpy (missing_spot_rid.data, "no-such-spot", missing_spot_rid.size);

    reply_probe_t reply_probe;
    zlink_msg_t payload;
    init_text_part (&payload, "anyone-there");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_route_bridge_request (
      client_bridge, "mesh", &target_node_rid, &missing_spot_rid, &payload, 1, &capture_reply,
      &reply_probe, ZLINK_SEND_FLAGS_NONE, 1000));

    //  Feed the relay frames into the server bridge the way a receive pump
    //  would; the addressed spot does not live on this node.
    zlink_msg_t request_parts[8];
    size_t request_part_count = 0;
    const zlink_routing_id_t *source_node_rid = NULL;
    const zlink_routing_id_t *source_spot_rid = NULL;
    uint64_t request_seq = 0;
    zlink_part_flag_t has_more = ZLINK_PART_MORE;
    while (has_more == ZLINK_PART_MORE && request_part_count < 8) {
        zlink_msg_init (&request_parts[request_part_count]);
        TEST_ASSERT_EQUAL_INT (
          ZLINK_RECV_OK,
          zlink_router_recv_part (server_router, &source_node_rid, &source_spot_rid, &request_seq,
                                  &request_parts[request_part_count], &has_more,
                                  ZLINK_RECV_FLAGS_NONE));
        ++request_part_count;
    }
    TEST_ASSERT_EQUAL_INT (ZLINK_PART_FINAL, has_more);
    TEST_ASSERT_NOT_NULL (source_node_rid);
    TEST_ASSERT_NOT_EQUAL (0, request_seq);

    bool handled = false;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_route_bridge_handle_router_received (
      server_bridge, "mesh", source_node_rid, request_seq, request_parts, request_part_count,
      &handled));
    TEST_ASSERT_TRUE (handled);

    //  The requester completes with NOT_FOUND well before the 1s timeout.
    TEST_ASSERT_TRUE (wait_for_reply (&reply_probe, client_router));
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_NOT_FOUND, reply_probe.result);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_route_bridge_close (client_bridge));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_route_bridge_close (server_bridge));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (client_router));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (server_router));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&client_node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&server_node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_handle_received_rejects_malformed_and_request_relay ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_set (ctx, ZLINK_MAX_SOCKETS, 128));

    void *node = zlink_spot_node_new (ctx, NULL);
    TEST_ASSERT_NOT_NULL (node);
    void *router = zlink_socket (ctx, ZLINK_SOCKET_ROUTER);
    TEST_ASSERT_NOT_NULL (router);

    void *bridge = zlink_spot_route_bridge_new (ctx, node, NULL);
    TEST_ASSERT_NOT_NULL (bridge);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_route_bridge_attach_router_channel (bridge, "mesh", router, NULL));

    zlink_msg_t malformed[2];
    init_text_part (&malformed[0], "__zlink.routed_spot.egress.send");
    init_text_part (&malformed[1], "missing-target-payload");
    bool handled = false;
    TEST_ASSERT_EQUAL_INT (
      -1, zlink_spot_route_bridge_handle_router_received (bridge, "mesh", NULL, 0, malformed, 2,
                                                          &handled));
    TEST_ASSERT_TRUE (handled);
    TEST_ASSERT_EQUAL_INT (EPROTO, zlink_errno ());

    zlink_routing_id_t target_spot_rid;
    memset (&target_spot_rid, 0, sizeof (target_spot_rid));
    target_spot_rid.size = 11;
    memcpy (target_spot_rid.data, "target-spot", target_spot_rid.size);

    zlink_msg_t request_parts[3];
    init_text_part (&request_parts[0], "__zlink.routed_spot.egress.request");
    init_buffer_part (&request_parts[1], target_spot_rid.data, target_spot_rid.size);
    init_text_part (&request_parts[2], "question");
    handled = false;
    TEST_ASSERT_EQUAL_INT (
      -1, zlink_spot_route_bridge_handle_router_received (bridge, "mesh", NULL, 0, request_parts,
                                                          3, &handled));
    TEST_ASSERT_TRUE (handled);
    TEST_ASSERT_EQUAL_INT (ENOTSUP, zlink_errno ());

    zlink_msg_t app_part;
    init_text_part (&app_part, "application");
    handled = true;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_route_bridge_handle_router_received (bridge, "mesh", NULL, 0, &app_part, 1,
                                                      &handled));
    TEST_ASSERT_FALSE (handled);
    zlink_msg_close (&app_part);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_route_bridge_close (bridge));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (router));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_handle_router_received_request_replies_to_channel_peer ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_set (ctx, ZLINK_MAX_SOCKETS, 128));

    void *node = zlink_spot_node_new (ctx, NULL);
    TEST_ASSERT_NOT_NULL (node);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_routing_id (node, "node-bridge-request", strlen ("node-bridge-request")));

    void *target_spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (target_spot);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_routing_id (target_spot, "spot-bridge-request",
                            strlen ("spot-bridge-request")));

    zlink_routing_id_t target_spot_rid;
    memset (&target_spot_rid, 0, sizeof (target_spot_rid));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_routing_id (target_spot, &target_spot_rid));

    const zlink_routing_id_t *unused_source_node = NULL;
    const zlink_routing_id_t *unused_source_spot = NULL;
    uint64_t unused_request_seq = 0;
    zlink_msg_t no_data_probe;
    zlink_msg_init (&no_data_probe);
    zlink_part_flag_t unused_more = ZLINK_PART_FINAL;
    TEST_ASSERT_EQUAL_INT (ZLINK_RECV_NO_DATA,
                           zlink_spot_recv_part (target_spot, &unused_source_node,
                                                 &unused_source_spot, &unused_request_seq,
                                                 &no_data_probe, &unused_more,
                                                 ZLINK_RECV_FLAGS_DONTWAIT));
    zlink_msg_close (&no_data_probe);

    void *bridge_router = zlink_socket (ctx, ZLINK_SOCKET_ROUTER);
    void *peer_router = zlink_socket (ctx, ZLINK_SOCKET_ROUTER);
    TEST_ASSERT_NOT_NULL (bridge_router);
    TEST_ASSERT_NOT_NULL (peer_router);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_routing_id (bridge_router, "bridge-router-local", 19));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_routing_id (peer_router, "bridge-router-peer", 18));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_bind (peer_router, "inproc://spot-route-bridge-ingress"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (bridge_router, "inproc://spot-route-bridge-ingress"));
    std::this_thread::sleep_for (std::chrono::milliseconds (50));

    void *bridge = zlink_spot_route_bridge_new (ctx, node, NULL);
    TEST_ASSERT_NOT_NULL (bridge);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_route_bridge_attach_router_channel (bridge, "mesh", bridge_router, NULL));

    zlink_routing_id_t peer_rid;
    memset (&peer_rid, 0, sizeof (peer_rid));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_routing_id (peer_router, &peer_rid));

    zlink_msg_t request_parts[3];
    init_text_part (&request_parts[0], "__zlink.routed_spot.egress.request");
    init_buffer_part (&request_parts[1], target_spot_rid.data, target_spot_rid.size);
    init_text_part (&request_parts[2], "question");

    const uint64_t channel_request_seq = 77;
    bool handled = false;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_route_bridge_handle_router_received (
      bridge, "mesh", &peer_rid, channel_request_seq, request_parts, 3, &handled));
    TEST_ASSERT_TRUE (handled);

    const zlink_routing_id_t *source_node_rid = NULL;
    const zlink_routing_id_t *source_spot_rid = NULL;
    uint64_t local_request_seq = 0;
    zlink_msg_t received;
    zlink_msg_init (&received);
    zlink_part_flag_t has_more = ZLINK_PART_MORE;
    TEST_ASSERT_EQUAL_INT (ZLINK_RECV_OK,
                           zlink_spot_recv_part (target_spot, &source_node_rid,
                                                 &source_spot_rid, &local_request_seq, &received,
                                                 &has_more, ZLINK_RECV_FLAGS_DONTWAIT));
    TEST_ASSERT_NOT_NULL (source_node_rid);
    TEST_ASSERT_NOT_EQUAL (0, local_request_seq);
    TEST_ASSERT_EQUAL_INT (strlen ("bridge:mesh:bridge-router-local"), source_node_rid->size);
    TEST_ASSERT_EQUAL_MEMORY ("bridge:mesh:bridge-router-local", source_node_rid->data,
                              strlen ("bridge:mesh:bridge-router-local"));
    TEST_ASSERT_EQUAL_INT (ZLINK_PART_FINAL, has_more);
    TEST_ASSERT_EQUAL_INT (strlen ("question"), zlink_msg_size (&received));
    TEST_ASSERT_EQUAL_MEMORY ("question", zlink_msg_data (&received), strlen ("question"));
    zlink_msg_close (&received);

    zlink_msg_t reply;
    init_text_part (&reply, "answer");
    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_OK,
      zlink_spot_reply_router_part (target_spot, source_node_rid, local_request_seq, &reply,
                                    ZLINK_PART_FINAL));

    for (int attempt = 0; attempt < 100; ++attempt) {
        drain_router_progress (bridge_router);
        const zlink_routing_id_t *reply_source_node = NULL;
        const zlink_routing_id_t *reply_source_spot = NULL;
        uint64_t reply_seq = 0;
        zlink_msg_t reply_part;
        zlink_msg_init (&reply_part);
        zlink_part_flag_t reply_more = ZLINK_PART_FINAL;
        const zlink_recv_result_t recv_rc =
          zlink_router_recv_part (peer_router, &reply_source_node, &reply_source_spot, &reply_seq,
                                  &reply_part, &reply_more, ZLINK_RECV_FLAGS_DONTWAIT);
        if (recv_rc == ZLINK_RECV_OK) {
            TEST_ASSERT_EQUAL_UINT64 (channel_request_seq, reply_seq);
            TEST_ASSERT_EQUAL_INT (strlen ("answer"), zlink_msg_size (&reply_part));
            TEST_ASSERT_EQUAL_MEMORY ("answer", zlink_msg_data (&reply_part), strlen ("answer"));
            TEST_ASSERT_EQUAL_INT (ZLINK_PART_FINAL, reply_more);
            zlink_msg_close (&reply_part);
            goto received_reply;
        }
        zlink_msg_close (&reply_part);
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }
    TEST_FAIL_MESSAGE ("router bridge reply was not delivered to the channel peer");

received_reply:
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_route_bridge_close (bridge));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (bridge_router));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (peer_router));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&target_spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_bridge_request_roundtrips_through_target_bridge_spot ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_set (ctx, ZLINK_MAX_SOCKETS, 128));

    void *source_node = zlink_spot_node_new (ctx, NULL);
    void *target_node = zlink_spot_node_new (ctx, NULL);
    TEST_ASSERT_NOT_NULL (source_node);
    TEST_ASSERT_NOT_NULL (target_node);

    void *target_spot = zlink_spot_new (target_node);
    TEST_ASSERT_NOT_NULL (target_spot);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_routing_id (target_spot, "bridge-roundtrip-spot",
                            strlen ("bridge-roundtrip-spot")));

    void *source_router = zlink_socket (ctx, ZLINK_SOCKET_ROUTER);
    void *target_router = zlink_socket (ctx, ZLINK_SOCKET_ROUTER);
    TEST_ASSERT_NOT_NULL (source_router);
    TEST_ASSERT_NOT_NULL (target_router);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_routing_id (source_router, "bridge-roundtrip-source",
                            strlen ("bridge-roundtrip-source")));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_routing_id (target_router, "bridge-roundtrip-target",
                            strlen ("bridge-roundtrip-target")));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_bind (target_router, "inproc://spot-route-bridge-roundtrip"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_connect (source_router, "inproc://spot-route-bridge-roundtrip"));
    std::this_thread::sleep_for (std::chrono::milliseconds (50));

    void *source_bridge = zlink_spot_route_bridge_new (ctx, source_node, NULL);
    void *target_bridge = zlink_spot_route_bridge_new (ctx, target_node, NULL);
    TEST_ASSERT_NOT_NULL (source_bridge);
    TEST_ASSERT_NOT_NULL (target_bridge);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_route_bridge_attach_router_channel (source_bridge, "mesh", source_router, NULL));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_route_bridge_attach_router_channel (target_bridge, "mesh", target_router, NULL));

    zlink_routing_id_t target_node_rid;
    memset (&target_node_rid, 0, sizeof (target_node_rid));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_routing_id (target_router, &target_node_rid));
    zlink_routing_id_t target_spot_rid;
    memset (&target_spot_rid, 0, sizeof (target_spot_rid));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_routing_id (target_spot, &target_spot_rid));

    reply_probe_t reply_probe;
    zlink_msg_t payload;
    init_text_part (&payload, "roundtrip-question");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_route_bridge_request (
      source_bridge, "mesh", &target_node_rid, &target_spot_rid, &payload, 1, &capture_reply,
      &reply_probe, ZLINK_SEND_FLAGS_NONE, 1000));

    bool handled = false;
    for (int attempt = 0; attempt < 100 && !handled; ++attempt) {
        const zlink_routing_id_t *source_node_rid = NULL;
        const zlink_routing_id_t *source_spot_rid = NULL;
        uint64_t request_seq = 0;
        zlink_msg_t parts[3];
        zlink_recv_result_t rc = ZLINK_RECV_NO_DATA;
        for (size_t index = 0; index < 3; ++index) {
            zlink_msg_init (&parts[index]);
            zlink_part_flag_t has_more = ZLINK_PART_FINAL;
            rc = zlink_router_recv_part (target_router, &source_node_rid, &source_spot_rid,
                                         &request_seq, &parts[index], &has_more,
                                         ZLINK_RECV_FLAGS_DONTWAIT);
            if (rc != ZLINK_RECV_OK) {
                for (size_t close_index = 0; close_index <= index; ++close_index)
                    zlink_msg_close (&parts[close_index]);
                break;
            }
        }
        if (rc == ZLINK_RECV_OK) {
            TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_route_bridge_handle_router_received (
              target_bridge, "mesh", source_node_rid, request_seq, parts, 3, &handled));
        } else {
            std::this_thread::sleep_for (std::chrono::milliseconds (1));
        }
    }
    TEST_ASSERT_TRUE (handled);

    const zlink_routing_id_t *routed_source_node_rid = NULL;
    const zlink_routing_id_t *routed_source_spot_rid = NULL;
    uint64_t routed_request_seq = 0;
    zlink_msg_t routed;
    zlink_msg_init (&routed);
    zlink_part_flag_t routed_more = ZLINK_PART_FINAL;
    TEST_ASSERT_EQUAL_INT (ZLINK_RECV_OK,
                           zlink_spot_recv_part (target_spot, &routed_source_node_rid,
                                                 &routed_source_spot_rid, &routed_request_seq,
                                                 &routed, &routed_more,
                                                 ZLINK_RECV_FLAGS_DONTWAIT));
    TEST_ASSERT_EQUAL_INT (strlen ("roundtrip-question"), zlink_msg_size (&routed));
    TEST_ASSERT_EQUAL_MEMORY ("roundtrip-question", zlink_msg_data (&routed),
                              strlen ("roundtrip-question"));
    zlink_msg_close (&routed);

    zlink_msg_t reply;
    init_text_part (&reply, "roundtrip-answer");
    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_OK,
      zlink_spot_reply_router_part (target_spot, routed_source_node_rid, routed_request_seq,
                                    &reply, ZLINK_PART_FINAL));

    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now () + std::chrono::seconds (3);
    while (std::chrono::steady_clock::now () < deadline) {
        {
            std::unique_lock<std::mutex> lock (reply_probe.mutex);
            if (reply_probe.cv.wait_for (lock, std::chrono::milliseconds (10),
                                         [&reply_probe] () { return reply_probe.done; }))
                break;
        }
        (void) zlink_spot_route_bridge_drain (target_bridge);
        (void) zlink_spot_route_bridge_drain (source_bridge);
    }
    TEST_ASSERT_TRUE (reply_probe.done);
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK, reply_probe.result);
    TEST_ASSERT_EQUAL_INT (1, reply_probe.part_count);
    TEST_ASSERT_EQUAL_STRING ("roundtrip-answer", reply_probe.payload.c_str ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_route_bridge_close (source_bridge));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_route_bridge_close (target_bridge));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (source_router));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (target_router));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&target_spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&source_node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&target_node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_bridge_request_timeout_ignores_late_spot_reply_and_allows_next_request ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_set (ctx, ZLINK_MAX_SOCKETS, 128));

    void *source_node = zlink_spot_node_new (ctx, NULL);
    void *target_node = zlink_spot_node_new (ctx, NULL);
    TEST_ASSERT_NOT_NULL (source_node);
    TEST_ASSERT_NOT_NULL (target_node);

    void *target_spot = zlink_spot_new (target_node);
    TEST_ASSERT_NOT_NULL (target_spot);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_routing_id (target_spot, "bridge-timeout-spot", strlen ("bridge-timeout-spot")));

    void *source_router = zlink_socket (ctx, ZLINK_SOCKET_ROUTER);
    void *target_router = zlink_socket (ctx, ZLINK_SOCKET_ROUTER);
    TEST_ASSERT_NOT_NULL (source_router);
    TEST_ASSERT_NOT_NULL (target_router);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_routing_id (source_router, "bridge-timeout-source",
                            strlen ("bridge-timeout-source")));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_routing_id (target_router, "bridge-timeout-target",
                            strlen ("bridge-timeout-target")));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_bind (target_router, "inproc://spot-route-bridge-timeout"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (source_router, "inproc://spot-route-bridge-timeout"));
    std::this_thread::sleep_for (std::chrono::milliseconds (50));

    void *source_bridge = zlink_spot_route_bridge_new (ctx, source_node, NULL);
    void *target_bridge = zlink_spot_route_bridge_new (ctx, target_node, NULL);
    TEST_ASSERT_NOT_NULL (source_bridge);
    TEST_ASSERT_NOT_NULL (target_bridge);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_route_bridge_attach_router_channel (source_bridge, "mesh", source_router, NULL));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_route_bridge_attach_router_channel (target_bridge, "mesh", target_router, NULL));

    zlink_routing_id_t target_node_rid;
    memset (&target_node_rid, 0, sizeof (target_node_rid));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_routing_id (target_router, &target_node_rid));
    zlink_routing_id_t target_spot_rid;
    memset (&target_spot_rid, 0, sizeof (target_spot_rid));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_routing_id (target_spot, &target_spot_rid));

    reply_probe_t timed_out_probe;
    zlink_msg_t timeout_payload;
    init_text_part (&timeout_payload, "timeout-question");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_route_bridge_request (
      source_bridge, "mesh", &target_node_rid, &target_spot_rid, &timeout_payload, 1,
      &capture_reply, &timed_out_probe, ZLINK_SEND_FLAGS_NONE, 20));

    bool handled = false;
    zlink_routing_id_t source_node_rid;
    zlink_msg_t routed_parts[3];
    const uint64_t channel_request_seq =
      recv_router_request_multipart (target_router, &source_node_rid, routed_parts, 3);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_route_bridge_handle_router_received (
      target_bridge, "mesh", &source_node_rid, channel_request_seq, routed_parts, 3, &handled));
    TEST_ASSERT_TRUE (handled);

    const zlink_routing_id_t *routed_source_node_rid = NULL;
    const zlink_routing_id_t *routed_source_spot_rid = NULL;
    uint64_t routed_request_seq = 0;
    zlink_msg_t routed;
    zlink_msg_init (&routed);
    zlink_part_flag_t routed_more = ZLINK_PART_FINAL;
    TEST_ASSERT_EQUAL_INT (ZLINK_RECV_OK,
                           zlink_spot_recv_part (target_spot, &routed_source_node_rid,
                                                 &routed_source_spot_rid, &routed_request_seq,
                                                 &routed, &routed_more,
                                                 ZLINK_RECV_FLAGS_DONTWAIT));
    TEST_ASSERT_NOT_EQUAL (0, routed_request_seq);
    zlink_msg_close (&routed);

    TEST_ASSERT_TRUE (wait_for_reply_with_bridge_drain (&timed_out_probe, source_bridge));
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_TIMED_OUT, timed_out_probe.result);

    zlink_msg_t late_reply;
    init_text_part (&late_reply, "late-answer");
    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_OK,
      zlink_spot_reply_router_part (target_spot, routed_source_node_rid, routed_request_seq,
                                    &late_reply, ZLINK_PART_FINAL));
    for (int attempt = 0; attempt < 20; ++attempt) {
        (void) zlink_spot_route_bridge_drain (target_bridge);
        (void) zlink_spot_route_bridge_drain (source_bridge);
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }

    reply_probe_t fresh_probe;
    zlink_msg_t fresh_payload;
    init_text_part (&fresh_payload, "fresh-question");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_route_bridge_request (
      source_bridge, "mesh", &target_node_rid, &target_spot_rid, &fresh_payload, 1,
      &capture_reply, &fresh_probe, ZLINK_SEND_FLAGS_NONE, 1000));

    zlink_routing_id_t fresh_source_node_rid;
    zlink_msg_t fresh_routed_parts[3];
    const uint64_t fresh_channel_request_seq =
      recv_router_request_multipart (target_router, &fresh_source_node_rid, fresh_routed_parts, 3);
    handled = false;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_route_bridge_handle_router_received (
      target_bridge, "mesh", &fresh_source_node_rid, fresh_channel_request_seq,
      fresh_routed_parts, 3, &handled));
    TEST_ASSERT_TRUE (handled);

    routed_source_node_rid = NULL;
    routed_source_spot_rid = NULL;
    routed_request_seq = 0;
    zlink_msg_init (&routed);
    routed_more = ZLINK_PART_FINAL;
    TEST_ASSERT_EQUAL_INT (ZLINK_RECV_OK,
                           zlink_spot_recv_part (target_spot, &routed_source_node_rid,
                                                 &routed_source_spot_rid, &routed_request_seq,
                                                 &routed, &routed_more,
                                                 ZLINK_RECV_FLAGS_DONTWAIT));
    TEST_ASSERT_NOT_EQUAL (0, routed_request_seq);
    zlink_msg_close (&routed);

    zlink_msg_t fresh_reply;
    init_text_part (&fresh_reply, "fresh-answer");
    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_OK,
      zlink_spot_reply_router_part (target_spot, routed_source_node_rid, routed_request_seq,
                                    &fresh_reply, ZLINK_PART_FINAL));
    {
        const std::chrono::steady_clock::time_point deadline =
          std::chrono::steady_clock::now () + std::chrono::seconds (3);
        while (std::chrono::steady_clock::now () < deadline) {
            {
                std::unique_lock<std::mutex> lock (fresh_probe.mutex);
                if (fresh_probe.cv.wait_for (lock, std::chrono::milliseconds (10),
                                             [&fresh_probe] () { return fresh_probe.done; }))
                    break;
            }
            (void) zlink_spot_route_bridge_drain (target_bridge);
            (void) zlink_spot_route_bridge_drain (source_bridge);
        }
    }
    TEST_ASSERT_TRUE (fresh_probe.done);
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK, fresh_probe.result);
    TEST_ASSERT_EQUAL_STRING ("fresh-answer", fresh_probe.payload.c_str ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_route_bridge_close (source_bridge));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_route_bridge_close (target_bridge));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (source_router));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (target_router));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&target_spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&source_node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&target_node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_bridge_close_cancels_pending_router_reply ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_set (ctx, ZLINK_MAX_SOCKETS, 128));

    void *node = zlink_spot_node_new (ctx, NULL);
    TEST_ASSERT_NOT_NULL (node);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_routing_id (node, "node-bridge-close", strlen ("node-bridge-close")));

    void *target_spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (target_spot);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_routing_id (target_spot, "spot-bridge-close", strlen ("spot-bridge-close")));

    zlink_routing_id_t target_spot_rid;
    memset (&target_spot_rid, 0, sizeof (target_spot_rid));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_routing_id (target_spot, &target_spot_rid));

    void *bridge_router = zlink_socket (ctx, ZLINK_SOCKET_ROUTER);
    void *peer_router = zlink_socket (ctx, ZLINK_SOCKET_ROUTER);
    TEST_ASSERT_NOT_NULL (bridge_router);
    TEST_ASSERT_NOT_NULL (peer_router);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_routing_id (bridge_router, "bridge-close-local", strlen ("bridge-close-local")));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_routing_id (peer_router, "bridge-close-peer", strlen ("bridge-close-peer")));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_bind (peer_router, "inproc://spot-route-bridge-close-pending"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_connect (bridge_router, "inproc://spot-route-bridge-close-pending"));
    std::this_thread::sleep_for (std::chrono::milliseconds (50));

    void *bridge = zlink_spot_route_bridge_new (ctx, node, NULL);
    TEST_ASSERT_NOT_NULL (bridge);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_route_bridge_attach_router_channel (bridge, "mesh", bridge_router, NULL));

    zlink_routing_id_t peer_rid;
    memset (&peer_rid, 0, sizeof (peer_rid));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_routing_id (peer_router, &peer_rid));

    zlink_msg_t request_parts[3];
    init_text_part (&request_parts[0], "__zlink.routed_spot.egress.request");
    init_buffer_part (&request_parts[1], target_spot_rid.data, target_spot_rid.size);
    init_text_part (&request_parts[2], "question-before-close");

    bool handled = false;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_route_bridge_handle_router_received (
      bridge, "mesh", &peer_rid, 91, request_parts, 3, &handled));
    TEST_ASSERT_TRUE (handled);

    const zlink_routing_id_t *source_node_rid = NULL;
    const zlink_routing_id_t *source_spot_rid = NULL;
    uint64_t local_request_seq = 0;
    zlink_msg_t received;
    zlink_msg_init (&received);
    zlink_part_flag_t has_more = ZLINK_PART_MORE;
    TEST_ASSERT_EQUAL_INT (ZLINK_RECV_OK,
                           zlink_spot_recv_part (target_spot, &source_node_rid,
                                                 &source_spot_rid, &local_request_seq, &received,
                                                 &has_more, ZLINK_RECV_FLAGS_DONTWAIT));
    TEST_ASSERT_NOT_NULL (source_node_rid);
    TEST_ASSERT_NOT_EQUAL (0, local_request_seq);
    zlink_msg_close (&received);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_route_bridge_close (bridge));

    zlink_msg_t reply;
    init_text_part (&reply, "late-answer");
    const zlink_submit_result_t reply_rc =
      zlink_spot_reply_router_part (target_spot, source_node_rid, local_request_seq, &reply,
                                    ZLINK_PART_FINAL);
    if (reply_rc != ZLINK_SUBMIT_OK)
        zlink_msg_close (&reply);

    for (int attempt = 0; attempt < 10; ++attempt) {
        drain_router_progress (bridge_router);
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }

    const zlink_routing_id_t *reply_source_node = NULL;
    const zlink_routing_id_t *reply_source_spot = NULL;
    uint64_t reply_seq = 0;
    zlink_msg_t reply_part;
    zlink_msg_init (&reply_part);
    zlink_part_flag_t reply_more = ZLINK_PART_FINAL;
    TEST_ASSERT_EQUAL_INT (ZLINK_RECV_NO_DATA,
                           zlink_router_recv_part (peer_router, &reply_source_node,
                                                   &reply_source_spot, &reply_seq, &reply_part,
                                                   &reply_more, ZLINK_RECV_FLAGS_DONTWAIT));
    zlink_msg_close (&reply_part);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (bridge_router));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (peer_router));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&target_spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

}

int main ()
{
    UNITY_BEGIN ();
    RUN_TEST (test_spot_node_publisher_publish_uses_local_topic_plane);
    RUN_TEST (test_bridge_send_router_uses_target_node_rid);
    RUN_TEST (test_bridge_send_router_relay_delivers_to_target_bridge_spot);
    RUN_TEST (test_bridge_request_router_uses_target_node_rid);
    RUN_TEST (test_bridge_request_to_missing_spot_replies_not_found);
    RUN_TEST (test_handle_received_rejects_malformed_and_request_relay);
    RUN_TEST (test_handle_router_received_request_replies_to_channel_peer);
    RUN_TEST (test_bridge_request_roundtrips_through_target_bridge_spot);
    RUN_TEST (test_bridge_request_timeout_ignores_late_spot_reply_and_allows_next_request);
    RUN_TEST (test_bridge_close_cancels_pending_router_reply);
    return UNITY_END ();
}
