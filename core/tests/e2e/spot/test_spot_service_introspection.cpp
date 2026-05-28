/* SPDX-License-Identifier: MPL-2.0 */

#include "../../testutil.hpp"
#include "../../testutil_unity.hpp"
#include "../../testutil_monitoring.hpp"
#include "../../../src/api/service/service_api_internal.hpp"
#include "../../zlink_testing.hpp"
#include "../../../src/runtime/services/spot/runtime/spot_handle.hpp"
#include "../../../src/runtime/services/spot/node/spot_node.hpp"
#include "../../../src/runtime/services/spot/node/spot_node_access.hpp"
#include "../../../src/runtime/services/spot/pubsub/spot_pub.hpp"
#include "../../../src/runtime/services/spot/pubsub/spot_subject_access.hpp"
#include "../../../src/runtime/core/msg.hpp"

#include <unity.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <string>
#include <string.h>
#include <thread>
#include <vector>

namespace {

static std::atomic<int> g_port_seed (22618);
static std::mutex g_default_handle_mutex;
static std::map<void *, spot_handle_t *> g_node_spot_handles;
static const int bind_retry_limit = 256;

static void *node_spot_handle (void *node_);
static void *node_sub_spot_handle (void *node_);

static int next_port_seed ()
{
    return g_port_seed.fetch_add (8);
}

static int bounded_poll_step_ms (
  const std::chrono::steady_clock::time_point &deadline_)
{
    const std::chrono::steady_clock::duration remaining =
      deadline_ - std::chrono::steady_clock::now ();
    const long remaining_ms =
      std::chrono::duration_cast<std::chrono::milliseconds> (remaining).count ();
    if (remaining_ms <= 0)
        return 0;
    return remaining_ms > 5 ? 5 : static_cast<int> (remaining_ms);
}

static bool wait_for_spot_node_subject_ready (void *node_, int timeout_ms_)
{
    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now () + std::chrono::milliseconds (timeout_ms_);

    while (std::chrono::steady_clock::now () < deadline) {
        zlink_spot_node_status_t status;
        if (zlink_spot_node_status (node_, &status) == 0
            && status.subject_count > 0
            && (status.ready_subject_count > 0
                || status.connected_peer_count > 0
                || status.active_peer_count > 0
                || status.configured_peer_count == 0)) {
            return true;
        }

        const int wait_ms = bounded_poll_step_ms (deadline);
        if (wait_ms <= 0)
            break;
        zlink_pollitem_t item = {NULL, 0, 0, 0};
        (void) zlink_poll (&item, 0, wait_ms, NULL);
    }

    return false;
}

static bool wait_for_subscribe_queue_size (spot_handle_t *spot_,
                                           size_t expected_,
                                           int timeout_ms_)
{
    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now () + std::chrono::milliseconds (timeout_ms_);

    while (std::chrono::steady_clock::now () < deadline) {
        if (spot_ && spot_->logical_state
            && spot_->logical_state->subscribe_queue.size () == expected_)
            return true;

        const int wait_ms = bounded_poll_step_ms (deadline);
        if (wait_ms <= 0)
            break;
        zlink_pollitem_t item = {NULL, 0, 0, 0};
        (void) zlink_poll (&item, 0, wait_ms, NULL);
    }

    return spot_ && spot_->logical_state
           && spot_->logical_state->subscribe_queue.size () == expected_;
}

static bool wait_for_subscription_ready (void *sub_node_,
                                         const char *endpoint_,
                                         const char *topic_)
{
    LIBZLINK_UNUSED (endpoint_);
    void *sub_handle = node_sub_spot_handle (sub_node_);
    if (zlink_set_subscription (sub_handle, topic_) != ZLINK_CONFIG_OK)
        return false;
    return wait_for_spot_node_subject_ready (sub_node_, 3000);
}

static void set_linger_zero (void *handle_)
{
    const int linger = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_option (handle_, ZLINK_OPT_LINGER, &linger, sizeof (linger)));
}

static void *node_spot_handle (void *node_)
{
    zlink::spot_node_t *node = zlink::spot_node_access_t::from_handle (node_);
    if (!node)
        return node_;

    std::lock_guard<std::mutex> lock (g_default_handle_mutex);
    std::map<void *, spot_handle_t *>::iterator it =
      g_node_spot_handles.find (node_);
    if (it != g_node_spot_handles.end ())
        return it->second;

    spot_handle_t *spot = new (std::nothrow) spot_handle_t ();
    if (!spot) {
        errno = ENOMEM;
        return NULL;
    }
    spot->node = node;
    spot->logical_state = zlink::spot_node_access_t::entry_spot_state (node);
    if (!spot->logical_state) {
        delete spot;
        return NULL;
    }
    spot->spot_routing_id = spot->logical_state->routing_id;
    register_spot_mode_state (spot);
    g_node_spot_handles[node_] = spot;
    return spot;
}

static void *node_sub_spot_handle (void *node_)
{
    return node_spot_handle (node_);
}

static void destroy_node_spot_handle (void *node_)
{
    std::lock_guard<std::mutex> lock (g_default_handle_mutex);
    std::map<void *, spot_handle_t *>::iterator it =
      g_node_spot_handles.find (node_);
    if (it == g_node_spot_handles.end ())
        return;

    zlink::destroy_registered_spot_handle_for_testing (it->second);
    g_node_spot_handles.erase (it);
}

static void destroy_node_and_spot_handle (void **node_p_)
{
    if (!node_p_ || !*node_p_)
        return;

    destroy_node_spot_handle (*node_p_);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (node_p_));
}

static void *create_node (void *ctx_, const char *channel_name_)
{
    LIBZLINK_UNUSED (channel_name_);
    void *node = zlink_spot_node_new (ctx_, NULL);
    TEST_ASSERT_NOT_NULL (node);
    set_linger_zero (node);
    return node;
}

static int bind_node (void *node_, int *port_seed_, char *endpoint_out_)
{
    for (int i = 0; i < bind_retry_limit; ++i) {
        snprintf (endpoint_out_, MAX_SOCKET_STRING, "tcp://127.0.0.1:%d",
                  test_port ((*port_seed_)++));
        if (zlink_spot_node_set_pub_bind (node_, endpoint_out_) == 0)
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
    if (!as_spot_handle (subject_))
        subject_ = node_spot_handle (subject_);
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

static int publish_text_with_flags (void *subject_,
                                    const char *topic_,
                                    const char *payload_,
                                    zlink_send_flags_t flags_)
{
    if (!as_spot_handle (subject_))
        subject_ = node_spot_handle (subject_);
    zlink_msg_t part;
    const size_t size = payload_ ? strlen (payload_) : 0;
    if (zlink_msg_init_size (&part, size) != 0)
        return -1;
    if (size > 0)
        memcpy (zlink_msg_data (&part), payload_, size);

    const int rc = zlink_publish (subject_, topic_, &part, 1, flags_);
    if (rc != 0) {
        const int err = zlink_errno ();
        zlink_msg_close (&part);
        errno = err;
    }
    return rc;
}

static void assert_recv_text (void *spot_,
                              const char *expected_topic_,
                              const char *expected_payload_)
{
    zlink_routing_id_t source_rid;
    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    char topic[128] = {0};
    size_t topic_len = sizeof (topic);
    memset (&source_rid, 0, sizeof (source_rid));

    TEST_ASSERT_EQUAL_INT (
      ZLINK_RECV_OK,
      zlink_spot_subscribe (
        spot_, &source_rid, &parts, &part_count, topic, &topic_len,
        static_cast<zlink_recv_flags_t> (ZLINK_DONTWAIT)));
    TEST_ASSERT_EQUAL_UINT (strlen (expected_topic_), topic_len);
    TEST_ASSERT_EQUAL_MEMORY (expected_topic_, topic, topic_len);
    TEST_ASSERT_EQUAL_UINT (1u, static_cast<unsigned int> (part_count));
    TEST_ASSERT_EQUAL_UINT (strlen (expected_payload_),
                            zlink_msg_size (&parts[0]));
    TEST_ASSERT_EQUAL_MEMORY (expected_payload_, zlink_msg_data (&parts[0]),
                              strlen (expected_payload_));
    zlink_multipart_close (parts, part_count);
}

} // namespace

void setUp ()
{
}

void tearDown ()
{
    std::lock_guard<std::mutex> lock (g_default_handle_mutex);
    for (std::map<void *, spot_handle_t *>::iterator it =
           g_node_spot_handles.begin ();
         it != g_node_spot_handles.end (); ++it) {
        zlink::destroy_registered_spot_handle_for_testing (it->second);
    }
    g_node_spot_handles.clear ();
}

static void test_spot_pub_sub_options_and_routing_ids ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *pub_node = create_node (ctx, "spot-options");
    void *sub_node = create_node (ctx, "spot-options");
    void *sub = zlink_spot_new (sub_node);
    TEST_ASSERT_NOT_NULL (sub);
    set_linger_zero (sub);

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
      zlink_set_spot_node_option (sub_node, ZLINK_SPOT_NODE_OPT_PUBSUB_HWM,
                                  &rcvhwm, sizeof (rcvhwm)));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_subscription (sub, "topic.alpha"));
    char filter[64];
    size_t filter_len = sizeof (filter);
    int is_pattern = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_subscription_at (sub, 0, filter, &filter_len, &is_pattern));
    TEST_ASSERT_EQUAL_UINT (strlen ("topic.alpha"), filter_len);
    TEST_ASSERT_EQUAL_MEMORY ("topic.alpha", filter, filter_len);
    TEST_ASSERT_EQUAL_INT (0, is_pattern);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&sub));
    destroy_node_and_spot_handle (&sub_node);
    destroy_node_and_spot_handle (&pub_node);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

static void test_spot_callback_model_receive_regression ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *pub_node = create_node (ctx, "spot-callback");
    void *sub_node = create_node (ctx, "spot-callback");
    void *pub = zlink_spot_new (pub_node);
    TEST_ASSERT_NOT_NULL (pub);
    TEST_ASSERT_NOT_NULL (sub_node);
    void *sub = zlink_spot_new (sub_node);
    TEST_ASSERT_NOT_NULL (sub);
    set_linger_zero (pub);
    set_linger_zero (sub);

    char endpoint[MAX_SOCKET_STRING];
    int port_seed = next_port_seed ();
    TEST_ASSERT_SUCCESS_ERRNO (bind_node (pub_node, &port_seed, endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_connect_peer (sub_node, endpoint));
    TEST_ASSERT_TRUE (
      wait_for_subscription_ready (sub_node, endpoint, "topic.callback"));

    zlink_routing_id_t source_rid;
    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    char topic[64];
    size_t topic_len = sizeof (topic);
    TEST_ASSERT_EQUAL_INT (ZLINK_RECV_NO_DATA,
                           zlink_spot_subscribe (
                             sub, &source_rid, &parts, &part_count,
                             topic, &topic_len,
                             static_cast<zlink_recv_flags_t> (
                               ZLINK_DONTWAIT)));
    TEST_ASSERT_EQUAL_INT (EAGAIN, errno);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&sub));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&pub));
    destroy_node_and_spot_handle (&sub_node);
    destroy_node_and_spot_handle (&pub_node);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

static void test_spot_recv_model_receive_regression ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *pub_node = create_node (ctx, "spot-recv");
    void *sub_node = create_node (ctx, "spot-recv");
    void *pub = zlink_spot_new (pub_node);
    void *sub = zlink_spot_new (sub_node);
    TEST_ASSERT_NOT_NULL (pub);
    TEST_ASSERT_NOT_NULL (sub);
    set_linger_zero (pub);
    set_linger_zero (sub);

    char endpoint[MAX_SOCKET_STRING];
    int port_seed = next_port_seed ();
    TEST_ASSERT_SUCCESS_ERRNO (bind_node (pub_node, &port_seed, endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_connect_peer (sub_node, endpoint));
    TEST_ASSERT_TRUE (
      wait_for_subscription_ready (sub_node, endpoint, "topic.recv"));
    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    char topic[256];
    memset (topic, 0, sizeof (topic));
    size_t topic_len = sizeof (topic);
    errno = 0;
    TEST_ASSERT_EQUAL_INT (
      ZLINK_RECV_NO_DATA,
      zlink_subscribe (sub, NULL, &parts, &part_count, topic, &topic_len,
                       static_cast<zlink_recv_flags_t> (ZLINK_DONTWAIT)));
    TEST_ASSERT_EQUAL_INT (EAGAIN, errno);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&sub));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&pub));
    destroy_node_and_spot_handle (&sub_node);
    destroy_node_and_spot_handle (&pub_node);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

static void test_spot_subscribe_empty_logical_queue_concurrent_no_crash ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *pub_node = create_node (ctx, "spot-empty-concurrent");
    void *sub_node = create_node (ctx, "spot-empty-concurrent");
    void *pub = zlink_spot_new (pub_node);
    TEST_ASSERT_NOT_NULL (pub);
    set_linger_zero (pub);

    const int worker_count = 16;
    const int iterations_per_worker = 1000;
    std::vector<void *> subs;
    subs.reserve (worker_count);
    for (int worker = 0; worker < worker_count; ++worker) {
        void *sub = zlink_spot_new (sub_node);
        TEST_ASSERT_NOT_NULL (sub);
        set_linger_zero (sub);
        TEST_ASSERT_SUCCESS_ERRNO (zlink_set_subscription (sub, "topic.empty"));
        subs.push_back (sub);
    }

    char endpoint[MAX_SOCKET_STRING];
    int port_seed = next_port_seed ();
    TEST_ASSERT_SUCCESS_ERRNO (bind_node (pub_node, &port_seed, endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_connect_peer (sub_node, endpoint));
    TEST_ASSERT_TRUE (wait_for_spot_node_subject_ready (sub_node, 3000));

    std::atomic<bool> start (false);
    std::atomic<int> ready (0);
    std::atomic<int> failures (0);
    std::vector<std::thread> workers;
    workers.reserve (worker_count);

    for (int worker = 0; worker < worker_count; ++worker) {
        void *worker_sub = subs[worker];
        workers.push_back (std::thread ([&, worker_sub]() {
            ready.fetch_add (1);
            while (!start.load ())
                std::this_thread::yield ();

            for (int i = 0; i < iterations_per_worker; ++i) {
                zlink_routing_id_t source_rid;
                zlink_msg_t *parts = NULL;
                size_t part_count = 0;
                char topic[64] = {0};
                size_t topic_len = sizeof (topic);
                errno = 0;
                const int rc = zlink_spot_subscribe (
                  worker_sub, &source_rid, &parts, &part_count, topic, &topic_len,
                  static_cast<zlink_recv_flags_t> (ZLINK_DONTWAIT));
                const int err = zlink_errno ();
                if (rc == ZLINK_RECV_OK) {
                    zlink_multipart_close (parts, part_count);
                    failures.fetch_add (1);
                } else if (rc != ZLINK_RECV_NO_DATA || err != EAGAIN) {
                    failures.fetch_add (1);
                }
            }
        }));
    }

    while (ready.load () != worker_count)
        std::this_thread::yield ();
    start.store (true);

    for (size_t i = 0; i < workers.size (); ++i)
        workers[i].join ();

    TEST_ASSERT_EQUAL_INT (0, failures.load ());

    for (size_t i = 0; i < subs.size (); ++i)
        TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&subs[i]));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&pub));
    destroy_node_and_spot_handle (&sub_node);
    destroy_node_and_spot_handle (&pub_node);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

static void test_spot_subscription_mutation_does_not_race_dispatch_filter ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *pub_node = create_node (ctx, "spot-sub-race");
    void *sub_node = create_node (ctx, "spot-sub-race");
    void *pub = zlink_spot_new (pub_node);
    void *sub = zlink_spot_new (sub_node);
    TEST_ASSERT_NOT_NULL (pub);
    TEST_ASSERT_NOT_NULL (sub);
    set_linger_zero (pub);
    set_linger_zero (sub);

    char endpoint[MAX_SOCKET_STRING];
    int port_seed = next_port_seed ();
    TEST_ASSERT_SUCCESS_ERRNO (bind_node (pub_node, &port_seed, endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_connect_peer (sub_node, endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_subscription (sub, "race.topic.0"));
    TEST_ASSERT_TRUE (wait_for_spot_node_subject_ready (sub_node, 3000));

    std::atomic<bool> stop (false);
    std::atomic<int> failures (0);
    std::thread publisher ([&]() {
        for (int i = 0; i < 4000 && !stop.load (); ++i) {
            char topic[32];
            snprintf (topic, sizeof (topic), "race.topic.%d", i % 16);
            const int rc = publish_text_with_flags (
              pub, topic, "payload",
              static_cast<zlink_send_flags_t> (ZLINK_DONTWAIT));
            if (rc != 0) {
                const int err = zlink_errno ();
                if (err != EAGAIN && err != EINTR)
                    failures.fetch_add (1);
            }
        }
    });

    for (int i = 0; i < 4000; ++i) {
        char topic[32];
        snprintf (topic, sizeof (topic), "race.topic.%d", i % 16);
        if (zlink_set_subscription (sub, topic) != ZLINK_CONFIG_OK)
            failures.fetch_add (1);
        if (zlink_unset_subscription (sub, topic) != ZLINK_CONFIG_OK)
            failures.fetch_add (1);
    }
    stop.store (true);
    publisher.join ();

    TEST_ASSERT_EQUAL_INT (0, failures.load ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&sub));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&pub));
    destroy_node_and_spot_handle (&sub_node);
    destroy_node_and_spot_handle (&pub_node);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

static void test_spot_unified_spot_basic ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *pub_node = zlink_spot_node_new (ctx, NULL);
    void *sub_node = zlink_spot_node_new (ctx, NULL);
    TEST_ASSERT_NOT_NULL (pub_node);
    TEST_ASSERT_NOT_NULL (sub_node);
    void *pub_spot = zlink_spot_new (pub_node);
    void *sub_spot = zlink_spot_new (sub_node);
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
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&sub_node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&pub_node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

static void test_queue_pub_local_fanout_shared_block ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *node = create_node (ctx, "queue-fanout");
    void *pub = zlink_spot_new (node);
    void *sub_a = zlink_spot_new (node);
    void *sub_b = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (pub);
    TEST_ASSERT_NOT_NULL (sub_a);
    TEST_ASSERT_NOT_NULL (sub_b);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_subscription (pub, "queue.topic"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_subscription (sub_a, "queue.topic"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_subscription (sub_b, "queue.topic"));
    TEST_ASSERT_SUCCESS_ERRNO (publish_text (pub, "queue.topic", "payload"));

    spot_handle_t *p = as_spot_handle (pub);
    spot_handle_t *a = as_spot_handle (sub_a);
    spot_handle_t *b = as_spot_handle (sub_b);
    TEST_ASSERT_NOT_NULL (p);
    TEST_ASSERT_NOT_NULL (a);
    TEST_ASSERT_NOT_NULL (b);
    TEST_ASSERT_TRUE (wait_for_subscribe_queue_size (p, 1u, 1000));
    TEST_ASSERT_TRUE (wait_for_subscribe_queue_size (a, 1u, 1000));
    TEST_ASSERT_TRUE (wait_for_subscribe_queue_size (b, 1u, 1000));
    TEST_ASSERT_EQUAL_UINT (1u, p->logical_state->subscribe_queue.size ());
    TEST_ASSERT_EQUAL_UINT (1u, a->logical_state->subscribe_queue.size ());
    TEST_ASSERT_EQUAL_UINT (1u, b->logical_state->subscribe_queue.size ());
    TEST_ASSERT_EQUAL_PTR (p->logical_state->subscribe_queue.front ().get (),
                           a->logical_state->subscribe_queue.front ().get ());
    TEST_ASSERT_EQUAL_PTR (a->logical_state->subscribe_queue.front ().get (),
                           b->logical_state->subscribe_queue.front ().get ());

    zlink_routing_id_t source_rid;
    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    char topic[128] = {0};
    size_t topic_len = sizeof (topic);
    TEST_ASSERT_EQUAL_INT (
      ZLINK_RECV_OK,
      zlink_spot_subscribe (
        sub_a, &source_rid, &parts, &part_count, topic, &topic_len,
        static_cast<zlink_recv_flags_t> (ZLINK_DONTWAIT)));
    TEST_ASSERT_EQUAL_UINT (1u, static_cast<unsigned int> (part_count));
    TEST_ASSERT_EQUAL_UINT (strlen ("payload"), zlink_msg_size (&parts[0]));
    memset (zlink_msg_data (&parts[0]), 'x', zlink_msg_size (&parts[0]));
    zlink_multipart_close (parts, part_count);

    assert_recv_text (pub, "queue.topic", "payload");
    assert_recv_text (sub_b, "queue.topic", "payload");

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&sub_b));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&sub_a));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&pub));
    destroy_node_and_spot_handle (&node);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

static void test_queue_sub_exact_pattern_dedupe ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *node = create_node (ctx, "queue-dedupe");
    void *pub = zlink_spot_new (node);
    void *sub = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (pub);
    TEST_ASSERT_NOT_NULL (sub);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_subscription (sub, "queue.dedupe"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_subscription (sub, "queue.*"));
    TEST_ASSERT_SUCCESS_ERRNO (publish_text (pub, "queue.dedupe", "once"));

    spot_handle_t *sub_handle = as_spot_handle (sub);
    TEST_ASSERT_NOT_NULL (sub_handle);
    TEST_ASSERT_TRUE (wait_for_subscribe_queue_size (sub_handle, 1u, 1000));
    TEST_ASSERT_EQUAL_UINT (1u,
                            sub_handle->logical_state->subscribe_queue.size ());
    assert_recv_text (sub, "queue.dedupe", "once");

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&sub));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&pub));
    destroy_node_and_spot_handle (&node);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

static void test_queue_pub_no_subscriber_success ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *node = create_node (ctx, "queue-empty");
    void *pub = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (pub);

    TEST_ASSERT_SUCCESS_ERRNO (publish_text (pub, "queue.none", "payload"));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&pub));
    destroy_node_and_spot_handle (&node);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

static void test_queue_pub_dead_spot_fails ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *node = create_node (ctx, "queue-shutdown");
    void *pub = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (pub);

    zlink::service_public_api_guard_t *guard =
      zlink::spot_public_api_guard_for_testing (pub);
    TEST_ASSERT_NOT_NULL (guard);
    guard->mark_closing ();
    TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_TERMINATED,
                           publish_text (pub, "queue.shutdown", "payload"));
    guard->cancel_close ();

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&pub));
    destroy_node_and_spot_handle (&node);
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
    void *sub = zlink_spot_new (sub_node);
    TEST_ASSERT_NOT_NULL (sub);
    set_linger_zero (sub);

    char endpoint[MAX_SOCKET_STRING];
    int port_seed = next_port_seed ();
    TEST_ASSERT_SUCCESS_ERRNO (bind_node (pub_node, &port_seed, endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_connect_peer (sub_node, endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_subscription (sub, "topic.snapshot"));
    TEST_ASSERT_TRUE (wait_for_spot_node_subject_ready (sub_node, 3000));
    zlink_spot_node_status_t status;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_status (sub_node, &status));
    TEST_ASSERT_EQUAL_STRING ("", status.channel_name);
    TEST_ASSERT_TRUE (status.configured_peer_count >= 1);
    TEST_ASSERT_TRUE (status.connected_peer_count >= 1
                      || status.active_peer_count >= 1);
    TEST_ASSERT_TRUE (status.subject_count >= 1);
    TEST_ASSERT_TRUE (status.state == ZLINK_SPOT_NODE_STATE_CONNECTING
                      || status.state == ZLINK_SPOT_NODE_STATE_PARTIAL_READY
                      || status.state == ZLINK_SPOT_NODE_STATE_READY);

    size_t count = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_peers (sub_node, NULL, NULL, &count));
    TEST_ASSERT_EQUAL_UINT (1, count);
    std::vector<zlink_spot_node_peer_entry_t> peers (count);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_peers (sub_node, NULL, &peers[0], &count));
    TEST_ASSERT_EQUAL_UINT (1, count);
    TEST_ASSERT_EQUAL_STRING (endpoint, peers[0].peer_endpoint);
    TEST_ASSERT_EQUAL_UINT32 (ZLINK_SPOT_PEER_SOURCE_MANUAL, peers[0].source);
    TEST_ASSERT_TRUE (peers[0].state == ZLINK_SPOT_PEER_STATE_CONNECTING
                      || peers[0].state == ZLINK_SPOT_PEER_STATE_CONNECTED);

    zlink_spot_node_subject_filter_t filter;
    memset (&filter, 0, sizeof (filter));
    filter.role = ZLINK_SPOT_ROLE_SUB;
    size_t subject_count = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_subjects (sub_node, &filter, NULL, &subject_count));
    TEST_ASSERT_EQUAL_UINT (1, subject_count);
    std::vector<zlink_spot_node_subject_entry_t> subjects (subject_count);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_subjects (
      sub_node, &filter, &subjects[0], &subject_count));
    TEST_ASSERT_EQUAL_UINT (1, subject_count);
    TEST_ASSERT_EQUAL_UINT32 (ZLINK_SPOT_ROLE_SUB, subjects[0].role);
    TEST_ASSERT_EQUAL_UINT32 (ZLINK_SERVICE_EVENT_SUBJECT_TOPIC,
                              subjects[0].subject_kind);
    TEST_ASSERT_EQUAL_STRING ("topic.snapshot", subjects[0].subject);
    TEST_ASSERT_TRUE (subjects[0].active_peer_count >= 0);

    memset (&filter, 0, sizeof (filter));
    filter.role = ZLINK_SPOT_ROLE_PUB;
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_NOT_SUPPORTED,
      zlink_spot_node_subjects (sub_node, &filter, NULL, &subject_count));
    TEST_ASSERT_EQUAL_INT (ENOTSUP, zlink_errno ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&sub));
    destroy_node_and_spot_handle (&sub_node);
    destroy_node_and_spot_handle (&pub_node);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

static void test_spot_node_default_handle_owner_keeps_defaults_private ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *node_handle = create_node (ctx, "spot-default-owner");
    zlink::spot_node_t *node =
      static_cast<zlink::spot_node_t *> (node_handle);
    TEST_ASSERT_NOT_NULL (node);

    const int pub_sndhwm = 345;
    TEST_ASSERT_SUCCESS_ERRNO (node->set_pub_option (
      ZLINK_SPOT_PUB_OPT_SNDHWM, &pub_sndhwm, sizeof (pub_sndhwm)));

    zlink::spot_pub_t *pub = node->create_spot_pub ();
    TEST_ASSERT_NOT_NULL (pub);

    const int sub_rcvhwm = 678;
    TEST_ASSERT_SUCCESS_ERRNO (node->set_sub_option (
      ZLINK_SPOT_SUB_OPT_RCVHWM, &sub_rcvhwm, sizeof (sub_rcvhwm)));

    zlink::spot_internal_receiver_t *receiver =
      zlink::spot_node_access_t::ensure_internal_receiver (node);
    TEST_ASSERT_NOT_NULL (receiver);
    TEST_ASSERT_EQUAL_PTR (
      receiver, zlink::spot_node_access_t::ensure_internal_receiver (node));
    TEST_ASSERT_NULL (node->default_sub ());

    zlink::spot_sub_t *default_sub = node->ensure_default_sub ();
    TEST_ASSERT_NOT_NULL (default_sub);
    TEST_ASSERT_TRUE (receiver->impl () != default_sub);
    TEST_ASSERT_EQUAL_PTR (default_sub, node->default_sub ());

    TEST_ASSERT_SUCCESS_ERRNO (pub->destroy_from_node ());
    delete pub;
    destroy_node_and_spot_handle (&node_handle);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

static void test_discovery_local_value_route_limit_contract ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *discovery =
      zlink_discovery_new (ctx, ZLINK_AUTO_CONNECT_CLIENT_SERVER, "route-local");
    TEST_ASSERT_NOT_NULL (discovery);

    int64_t value = 0;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_get_value (discovery, &value));
    TEST_ASSERT_EQUAL_INT64 (0, value);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_set_value (discovery, -7));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_get_value (discovery, &value));
    TEST_ASSERT_EQUAL_INT64 (-7, value);

    size_t route_value_max_size = 4;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_option (discovery, ZLINK_OPT_ROUTE_VALUE_MAX_SIZE,
                        &route_value_max_size, sizeof (route_value_max_size)));
    size_t read_size = sizeof (route_value_max_size);
    size_t route_value_max_size_read = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_get_option (discovery, ZLINK_OPT_ROUTE_VALUE_MAX_SIZE,
                        &route_value_max_size_read, &read_size));
    TEST_ASSERT_EQUAL_UINT (sizeof (route_value_max_size_read), read_size);
    TEST_ASSERT_EQUAL_UINT (route_value_max_size, route_value_max_size_read);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
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
    RUN_SPOT_INTROSPECTION_TEST (test_spot_callback_model_receive_regression);
    RUN_SPOT_INTROSPECTION_TEST (test_spot_recv_model_receive_regression);
    RUN_SPOT_INTROSPECTION_TEST (test_spot_subscribe_empty_logical_queue_concurrent_no_crash);
    RUN_SPOT_INTROSPECTION_TEST (test_spot_subscription_mutation_does_not_race_dispatch_filter);
    RUN_SPOT_INTROSPECTION_TEST (test_spot_node_default_handle_owner_keeps_defaults_private);
    RUN_SPOT_INTROSPECTION_TEST (test_spot_unified_spot_basic);
    RUN_SPOT_INTROSPECTION_TEST (test_queue_pub_local_fanout_shared_block);
    RUN_SPOT_INTROSPECTION_TEST (test_queue_sub_exact_pattern_dedupe);
    RUN_SPOT_INTROSPECTION_TEST (test_queue_pub_no_subscriber_success);
    RUN_SPOT_INTROSPECTION_TEST (test_queue_pub_dead_spot_fails);
    RUN_SPOT_INTROSPECTION_TEST (test_spot_node_snapshot_status_peers_subjects);
    RUN_SPOT_INTROSPECTION_TEST (test_discovery_local_value_route_limit_contract);
#undef RUN_SPOT_INTROSPECTION_TEST
    return UNITY_END ();
}
