/* SPDX-License-Identifier: MPL-2.0 */

#include "../../testutil.hpp"
#include "../../testutil_unity.hpp"
#include "../../testutil_monitoring.hpp"
#include "../../../src/api/service_api_internal.hpp"
#include "../../../src/api/zlink_testing.hpp"
#include "../../../src/services/spot/spot_handle.hpp"
#include "../../../src/services/spot/spot_node.hpp"
#include "../../../src/services/spot/spot_node_access.hpp"
#include "../../../src/services/spot/spot_pub.hpp"
#include "../../../src/services/spot/spot_subject_access.hpp"
#include "../../../src/core/msg.hpp"

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
        if (zlink_spot_node_status_snapshot (node_, &status) == 0
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

static bool wait_for_subscription_ready (void *sub_node_,
                                         const char *endpoint_,
                                         const char *topic_)
{
    LIBZLINK_UNUSED (endpoint_);
    void *sub_handle = node_sub_spot_handle (sub_node_);
    if (!resolve_spot_sub_subject_poller_socket (sub_handle))
        return false;
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

    zlink::destroy_spot_handle_for_testing (it->second);
    erase_spot_mode_state (it->second);
    g_node_spot_handles.erase (it);
}

static void destroy_node_and_spot_handle (void **node_p_)
{
    if (!node_p_ || !*node_p_)
        return;

    destroy_node_spot_handle (*node_p_);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (node_p_));
}

static void *create_node (void *ctx_, const char *service_name_)
{
    LIBZLINK_UNUSED (service_name_);
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
        zlink::destroy_spot_handle_for_testing (it->second);
        erase_spot_mode_state (it->second);
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
    char service_name[64];
    size_t service_name_len = sizeof (service_name);
    char topic[64];
    size_t topic_len = sizeof (topic);
    TEST_ASSERT_EQUAL_INT (ZLINK_RECV_NO_DATA,
                           zlink_spot_subscribe (
                             sub, &source_rid, &parts, &part_count,
                             service_name, &service_name_len, topic,
                             &topic_len,
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
      zlink_spot_node_status_snapshot (sub_node, &status));
    TEST_ASSERT_EQUAL_STRING ("", status.service_name);
    TEST_ASSERT_TRUE (status.configured_peer_count >= 1);
    TEST_ASSERT_TRUE (status.connected_peer_count >= 1
                      || status.active_peer_count >= 1);
    TEST_ASSERT_TRUE (status.subject_count >= 1);
    TEST_ASSERT_TRUE (status.state == ZLINK_SPOT_NODE_STATE_CONNECTING
                      || status.state == ZLINK_SPOT_NODE_STATE_PARTIAL_READY
                      || status.state == ZLINK_SPOT_NODE_STATE_READY);

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
    TEST_ASSERT_TRUE (peers[0].state == ZLINK_SPOT_PEER_STATE_CONNECTING
                      || peers[0].state == ZLINK_SPOT_PEER_STATE_CONNECTED);

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
    TEST_ASSERT_TRUE (subjects[0].active_peer_count >= 0);

    memset (&filter, 0, sizeof (filter));
    filter.role = ZLINK_SPOT_ROLE_PUB;
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_NOT_SUPPORTED,
      zlink_spot_node_subjects_snapshot (sub_node, &filter, NULL, &subject_count));
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

static void test_discovery_local_value_metadata_contract ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *discovery =
      zlink_discovery_new (ctx, ZLINK_AUTO_CONNECT_CLIENT_SERVER, "metadata-local");
    TEST_ASSERT_NOT_NULL (discovery);

    int64_t value = 0;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_get_value (discovery, &value));
    TEST_ASSERT_EQUAL_INT64 (0, value);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_set_value (discovery, -7));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_get_value (discovery, &value));
    TEST_ASSERT_EQUAL_INT64 (-7, value);

    size_t metadata_max_size = 4;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_option (discovery, ZLINK_OPT_DISCOVERY_METADATA_MAX_SIZE,
                        &metadata_max_size, sizeof (metadata_max_size)));
    size_t read_size = sizeof (metadata_max_size);
    size_t metadata_max_size_read = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_get_option (discovery, ZLINK_OPT_DISCOVERY_METADATA_MAX_SIZE,
                        &metadata_max_size_read, &read_size));
    TEST_ASSERT_EQUAL_UINT (sizeof (metadata_max_size_read), read_size);
    TEST_ASSERT_EQUAL_UINT (metadata_max_size, metadata_max_size_read);

    zlink_msg_t metadata;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_get_metadata (discovery, &metadata));
    TEST_ASSERT_EQUAL_UINT (0, zlink_msg_size (&metadata));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_close (&metadata));

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_set_metadata (discovery, "abcd", 4));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_get_metadata (discovery, &metadata));
    TEST_ASSERT_EQUAL_UINT (4, zlink_msg_size (&metadata));
    TEST_ASSERT_EQUAL_MEMORY ("abcd", zlink_msg_data (&metadata), 4);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_close (&metadata));

    errno = 0;
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_INVALID_ARGUMENT,
      zlink_discovery_set_metadata (discovery, "abcde", 5));
    TEST_ASSERT_EQUAL_INT (EMSGSIZE, errno);

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
    RUN_SPOT_INTROSPECTION_TEST (test_spot_node_default_handle_owner_keeps_defaults_private);
    RUN_SPOT_INTROSPECTION_TEST (test_spot_unified_spot_basic);
    RUN_SPOT_INTROSPECTION_TEST (test_spot_node_snapshot_status_peers_subjects);
    RUN_SPOT_INTROSPECTION_TEST (test_discovery_local_value_metadata_contract);
#undef RUN_SPOT_INTROSPECTION_TEST
    return UNITY_END ();
}
