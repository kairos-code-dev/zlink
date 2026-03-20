/* SPDX-License-Identifier: MPL-2.0 */

#include "../../testutil.hpp"

#include <unity.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <string.h>
#include <thread>
#include <vector>

namespace {

static void assert_success_errno (int rc_, const char *expr_, int line_)
{
    if (rc_ == 0)
        return;
    char message[128];
    snprintf (message, sizeof (message), "%s failed: errno=%d", expr_,
              zlink_errno ());
    UnityFail (message, line_);
}

#define TEST_ASSERT_SUCCESS_ERRNO(expr)                                       \
    assert_success_errno ((expr), #expr, __LINE__)

struct service_monitor_probe_t
{
    service_monitor_probe_t () : event_count (0) {}

    std::mutex mutex;
    std::condition_variable cv;
    std::vector<zlink_service_event_t> events;
    uint64_t event_count;
};

struct subscribe_probe_t
{
    subscribe_probe_t () : calls (0) {}

    std::mutex mutex;
    std::condition_variable cv;
    int calls;
    std::string topic;
    std::string payload;
};

struct send_ready_probe_t
{
    send_ready_probe_t () : calls (0) {}

    std::mutex mutex;
    std::condition_variable cv;
    int calls;
};

static std::atomic<int> g_port_seed (22618);

static int next_port_seed ()
{
    return g_port_seed.fetch_add (8);
}

static void monitor_probe_handler (const zlink_service_event_t *event_,
                                   void *userdata_)
{
    service_monitor_probe_t *probe =
      static_cast<service_monitor_probe_t *> (userdata_);
    if (!probe || !event_)
        return;

    {
        std::lock_guard<std::mutex> lock (probe->mutex);
        probe->events.push_back (*event_);
        ++probe->event_count;
    }
    probe->cv.notify_all ();
}

static bool pop_event_locked (service_monitor_probe_t *probe_,
                              uint32_t expected_event_type_,
                              const char *endpoint_prefix_)
{
    for (std::vector<zlink_service_event_t>::iterator it =
           probe_->events.begin ();
         it != probe_->events.end (); ++it) {
        if (it->event_type != expected_event_type_)
            continue;
        if (endpoint_prefix_ && endpoint_prefix_[0] != '\0') {
            if ((it->detail_flags & ZLINK_EVENT_DETAIL_ENDPOINT) == 0)
                continue;
            if (strncmp (it->endpoint, endpoint_prefix_,
                         strlen (endpoint_prefix_))
                != 0) {
                continue;
            }
        }
        probe_->events.erase (it);
        return true;
    }
    return false;
}

static bool wait_for_event (service_monitor_probe_t *probe_,
                            uint32_t expected_event_type_,
                            const char *endpoint_prefix_,
                            int timeout_ms_)
{
    std::unique_lock<std::mutex> lock (probe_->mutex);
    if (pop_event_locked (probe_, expected_event_type_, endpoint_prefix_))
        return true;

    return probe_->cv.wait_for (
      lock, std::chrono::milliseconds (timeout_ms_),
      [probe_, expected_event_type_, endpoint_prefix_]() {
          return pop_event_locked (probe_, expected_event_type_,
                                   endpoint_prefix_);
      });
}

static bool wait_for_subscription_ready (void *sub_node_,
                                         const char *endpoint_,
                                         const char *topic_)
{
    service_monitor_probe_t probe;
    zlink_service_monitor_open_options_t opts;
    memset (&opts, 0, sizeof (opts));
    opts.events = ZLINK_SPOT_SUB_FILTER_APPLIED
                  | ZLINK_SPOT_SUB_SUBSCRIPTION_READY
                  | ZLINK_MONITOR_EVENT_ERROR;
    void *monitor = zlink_service_monitor_open (sub_node_, &opts);
    if (!monitor)
        return false;
    if (zlink_service_monitor_handler (monitor, &monitor_probe_handler, &probe)
        != 0) {
        zlink_monitor_close (&monitor);
        return false;
    }

    const bool ok =
      zlink_set_subscription (sub_node_, topic_) == 0
      && wait_for_event (&probe, ZLINK_SPOT_SUB_FILTER_APPLIED, NULL, 3000)
      && wait_for_event (&probe, ZLINK_SPOT_SUB_SUBSCRIPTION_READY, endpoint_,
                         3000);
    zlink_monitor_close (&monitor);
    return ok;
}

static bool wait_for_existing_subscription_ready (void *sub_node_,
                                                  const char *endpoint_)
{
    service_monitor_probe_t probe;
    zlink_service_monitor_open_options_t opts;
    memset (&opts, 0, sizeof (opts));
    opts.events = ZLINK_SPOT_SUB_SUBSCRIPTION_READY | ZLINK_MONITOR_EVENT_ERROR;
    void *monitor = zlink_service_monitor_open (sub_node_, &opts);
    if (!monitor)
        return false;
    if (zlink_service_monitor_handler (monitor, &monitor_probe_handler, &probe)
        != 0) {
        zlink_monitor_close (&monitor);
        return false;
    }

    const bool ok = wait_for_event (&probe, ZLINK_SPOT_SUB_SUBSCRIPTION_READY,
                                    endpoint_, 3000);
    zlink_monitor_close (&monitor);
    return ok;
}

static bool wait_for_pub_ready (void *pub_node_)
{
    service_monitor_probe_t probe;
    zlink_service_monitor_open_options_t opts;
    memset (&opts, 0, sizeof (opts));
    opts.events = ZLINK_MONITOR_EVENT_READY | ZLINK_MONITOR_EVENT_PEER_UP
                  | ZLINK_MONITOR_EVENT_ERROR
                  | ZLINK_SPOT_PUB_FIRST_DELIVERY_READY_CHANGED;
    void *monitor = zlink_service_monitor_open (pub_node_, &opts);
    if (!monitor)
        return false;
    if (zlink_service_monitor_handler (monitor, &monitor_probe_handler, &probe)
        != 0) {
        zlink_monitor_close (&monitor);
        return false;
    }

    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now () + std::chrono::milliseconds (3000);
    while (std::chrono::steady_clock::now () < deadline) {
        zlink_monitor_snapshot_t snapshot;
        if (zlink_monitor_snapshot (monitor, &snapshot) == 0
            && (snapshot.state_flags & ZLINK_MONITOR_STATE_SEND_READY) != 0) {
            zlink_monitor_close (&monitor);
            return true;
        }
        (void) wait_for_event (&probe, ZLINK_MONITOR_EVENT_READY, NULL, 200);
    }

    zlink_monitor_close (&monitor);
    return false;
}

static void subscribe_probe_handler (const zlink_routing_id_t *,
                                     const char *topic_,
                                     size_t topic_len_,
                                     zlink_msg_t *parts_,
                                     size_t part_count_,
                                     void *userdata_)
{
    subscribe_probe_t *probe = static_cast<subscribe_probe_t *> (userdata_);
    if (!probe) {
        for (size_t i = 0; i < part_count_; ++i)
            zlink_msg_close (&parts_[i]);
        return;
    }

    {
        std::lock_guard<std::mutex> lock (probe->mutex);
        ++probe->calls;
        probe->topic.assign (topic_ ? topic_ : "", topic_len_);
        probe->payload.clear ();
        if (part_count_ > 0) {
            probe->payload.assign (
              static_cast<const char *> (zlink_msg_data (&parts_[0])),
              zlink_msg_size (&parts_[0]));
        }
    }
    for (size_t i = 0; i < part_count_; ++i)
        zlink_msg_close (&parts_[i]);
    probe->cv.notify_all ();
}

static void send_ready_probe_handler (void *, void *userdata_)
{
    send_ready_probe_t *probe = static_cast<send_ready_probe_t *> (userdata_);
    if (!probe)
        return;

    {
        std::lock_guard<std::mutex> lock (probe->mutex);
        ++probe->calls;
    }
    probe->cv.notify_all ();
}

static void set_linger_zero (void *handle_)
{
    const int linger = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_option (handle_, ZLINK_OPT_LINGER, &linger, sizeof (linger)));
}

static void *create_node (void *ctx_, const char *service_name_)
{
    void *node = zlink_spot_node_new (ctx_, service_name_);
    TEST_ASSERT_NOT_NULL (node);
    set_linger_zero (node);
    return node;
}

static int bind_node (void *node_, int *port_seed_, char *endpoint_out_)
{
    for (int i = 0; i < 32; ++i) {
        snprintf (endpoint_out_, MAX_SOCKET_STRING, "tcp://127.0.0.1:%d",
                  test_port ((*port_seed_)++));
        if (zlink_spot_node_bind (node_, endpoint_out_) == 0)
            return 0;
        if (zlink_errno () != EADDRINUSE)
            return -1;
    }
    errno = EADDRINUSE;
    return -1;
}

static int publish_text (void *subject_,
                         const char *topic_,
                         const char *payload_)
{
    zlink_msg_t part;
    const size_t size = payload_ ? strlen (payload_) : 0;
    if (zlink_msg_init_size (&part, size) != 0)
        return -1;
    if (size > 0)
        memcpy (zlink_msg_data (&part), payload_, size);

    const int rc = zlink_publish (subject_, topic_, &part, 1, 0);
    if (rc != 0) {
        const int err = zlink_errno ();
        zlink_msg_close (&part);
        errno = err;
    }
    return rc;
}

static bool wait_for_subscribe_payload (void *subject_,
                                        const char *expected_topic_,
                                        const char *expected_payload_,
                                        int timeout_ms_)
{
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_option (subject_, ZLINK_OPT_RCVTIMEO, &timeout_ms_,
                        sizeof (timeout_ms_)));

    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    char topic[256];
    size_t topic_len = sizeof (topic);
    if (zlink_subscribe (subject_, NULL, &parts, &part_count, topic,
                         &topic_len, 0)
        != 0) {
        return false;
    }

    const std::string got_topic (topic, topic_len);
    std::string got_payload;
    if (part_count > 0) {
        got_payload.assign (
          static_cast<const char *> (zlink_msg_data (&parts[0])),
          zlink_msg_size (&parts[0]));
    }
    for (size_t i = 0; i < part_count; ++i)
        zlink_msg_close (&parts[i]);
    free (parts);
    return got_topic == expected_topic_ && got_payload == expected_payload_;
}

static bool wait_for_callback_payload (subscribe_probe_t *probe_,
                                       const char *expected_topic_,
                                       const char *expected_payload_,
                                       int timeout_ms_)
{
    std::unique_lock<std::mutex> lock (probe_->mutex);
    return probe_->cv.wait_for (
      lock, std::chrono::milliseconds (timeout_ms_),
      [probe_, expected_topic_, expected_payload_]() {
          return probe_->calls > 0 && probe_->topic == expected_topic_
                 && probe_->payload == expected_payload_;
      });
}

static bool wait_for_send_ready (send_ready_probe_t *probe_, int timeout_ms_)
{
    std::unique_lock<std::mutex> lock (probe_->mutex);
    return probe_->cv.wait_for (
      lock, std::chrono::milliseconds (timeout_ms_),
      [probe_]() { return probe_->calls > 0; });
}

} // namespace

void setUp ()
{
}

void tearDown ()
{
}

static void test_spot_pub_sub_options_and_routing_ids ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *pub_node = create_node (ctx, "spot-options");
    void *sub_node = create_node (ctx, "spot-options");

    const char pub_rid[] = "pub-node";
    const char sub_rid[] = "sub-node";
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_routing_id (pub_node, pub_rid, sizeof (pub_rid) - 1));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_routing_id (sub_node, sub_rid, sizeof (sub_rid) - 1));

    zlink_routing_id_t got_pub;
    zlink_routing_id_t got_sub;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_routing_id (pub_node, &got_pub));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_routing_id (sub_node, &got_sub));
    TEST_ASSERT_EQUAL_UINT32 (sizeof (pub_rid) - 1, got_pub.size);
    TEST_ASSERT_EQUAL_UINT32 (sizeof (sub_rid) - 1, got_sub.size);
    TEST_ASSERT_EQUAL_MEMORY (pub_rid, got_pub.data, got_pub.size);
    TEST_ASSERT_EQUAL_MEMORY (sub_rid, got_sub.data, got_sub.size);

    const int rcvhwm = 64;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_option (sub_node, ZLINK_OPT_RCVHWM, &rcvhwm, sizeof (rcvhwm)));

    char endpoint[MAX_SOCKET_STRING];
    int port_seed = next_port_seed ();
    TEST_ASSERT_SUCCESS_ERRNO (bind_node (pub_node, &port_seed, endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_connect_peer (sub_node, endpoint));

    TEST_ASSERT_TRUE (
      wait_for_subscription_ready (sub_node, endpoint, "topic.alpha"));

    char filter[64];
    size_t filter_len = sizeof (filter);
    int is_pattern = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_subscription_at (sub_node, 0, filter, &filter_len, &is_pattern));
    TEST_ASSERT_EQUAL_UINT (strlen ("topic.alpha"), filter_len);
    TEST_ASSERT_EQUAL_MEMORY ("topic.alpha", filter, filter_len);
    TEST_ASSERT_EQUAL_INT (0, is_pattern);

    TEST_ASSERT_TRUE (wait_for_pub_ready (pub_node));
    TEST_ASSERT_SUCCESS_ERRNO (
      publish_text (pub_node, "topic.alpha", "payload"));
    TEST_ASSERT_TRUE (wait_for_subscribe_payload (
      sub_node, "topic.alpha", "payload", 3000));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&sub_node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&pub_node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

static void test_spot_monitors_and_monitor_poller ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *pub_node = create_node (ctx, "spot-monitor");
    void *sub_node = create_node (ctx, "spot-monitor");

    char endpoint[MAX_SOCKET_STRING];
    int port_seed = next_port_seed ();
    TEST_ASSERT_SUCCESS_ERRNO (bind_node (pub_node, &port_seed, endpoint));

    service_monitor_probe_t probe;
    zlink_service_monitor_open_options_t opts;
    memset (&opts, 0, sizeof (opts));
    opts.events = ZLINK_SPOT_SUB_FILTER_APPLIED
                  | ZLINK_SPOT_SUB_SUBSCRIPTION_READY
                  | ZLINK_MONITOR_EVENT_READY | ZLINK_MONITOR_EVENT_ERROR;
    void *monitor = zlink_service_monitor_open (sub_node, &opts);
    TEST_ASSERT_NOT_NULL (monitor);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_service_monitor_handler (monitor, &monitor_probe_handler, &probe));

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_connect_peer (sub_node, endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_subscription (sub_node, "topic.monitor"));

    TEST_ASSERT_TRUE (wait_for_event (
      &probe, ZLINK_SPOT_SUB_FILTER_APPLIED, NULL, 3000));
    TEST_ASSERT_TRUE (wait_for_event (
      &probe, ZLINK_SPOT_SUB_SUBSCRIPTION_READY, endpoint, 3000));

    zlink_monitor_snapshot_t snapshot;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_monitor_snapshot (monitor, &snapshot));
    TEST_ASSERT_TRUE ((snapshot.state_flags & ZLINK_MONITOR_STATE_READY) != 0);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_monitor_close (&monitor));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&sub_node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&pub_node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

static void test_spot_late_connect_replays_existing_subscription ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *pub_node = create_node (ctx, "spot-replay");
    void *sub_node = create_node (ctx, "spot-replay");

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_subscription (sub_node, "topic.replay"));

    char endpoint[MAX_SOCKET_STRING];
    int port_seed = next_port_seed ();
    TEST_ASSERT_SUCCESS_ERRNO (bind_node (pub_node, &port_seed, endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_connect_peer (sub_node, endpoint));
    TEST_ASSERT_TRUE (
      wait_for_existing_subscription_ready (sub_node, endpoint));

    TEST_ASSERT_TRUE (wait_for_pub_ready (pub_node));
    TEST_ASSERT_SUCCESS_ERRNO (
      publish_text (pub_node, "topic.replay", "replayed"));
    TEST_ASSERT_TRUE (wait_for_subscribe_payload (
      sub_node, "topic.replay", "replayed", 3000));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&sub_node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&pub_node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

static void test_spot_callback_model_receive_regression ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *pub_node = create_node (ctx, "spot-callback");
    void *sub_node = zlink_spot_node_new (ctx, "spot-callback");
    TEST_ASSERT_NOT_NULL (sub_node);
    set_linger_zero (sub_node);

    subscribe_probe_t probe;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_subscribe_handler (sub_node, &subscribe_probe_handler, &probe));

    char endpoint[MAX_SOCKET_STRING];
    int port_seed = next_port_seed ();
    TEST_ASSERT_SUCCESS_ERRNO (bind_node (pub_node, &port_seed, endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_connect_peer (sub_node, endpoint));
    TEST_ASSERT_TRUE (
      wait_for_subscription_ready (sub_node, endpoint, "topic.callback"));

    TEST_ASSERT_TRUE (wait_for_pub_ready (pub_node));
    TEST_ASSERT_SUCCESS_ERRNO (
      publish_text (pub_node, "topic.callback", "callback"));
    TEST_ASSERT_TRUE (wait_for_callback_payload (
      &probe, "topic.callback", "callback", 3000));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&sub_node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&pub_node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

static void test_spot_recv_model_receive_regression ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *pub_node = create_node (ctx, "spot-recv");
    void *sub_node = create_node (ctx, "spot-recv");

    char endpoint[MAX_SOCKET_STRING];
    int port_seed = next_port_seed ();
    TEST_ASSERT_SUCCESS_ERRNO (bind_node (pub_node, &port_seed, endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_connect_peer (sub_node, endpoint));
    TEST_ASSERT_TRUE (
      wait_for_subscription_ready (sub_node, endpoint, "topic.recv"));

    TEST_ASSERT_TRUE (wait_for_pub_ready (pub_node));
    TEST_ASSERT_SUCCESS_ERRNO (
      publish_text (pub_node, "topic.recv", "recv"));
    TEST_ASSERT_TRUE (wait_for_subscribe_payload (
      sub_node, "topic.recv", "recv", 3000));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&sub_node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&pub_node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

static void test_spot_node_send_ready_handler_isolated_by_service ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *pub_node = create_node (ctx, "spot-ready");
    void *sub_node = create_node (ctx, "spot-ready");

    subscribe_probe_t callback_mode_probe;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_subscribe_handler (pub_node, &subscribe_probe_handler,
                               &callback_mode_probe));

    send_ready_probe_t probe;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_send_ready_handler (pub_node, &send_ready_probe_handler, &probe));

    char endpoint[MAX_SOCKET_STRING];
    int port_seed = next_port_seed ();
    TEST_ASSERT_SUCCESS_ERRNO (bind_node (pub_node, &port_seed, endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (
      publish_text (pub_node, "__warmup__", "ready"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_connect_peer (sub_node, endpoint));

    TEST_ASSERT_TRUE (wait_for_pub_ready (pub_node));
    TEST_ASSERT_TRUE (wait_for_send_ready (&probe, 3000));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&sub_node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&pub_node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

static void test_spot_unified_spot_basic ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *pub_spot = zlink_spot_new (ctx, "spot-unified");
    void *sub_spot = zlink_spot_new (ctx, "spot-unified");
    TEST_ASSERT_NOT_NULL (pub_spot);
    TEST_ASSERT_NOT_NULL (sub_spot);
    set_linger_zero (pub_spot);
    set_linger_zero (sub_spot);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_subscription (sub_spot, "topic.unified"));

    char filter[64];
    size_t filter_len = sizeof (filter);
    int is_pattern = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_subscription_at (sub_spot, 0, filter, &filter_len, &is_pattern));
    TEST_ASSERT_EQUAL_UINT (strlen ("topic.unified"), filter_len);
    TEST_ASSERT_EQUAL_MEMORY ("topic.unified", filter, filter_len);
    TEST_ASSERT_EQUAL_INT (0, is_pattern);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&sub_spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&pub_spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

static void test_spot_node_snapshot_status_peers_subjects ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *pub_node = create_node (ctx, "spot-snapshot");
    void *sub_node = create_node (ctx, "spot-snapshot");
    TEST_ASSERT_NOT_NULL (pub_node);
    TEST_ASSERT_NOT_NULL (sub_node);

    char endpoint[MAX_SOCKET_STRING];
    int port_seed = next_port_seed ();
    TEST_ASSERT_SUCCESS_ERRNO (bind_node (pub_node, &port_seed, endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_connect_peer (sub_node, endpoint));
    TEST_ASSERT_TRUE (
      wait_for_subscription_ready (sub_node, endpoint, "topic.snapshot"));
    TEST_ASSERT_TRUE (wait_for_pub_ready (pub_node));

    zlink_spot_node_status_t status;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_status_snapshot (sub_node, &status));
    TEST_ASSERT_EQUAL_STRING ("spot-snapshot", status.service_name);
    TEST_ASSERT_TRUE (status.configured_peer_count >= 1);
    TEST_ASSERT_TRUE (status.active_peer_count >= 1);
    TEST_ASSERT_TRUE (status.connected_peer_count >= 1);
    TEST_ASSERT_TRUE (status.subject_count >= 1);
    TEST_ASSERT_TRUE (status.ready_subject_count >= 1);
    TEST_ASSERT_EQUAL_UINT32 (ZLINK_SPOT_NODE_STATE_READY, status.state);

    size_t count = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_peers_snapshot (sub_node, NULL, &count));
    TEST_ASSERT_EQUAL_UINT (1, count);
    std::vector<zlink_spot_node_peer_entry_t> peers (count);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_peers_snapshot (sub_node, &peers[0], &count));
    TEST_ASSERT_EQUAL_UINT (1, count);
    TEST_ASSERT_EQUAL_STRING (endpoint, peers[0].peer_endpoint);
    TEST_ASSERT_EQUAL_UINT32 (ZLINK_SPOT_PEER_SOURCE_MANUAL, peers[0].source);
    TEST_ASSERT_EQUAL_UINT32 (ZLINK_SPOT_PEER_STATE_CONNECTED, peers[0].state);

    zlink_spot_node_subject_filter_t filter;
    memset (&filter, 0, sizeof (filter));
    filter.role = ZLINK_SPOT_ROLE_SUB;
    size_t subject_count = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_subjects_snapshot (sub_node, &filter, NULL, &subject_count));
    TEST_ASSERT_EQUAL_UINT (1, subject_count);
    std::vector<zlink_spot_node_subject_entry_t> subjects (subject_count);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_subjects_snapshot (
      sub_node, &filter, &subjects[0], &subject_count));
    TEST_ASSERT_EQUAL_UINT (1, subject_count);
    TEST_ASSERT_EQUAL_UINT32 (ZLINK_SPOT_ROLE_SUB, subjects[0].role);
    TEST_ASSERT_EQUAL_UINT32 (ZLINK_SERVICE_EVENT_SUBJECT_TOPIC,
                              subjects[0].subject_kind);
    TEST_ASSERT_EQUAL_STRING ("topic.snapshot", subjects[0].subject);
    TEST_ASSERT_TRUE (subjects[0].active_peer_count >= 1);
    TEST_ASSERT_TRUE (subjects[0].ready_peer_count >= 1);

    memset (&filter, 0, sizeof (filter));
    filter.role = ZLINK_SPOT_ROLE_PUB;
    TEST_ASSERT_EQUAL_INT (
      -1, zlink_spot_node_subjects_snapshot (sub_node, &filter, NULL, &subject_count));
    TEST_ASSERT_EQUAL_INT (ENOTSUP, zlink_errno ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&sub_node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&pub_node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

static bool should_run_spot_introspection_test (const char *name_)
{
    const char *selected = getenv ("ZLINK_TEST_CASE");
    return !selected || !*selected || strcmp (selected, name_) == 0;
}

int main (int, char **)
{
    setup_test_environment (300);

    UNITY_BEGIN ();
#define RUN_SPOT_INTROSPECTION_TEST(name)                                      \
    do {                                                                       \
        if (should_run_spot_introspection_test (#name))                        \
            RUN_TEST (name);                                                   \
    } while (0)
    RUN_SPOT_INTROSPECTION_TEST (test_spot_pub_sub_options_and_routing_ids);
    RUN_SPOT_INTROSPECTION_TEST (test_spot_monitors_and_monitor_poller);
    RUN_SPOT_INTROSPECTION_TEST (test_spot_late_connect_replays_existing_subscription);
    RUN_SPOT_INTROSPECTION_TEST (test_spot_callback_model_receive_regression);
    RUN_SPOT_INTROSPECTION_TEST (test_spot_recv_model_receive_regression);
    RUN_SPOT_INTROSPECTION_TEST (test_spot_unified_spot_basic);
    RUN_SPOT_INTROSPECTION_TEST (test_spot_node_snapshot_status_peers_subjects);
#undef RUN_SPOT_INTROSPECTION_TEST
    return UNITY_END ();
}
