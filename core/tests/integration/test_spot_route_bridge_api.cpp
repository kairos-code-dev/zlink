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

void test_bridge_accepts_borrowed_dealer_and_router_endpoints ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_set (ctx, ZLINK_MAX_SOCKETS, 128));

    void *node = zlink_spot_node_new (ctx, NULL);
    TEST_ASSERT_NOT_NULL (node);

    void *dealer = zlink_socket (ctx, ZLINK_SOCKET_DEALER);
    void *router = zlink_socket (ctx, ZLINK_SOCKET_ROUTER);
    TEST_ASSERT_NOT_NULL (dealer);
    TEST_ASSERT_NOT_NULL (router);

    void *bridge = zlink_spot_route_bridge_new (ctx, node, NULL);
    TEST_ASSERT_NOT_NULL (bridge);

    zlink_spot_route_bridge_endpoint_options_t options;
    memset (&options, 0, sizeof (options));
    options.struct_size = sizeof (options);
    options.capabilities = ZLINK_SPOT_ROUTE_BRIDGE_ROUTE_WITH_CHANNEL_INBOUND;

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_route_bridge_attach_dealer_channel (bridge, "client", dealer, &options));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_route_bridge_attach_router_channel (bridge, "mesh", router, &options));

    zlink_routing_id_t target;
    memset (&target, 0, sizeof (target));
    target.size = 4;
    memcpy (target.data, "node", 4);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_route_bridge_set_target_node (bridge, "mesh", &target));

    bool handled = true;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_route_bridge_handle_dealer_received (bridge, "client", NULL, 0, &handled));
    TEST_ASSERT_FALSE (handled);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_route_bridge_close (bridge));

    zlink_msg_t ping;
    init_text_part (&ping, "ping");
    const zlink_submit_result_t send_rc =
      zlink_send_part (dealer, &ping, ZLINK_SEND_FLAGS_NONE, ZLINK_PART_FINAL);
    TEST_ASSERT_NOT_EQUAL (ZLINK_SUBMIT_INVALID_HANDLE, send_rc);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (dealer));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (router));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_bridge_rejects_socket_kind_mismatch ()
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

    TEST_ASSERT_EQUAL_INT (
      -1, zlink_spot_route_bridge_attach_dealer_channel (bridge, "bad", router, NULL));
    TEST_ASSERT_EQUAL_INT (EINVAL, zlink_errno ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_route_bridge_close (bridge));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (router));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_legacy_spot_attach_apis_return_migration_error ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_set (ctx, ZLINK_MAX_SOCKETS, 128));

    void *node = zlink_spot_node_new (ctx, NULL);
    TEST_ASSERT_NOT_NULL (node);
    void *dealer = zlink_socket (ctx, ZLINK_SOCKET_DEALER);
    void *router = zlink_socket (ctx, ZLINK_SOCKET_ROUTER);
    void *pub = zlink_socket (ctx, ZLINK_SOCKET_PUB);
    TEST_ASSERT_NOT_NULL (dealer);
    TEST_ASSERT_NOT_NULL (router);
    TEST_ASSERT_NOT_NULL (pub);

    zlink_routing_id_t router_rid;
    memset (&router_rid, 0, sizeof (router_rid));
    router_rid.size = strlen ("legacy-router");
    memcpy (router_rid.data, "legacy-router", router_rid.size);

    TEST_ASSERT_NOT_EQUAL (
      ZLINK_CONNECT_OK,
      zlink_spot_node_connect_router_channel_peer (node, "legacy", "tcp://127.0.0.1:1"));
    TEST_ASSERT_EQUAL_INT (ENOTSUP, zlink_errno ());
    TEST_ASSERT_NOT_EQUAL (ZLINK_CONNECT_OK,
                           zlink_spot_node_connect_router_channel_peer_rid (
                             node, "legacy", &router_rid, "tcp://127.0.0.1:1"));
    TEST_ASSERT_EQUAL_INT (ENOTSUP, zlink_errno ());
    TEST_ASSERT_NOT_EQUAL (
      ZLINK_CONNECT_OK,
      zlink_spot_node_disconnect_router_channel_peer (node, "legacy", "tcp://127.0.0.1:1"));
    TEST_ASSERT_EQUAL_INT (ENOTSUP, zlink_errno ());
    TEST_ASSERT_NOT_EQUAL (
      ZLINK_CONNECT_OK,
      zlink_spot_node_disconnect_router_channel_peer_rid (node, "legacy", &router_rid));
    TEST_ASSERT_EQUAL_INT (ENOTSUP, zlink_errno ());

    void *discovery = zlink_discovery_new (ctx, ZLINK_AUTO_CONNECT_CLIENT_SERVER, "legacy");
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_NOT_EQUAL (
      ZLINK_CONFIG_OK, zlink_spot_node_attach_router_channel_discovery (node, "legacy", discovery));
    TEST_ASSERT_EQUAL_INT (ENOTSUP, zlink_errno ());
    TEST_ASSERT_NOT_EQUAL (ZLINK_CONFIG_OK,
                           zlink_spot_node_attach_channel_dealer (node, discovery, dealer));
    TEST_ASSERT_EQUAL_INT (ENOTSUP, zlink_errno ());
    TEST_ASSERT_NOT_EQUAL (
      ZLINK_CONFIG_OK, zlink_spot_node_attach_channel_dealer_manual (node, "legacy", dealer));
    TEST_ASSERT_EQUAL_INT (ENOTSUP, zlink_errno ());
    TEST_ASSERT_NOT_EQUAL (ZLINK_CONFIG_OK, zlink_spot_node_attach_pub_ingress (node, pub));
    TEST_ASSERT_EQUAL_INT (ENOTSUP, zlink_errno ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (pub));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (router));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (dealer));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_bridge_enforces_spot_route_capability ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_set (ctx, ZLINK_MAX_SOCKETS, 128));

    void *node = zlink_spot_node_new (ctx, NULL);
    TEST_ASSERT_NOT_NULL (node);
    void *dealer = zlink_socket (ctx, ZLINK_SOCKET_DEALER);
    TEST_ASSERT_NOT_NULL (dealer);

    void *bridge = zlink_spot_route_bridge_new (ctx, node, NULL);
    TEST_ASSERT_NOT_NULL (bridge);

    zlink_spot_route_bridge_endpoint_options_t options;
    memset (&options, 0, sizeof (options));
    options.struct_size = sizeof (options);
    options.capabilities = ZLINK_SPOT_ROUTE_BRIDGE_CAP_CHANNEL_INBOUND;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_route_bridge_attach_dealer_channel (bridge, "client", dealer, &options));

    zlink_spot_route_bridge_summary_t summary;
    memset (&summary, 0, sizeof (summary));
    summary.struct_size = sizeof (summary);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_route_bridge_summary (bridge, &summary));
    TEST_ASSERT_EQUAL_UINT32 (1, summary.attached_channel_count);
    TEST_ASSERT_EQUAL_UINT64 (0, summary.rejected_inbound_count);
    TEST_ASSERT_EQUAL_UINT64 (0, summary.routed_send_failure_count);

    zlink_routing_id_t target_spot_rid;
    memset (&target_spot_rid, 0, sizeof (target_spot_rid));
    target_spot_rid.size = 11;
    memcpy (target_spot_rid.data, "target-spot", target_spot_rid.size);

    zlink_msg_t payload;
    init_text_part (&payload, "blocked");
    TEST_ASSERT_EQUAL_INT (-1, zlink_spot_route_bridge_send (
                                 bridge, "client", &target_spot_rid, &payload, 1,
                                 ZLINK_SEND_FLAGS_NONE));
    TEST_ASSERT_EQUAL_INT (EPERM, zlink_errno ());
    zlink_msg_close (&payload);
    memset (&summary, 0, sizeof (summary));
    summary.struct_size = sizeof (summary);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_route_bridge_summary (bridge, &summary));
    TEST_ASSERT_EQUAL_UINT64 (1, summary.routed_send_failure_count);

    zlink_msg_t relay_parts[3];
    init_text_part (&relay_parts[0], "__zlink.routed_spot.egress.send");
    init_buffer_part (&relay_parts[1], target_spot_rid.data, target_spot_rid.size);
    init_text_part (&relay_parts[2], "blocked");
    bool handled = false;
    TEST_ASSERT_EQUAL_INT (
      -1, zlink_spot_route_bridge_handle_dealer_received (bridge, "client", relay_parts, 3,
                                                          &handled));
    TEST_ASSERT_TRUE (handled);
    TEST_ASSERT_EQUAL_INT (EPERM, zlink_errno ());
    memset (&summary, 0, sizeof (summary));
    summary.struct_size = sizeof (summary);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_route_bridge_summary (bridge, &summary));
    TEST_ASSERT_EQUAL_UINT64 (1, summary.rejected_inbound_count);

    zlink_msg_t app_part;
    init_text_part (&app_part, "application");
    handled = true;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_route_bridge_handle_dealer_received (bridge, "client", &app_part, 1, &handled));
    TEST_ASSERT_FALSE (handled);
    zlink_msg_close (&app_part);
    memset (&summary, 0, sizeof (summary));
    summary.struct_size = sizeof (summary);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_route_bridge_summary (bridge, &summary));
    TEST_ASSERT_EQUAL_UINT64 (1, summary.rejected_inbound_count);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_route_bridge_close (bridge));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (dealer));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
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

void test_bridge_send_dealer_emits_relay_packet ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_set (ctx, ZLINK_MAX_SOCKETS, 128));

    void *node = zlink_spot_node_new (ctx, NULL);
    TEST_ASSERT_NOT_NULL (node);
    void *dealer = zlink_socket (ctx, ZLINK_SOCKET_DEALER);
    void *router = zlink_socket (ctx, ZLINK_SOCKET_ROUTER);
    TEST_ASSERT_NOT_NULL (dealer);
    TEST_ASSERT_NOT_NULL (router);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (dealer, "bridge-dealer", 13));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_bind (router, "inproc://spot-route-bridge-egress"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (dealer, "inproc://spot-route-bridge-egress"));
    std::this_thread::sleep_for (std::chrono::milliseconds (20));

    void *bridge = zlink_spot_route_bridge_new (ctx, node, NULL);
    TEST_ASSERT_NOT_NULL (bridge);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_route_bridge_attach_dealer_channel (bridge, "client", dealer, NULL));

    zlink_routing_id_t target_spot_rid;
    memset (&target_spot_rid, 0, sizeof (target_spot_rid));
    target_spot_rid.size = 11;
    memcpy (target_spot_rid.data, "target-spot", target_spot_rid.size);

    zlink_msg_t payload;
    init_text_part (&payload, "hello");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_route_bridge_send (
      bridge, "client", &target_spot_rid, &payload, 1, ZLINK_SEND_FLAGS_NONE));

    assert_recv_text_part (router, "__zlink.routed_spot.egress.send", ZLINK_PART_MORE);
    assert_recv_buffer_part (router, target_spot_rid.data, target_spot_rid.size, ZLINK_PART_MORE);
    assert_recv_text_part (router, "hello", ZLINK_PART_FINAL);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_route_bridge_close (bridge));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (dealer));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (router));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_bridge_request_dealer_uses_socket_request_reply ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_set (ctx, ZLINK_MAX_SOCKETS, 128));

    void *node = zlink_spot_node_new (ctx, NULL);
    TEST_ASSERT_NOT_NULL (node);
    void *dealer = zlink_socket (ctx, ZLINK_SOCKET_DEALER);
    void *router = zlink_socket (ctx, ZLINK_SOCKET_ROUTER);
    TEST_ASSERT_NOT_NULL (dealer);
    TEST_ASSERT_NOT_NULL (router);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (dealer, "bridge-req-dealer", 17));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_bind (router, "inproc://spot-route-bridge-request"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (dealer, "inproc://spot-route-bridge-request"));
    std::this_thread::sleep_for (std::chrono::milliseconds (20));

    void *bridge = zlink_spot_route_bridge_new (ctx, node, NULL);
    TEST_ASSERT_NOT_NULL (bridge);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_route_bridge_attach_dealer_channel (bridge, "client", dealer, NULL));

    zlink_routing_id_t target_spot_rid;
    memset (&target_spot_rid, 0, sizeof (target_spot_rid));
    target_spot_rid.size = 11;
    memcpy (target_spot_rid.data, "target-spot", target_spot_rid.size);

    reply_probe_t reply_probe;
    zlink_msg_t payload;
    init_text_part (&payload, "question");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_route_bridge_request (
      bridge, "client", &target_spot_rid, &payload, 1, &capture_reply, &reply_probe,
      ZLINK_SEND_FLAGS_NONE, 1000));

    const zlink_routing_id_t *source_node_rid = NULL;
    const zlink_routing_id_t *source_spot_rid = NULL;
    uint64_t request_seq = 0;
    zlink_msg_t part;
    zlink_part_flag_t has_more = ZLINK_PART_FINAL;
    zlink_msg_init (&part);
    TEST_ASSERT_EQUAL_INT (ZLINK_RECV_OK,
                           zlink_router_recv_part (router, &source_node_rid, &source_spot_rid,
                                                   &request_seq, &part, &has_more,
                                                   ZLINK_RECV_FLAGS_NONE));
    TEST_ASSERT_NOT_NULL (source_node_rid);
    TEST_ASSERT_NOT_EQUAL (0, request_seq);
    TEST_ASSERT_EQUAL_INT (strlen ("__zlink.routed_spot.egress.request"), zlink_msg_size (&part));
    TEST_ASSERT_EQUAL_MEMORY ("__zlink.routed_spot.egress.request", zlink_msg_data (&part),
                              strlen ("__zlink.routed_spot.egress.request"));
    TEST_ASSERT_EQUAL_INT (ZLINK_PART_MORE, has_more);
    zlink_msg_close (&part);

    zlink_msg_init (&part);
    TEST_ASSERT_EQUAL_INT (ZLINK_RECV_OK,
                           zlink_router_recv_part (router, &source_node_rid, &source_spot_rid,
                                                   &request_seq, &part, &has_more,
                                                   ZLINK_RECV_FLAGS_NONE));
    TEST_ASSERT_EQUAL_INT (target_spot_rid.size, zlink_msg_size (&part));
    TEST_ASSERT_EQUAL_MEMORY (target_spot_rid.data, zlink_msg_data (&part), target_spot_rid.size);
    TEST_ASSERT_EQUAL_INT (ZLINK_PART_MORE, has_more);
    zlink_msg_close (&part);

    zlink_msg_init (&part);
    TEST_ASSERT_EQUAL_INT (ZLINK_RECV_OK,
                           zlink_router_recv_part (router, &source_node_rid, &source_spot_rid,
                                                   &request_seq, &part, &has_more,
                                                   ZLINK_RECV_FLAGS_NONE));
    TEST_ASSERT_EQUAL_INT (strlen ("question"), zlink_msg_size (&part));
    TEST_ASSERT_EQUAL_MEMORY ("question", zlink_msg_data (&part), strlen ("question"));
    TEST_ASSERT_EQUAL_INT (ZLINK_PART_FINAL, has_more);
    zlink_msg_close (&part);

    zlink_msg_t reply;
    init_text_part (&reply, "answer");
    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_OK,
      zlink_router_reply_part (router, source_node_rid, request_seq, &reply, ZLINK_PART_FINAL));
    TEST_ASSERT_TRUE (wait_for_reply (&reply_probe, dealer));
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK, reply_probe.result);
    TEST_ASSERT_EQUAL_INT (1, reply_probe.part_count);
    TEST_ASSERT_EQUAL_STRING ("answer", reply_probe.payload.c_str ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_route_bridge_close (bridge));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (dealer));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (router));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_bridge_request_uses_default_timeout_when_call_timeout_is_zero ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_set (ctx, ZLINK_MAX_SOCKETS, 128));

    void *node = zlink_spot_node_new (ctx, NULL);
    TEST_ASSERT_NOT_NULL (node);
    void *dealer = zlink_socket (ctx, ZLINK_SOCKET_DEALER);
    void *router = zlink_socket (ctx, ZLINK_SOCKET_ROUTER);
    TEST_ASSERT_NOT_NULL (dealer);
    TEST_ASSERT_NOT_NULL (router);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (dealer, "bridge-timeout-dealer", 21));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_bind (router, "inproc://spot-route-bridge-timeout"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (dealer, "inproc://spot-route-bridge-timeout"));
    std::this_thread::sleep_for (std::chrono::milliseconds (20));

    zlink_spot_route_bridge_options_t options;
    memset (&options, 0, sizeof (options));
    options.struct_size = sizeof (options);
    options.default_request_timeout_ms = 20;

    void *bridge = zlink_spot_route_bridge_new (ctx, node, &options);
    TEST_ASSERT_NOT_NULL (bridge);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_route_bridge_attach_dealer_channel (bridge, "client", dealer, NULL));

    zlink_routing_id_t target_spot_rid;
    memset (&target_spot_rid, 0, sizeof (target_spot_rid));
    target_spot_rid.size = 11;
    memcpy (target_spot_rid.data, "target-spot", target_spot_rid.size);

    reply_probe_t reply_probe;
    zlink_msg_t payload;
    init_text_part (&payload, "timeout");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_route_bridge_request (
      bridge, "client", &target_spot_rid, &payload, 1, &capture_reply, &reply_probe,
      ZLINK_SEND_FLAGS_NONE, 0));

    TEST_ASSERT_TRUE (wait_for_reply (&reply_probe, dealer));
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_TIMED_OUT, reply_probe.result);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_route_bridge_close (bridge));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (dealer));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (router));
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
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_route_bridge_set_target_node (bridge, "mesh", &target_node_rid));

    zlink_routing_id_t target_spot_rid;
    memset (&target_spot_rid, 0, sizeof (target_spot_rid));
    target_spot_rid.size = 11;
    memcpy (target_spot_rid.data, "target-spot", target_spot_rid.size);

    zlink_msg_t payload;
    init_text_part (&payload, "router-egress");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_route_bridge_send (
      bridge, "mesh", &target_spot_rid, &payload, 1, ZLINK_SEND_FLAGS_NONE));

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
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_route_bridge_set_target_node (bridge, "mesh", &target_node_rid));

    zlink_routing_id_t target_spot_rid;
    memset (&target_spot_rid, 0, sizeof (target_spot_rid));
    target_spot_rid.size = 11;
    memcpy (target_spot_rid.data, "target-spot", target_spot_rid.size);

    reply_probe_t reply_probe;
    zlink_msg_t payload;
    init_text_part (&payload, "router-question");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_route_bridge_request (
      bridge, "mesh", &target_spot_rid, &payload, 1, &capture_reply, &reply_probe,
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

void test_handle_received_rejects_malformed_and_request_relay ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_set (ctx, ZLINK_MAX_SOCKETS, 128));

    void *node = zlink_spot_node_new (ctx, NULL);
    TEST_ASSERT_NOT_NULL (node);
    void *dealer = zlink_socket (ctx, ZLINK_SOCKET_DEALER);
    void *router = zlink_socket (ctx, ZLINK_SOCKET_ROUTER);
    TEST_ASSERT_NOT_NULL (dealer);
    TEST_ASSERT_NOT_NULL (router);

    void *bridge = zlink_spot_route_bridge_new (ctx, node, NULL);
    TEST_ASSERT_NOT_NULL (bridge);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_route_bridge_attach_dealer_channel (bridge, "client", dealer, NULL));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_route_bridge_attach_router_channel (bridge, "mesh", router, NULL));

    zlink_msg_t malformed[2];
    init_text_part (&malformed[0], "__zlink.routed_spot.egress.send");
    init_text_part (&malformed[1], "missing-target-payload");
    bool handled = false;
    TEST_ASSERT_EQUAL_INT (
      -1, zlink_spot_route_bridge_handle_dealer_received (bridge, "client", malformed, 2,
                                                          &handled));
    TEST_ASSERT_TRUE (handled);
    TEST_ASSERT_EQUAL_INT (EPROTO, zlink_errno ());
    zlink_spot_route_bridge_summary_t summary;
    memset (&summary, 0, sizeof (summary));
    summary.struct_size = sizeof (summary);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_route_bridge_summary (bridge, &summary));
    TEST_ASSERT_EQUAL_UINT64 (1, summary.malformed_inbound_count);

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
      -1, zlink_spot_route_bridge_handle_router_received (bridge, "mesh", NULL, request_parts, 3,
                                                          &handled));
    TEST_ASSERT_TRUE (handled);
    TEST_ASSERT_EQUAL_INT (ENOTSUP, zlink_errno ());
    memset (&summary, 0, sizeof (summary));
    summary.struct_size = sizeof (summary);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_route_bridge_summary (bridge, &summary));
    TEST_ASSERT_EQUAL_UINT64 (1, summary.rejected_inbound_count);

    zlink_msg_t app_part;
    init_text_part (&app_part, "application");
    handled = true;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_route_bridge_handle_router_received (bridge, "mesh", NULL, &app_part, 1,
                                                      &handled));
    TEST_ASSERT_FALSE (handled);
    zlink_msg_close (&app_part);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_route_bridge_close (bridge));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (dealer));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (router));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_handle_dealer_received_delivers_send_relay_to_local_spot ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_set (ctx, ZLINK_MAX_SOCKETS, 128));

    void *node = zlink_spot_node_new (ctx, NULL);
    TEST_ASSERT_NOT_NULL (node);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (node, "node-bridge", strlen ("node-bridge")));

    void *target_spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (target_spot);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_routing_id (target_spot, "spot-bridge", strlen ("spot-bridge")));

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

    void *dealer = zlink_socket (ctx, ZLINK_SOCKET_DEALER);
    TEST_ASSERT_NOT_NULL (dealer);
    void *bridge = zlink_spot_route_bridge_new (ctx, node, NULL);
    TEST_ASSERT_NOT_NULL (bridge);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_route_bridge_attach_dealer_channel (bridge, "client", dealer, NULL));

    zlink_msg_t relay_parts[3];
    init_text_part (&relay_parts[0], "__zlink.routed_spot.egress.send");
    init_buffer_part (&relay_parts[1], target_spot_rid.data, target_spot_rid.size);
    init_text_part (&relay_parts[2], "delivered");

    bool handled = false;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_route_bridge_handle_dealer_received (bridge, "client", relay_parts, 3, &handled));
    TEST_ASSERT_TRUE (handled);

    const zlink_routing_id_t *source_node_rid = NULL;
    const zlink_routing_id_t *source_spot_rid = NULL;
    uint64_t request_seq = 1;
    zlink_msg_t received;
    zlink_msg_init (&received);
    zlink_part_flag_t has_more = ZLINK_PART_MORE;
    TEST_ASSERT_EQUAL_INT (ZLINK_RECV_OK,
                           zlink_spot_recv_part (target_spot, &source_node_rid,
                                                 &source_spot_rid, &request_seq, &received,
                                                 &has_more, ZLINK_RECV_FLAGS_DONTWAIT));
    TEST_ASSERT_EQUAL_UINT64 (0, request_seq);
    TEST_ASSERT_EQUAL_INT (ZLINK_PART_FINAL, has_more);
    TEST_ASSERT_EQUAL_INT (strlen ("delivered"), zlink_msg_size (&received));
    TEST_ASSERT_EQUAL_MEMORY ("delivered", zlink_msg_data (&received), strlen ("delivered"));
    zlink_msg_close (&received);

    zlink_msg_t app_part;
    init_text_part (&app_part, "application");
    handled = true;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_route_bridge_handle_dealer_received (bridge, "client", &app_part, 1, &handled));
    TEST_ASSERT_FALSE (handled);
    zlink_msg_close (&app_part);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_route_bridge_close (bridge));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (dealer));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&target_spot));
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
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_route_bridge_handle_router_received_with_metadata (
      bridge, "mesh", &peer_rid, channel_request_seq, request_parts, 3, &handled));
    TEST_ASSERT_TRUE (handled);
    zlink_spot_route_bridge_summary_t summary;
    memset (&summary, 0, sizeof (summary));
    summary.struct_size = sizeof (summary);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_route_bridge_summary (bridge, &summary));
    TEST_ASSERT_EQUAL_UINT64 (1, summary.pending_request_count);

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
    TEST_ASSERT_EQUAL_INT (strlen ("bridge-router-local"), source_node_rid->size);
    TEST_ASSERT_EQUAL_MEMORY ("bridge-router-local", source_node_rid->data,
                              strlen ("bridge-router-local"));
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
    memset (&summary, 0, sizeof (summary));
    summary.struct_size = sizeof (summary);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_route_bridge_summary (bridge, &summary));
    TEST_ASSERT_EQUAL_UINT64 (0, summary.pending_request_count);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_route_bridge_close (bridge));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (bridge_router));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (peer_router));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&target_spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_handle_dealer_received_request_replies_to_channel_peer ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_set (ctx, ZLINK_MAX_SOCKETS, 128));

    void *node = zlink_spot_node_new (ctx, NULL);
    TEST_ASSERT_NOT_NULL (node);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_routing_id (node, "node-dealer-request", strlen ("node-dealer-request")));

    void *target_spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (target_spot);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_routing_id (target_spot, "spot-dealer-request",
                            strlen ("spot-dealer-request")));

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

    void *dealer = zlink_socket (ctx, ZLINK_SOCKET_DEALER);
    void *router = zlink_socket (ctx, ZLINK_SOCKET_ROUTER);
    TEST_ASSERT_NOT_NULL (dealer);
    TEST_ASSERT_NOT_NULL (router);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (dealer, "bridge-dealer-local", 19));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (router, "bridge-dealer-peer", 18));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_bind (router, "inproc://spot-route-bridge-dealer-ingress"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (dealer, "inproc://spot-route-bridge-dealer-ingress"));
    std::this_thread::sleep_for (std::chrono::milliseconds (50));

    void *bridge = zlink_spot_route_bridge_new (ctx, node, NULL);
    TEST_ASSERT_NOT_NULL (bridge);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_route_bridge_attach_dealer_channel (bridge, "client", dealer, NULL));

    zlink_routing_id_t dealer_rid;
    memset (&dealer_rid, 0, sizeof (dealer_rid));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_routing_id (dealer, &dealer_rid));

    reply_probe_t peer_reply;
    zlink_msg_t part0;
    zlink_msg_t part1;
    zlink_msg_t part2;
    init_text_part (&part0, "__zlink.routed_spot.egress.request");
    init_buffer_part (&part1, target_spot_rid.data, target_spot_rid.size);
    init_text_part (&part2, "dealer-question");
    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_OK,
      zlink_router_request_part (router, &dealer_rid, &part0, ZLINK_SEND_FLAGS_NONE,
                                 ZLINK_PART_MORE, 1000, NULL, NULL));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_OK,
      zlink_router_request_part (router, &dealer_rid, &part1, ZLINK_SEND_FLAGS_NONE,
                                 ZLINK_PART_MORE, 1000, NULL, NULL));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_OK,
      zlink_router_request_part (router, &dealer_rid, &part2, ZLINK_SEND_FLAGS_NONE,
                                 ZLINK_PART_FINAL, 1000, &capture_reply, &peer_reply));

    zlink_msg_t request_parts[3];
    uint8_t message_type = ZLINK_DEALER_MESSAGE_RAW;
    uint64_t dealer_request_seq = 0;
    zlink_part_flag_t dealer_more = ZLINK_PART_FINAL;
    for (int i = 0; i < 3; ++i)
        zlink_msg_init (&request_parts[i]);
    TEST_ASSERT_EQUAL_INT (ZLINK_RECV_OK,
                           zlink_dealer_recv_part (dealer, &message_type, &dealer_request_seq,
                                                   &request_parts[0], &dealer_more,
                                                   ZLINK_RECV_FLAGS_NONE));
    TEST_ASSERT_EQUAL_INT (ZLINK_DEALER_MESSAGE_REQUEST, message_type);
    TEST_ASSERT_NOT_EQUAL (0, dealer_request_seq);
    TEST_ASSERT_EQUAL_INT (ZLINK_PART_MORE, dealer_more);
    TEST_ASSERT_EQUAL_INT (ZLINK_RECV_OK,
                           zlink_dealer_recv_part (dealer, &message_type, &dealer_request_seq,
                                                   &request_parts[1], &dealer_more,
                                                   ZLINK_RECV_FLAGS_NONE));
    TEST_ASSERT_EQUAL_INT (ZLINK_PART_MORE, dealer_more);
    TEST_ASSERT_EQUAL_INT (ZLINK_RECV_OK,
                           zlink_dealer_recv_part (dealer, &message_type, &dealer_request_seq,
                                                   &request_parts[2], &dealer_more,
                                                   ZLINK_RECV_FLAGS_NONE));
    TEST_ASSERT_EQUAL_INT (ZLINK_PART_FINAL, dealer_more);

    bool handled = false;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_route_bridge_handle_dealer_received_with_metadata (
      bridge, "client", message_type, dealer_request_seq, request_parts, 3, &handled));
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
    TEST_ASSERT_EQUAL_INT (strlen ("bridge-dealer-local"), source_node_rid->size);
    TEST_ASSERT_EQUAL_MEMORY ("bridge-dealer-local", source_node_rid->data,
                              strlen ("bridge-dealer-local"));
    TEST_ASSERT_EQUAL_INT (strlen ("dealer-question"), zlink_msg_size (&received));
    TEST_ASSERT_EQUAL_MEMORY ("dealer-question", zlink_msg_data (&received),
                              strlen ("dealer-question"));
    zlink_msg_close (&received);

    zlink_msg_t reply;
    init_text_part (&reply, "dealer-answer");
    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_OK,
      zlink_spot_reply_router_part (target_spot, source_node_rid, local_request_seq, &reply,
                                    ZLINK_PART_FINAL));
    for (int attempt = 0; attempt < 100 && !peer_reply.done; ++attempt) {
        (void) zlink_spot_route_bridge_drain (bridge);
        (void) drain_completion_via_poller (router);
        {
            std::unique_lock<std::mutex> lock (peer_reply.mutex);
            if (peer_reply.cv.wait_for (lock, std::chrono::milliseconds (10),
                                        [&peer_reply] () { return peer_reply.done; }))
                break;
        }
    }
    TEST_ASSERT_TRUE (peer_reply.done);
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK, peer_reply.result);
    TEST_ASSERT_EQUAL_INT (1, peer_reply.part_count);
    TEST_ASSERT_EQUAL_STRING ("dealer-answer", peer_reply.payload.c_str ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_route_bridge_close (bridge));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (dealer));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (router));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&target_spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}
}

int main ()
{
    UNITY_BEGIN ();
    RUN_TEST (test_bridge_accepts_borrowed_dealer_and_router_endpoints);
    RUN_TEST (test_bridge_rejects_socket_kind_mismatch);
    RUN_TEST (test_legacy_spot_attach_apis_return_migration_error);
    RUN_TEST (test_bridge_enforces_spot_route_capability);
    RUN_TEST (test_spot_node_publisher_publish_uses_local_topic_plane);
    RUN_TEST (test_bridge_send_dealer_emits_relay_packet);
    RUN_TEST (test_bridge_request_dealer_uses_socket_request_reply);
    RUN_TEST (test_bridge_request_uses_default_timeout_when_call_timeout_is_zero);
    RUN_TEST (test_bridge_send_router_uses_target_node_rid);
    RUN_TEST (test_bridge_request_router_uses_target_node_rid);
    RUN_TEST (test_handle_received_rejects_malformed_and_request_relay);
    RUN_TEST (test_handle_dealer_received_delivers_send_relay_to_local_spot);
    RUN_TEST (test_handle_router_received_request_replies_to_channel_peer);
    RUN_TEST (test_handle_dealer_received_request_replies_to_channel_peer);
    return UNITY_END ();
}
