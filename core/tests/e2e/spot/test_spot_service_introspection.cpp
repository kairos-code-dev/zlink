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

struct service_monitor_probe_t
{
    service_monitor_probe_t () {}

    std::mutex mutex;
    std::condition_variable cv;
    std::vector<zlink_service_event_t> events;
};

static std::atomic<int> g_port_seed (22618);
static std::mutex g_default_handle_mutex;
static std::map<void *, spot_handle_t *> g_default_spot_handles;

static void *default_pub_handle (void *node_);
static void *default_sub_handle (void *node_);

static int next_port_seed ()
{
    return g_port_seed.fetch_add (8);
}

static bool service_event_matches (const zlink_service_event_t &event_,
                                   uint32_t expected_event_type_,
                                   const char *endpoint_prefix_)
{
    if (event_.event_type != expected_event_type_)
        return false;
    if (!endpoint_prefix_ || endpoint_prefix_[0] == '\0')
        return true;
    if ((event_.detail_flags & ZLINK_EVENT_DETAIL_ENDPOINT) == 0)
        return false;
    return strncmp (event_.endpoint, endpoint_prefix_, strlen (endpoint_prefix_))
           == 0;
}

static bool wait_for_service_event (void *monitor_,
                                    uint32_t expected_event_type_,
                                    const char *endpoint_prefix_,
                                    int timeout_ms_)
{
    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now () + std::chrono::milliseconds (timeout_ms_);

    while (std::chrono::steady_clock::now () < deadline) {
        const std::chrono::steady_clock::duration remaining =
          deadline - std::chrono::steady_clock::now ();
        const long remaining_ms =
          std::chrono::duration_cast<std::chrono::milliseconds> (remaining)
            .count ();
        zlink_pollitem_t item = {monitor_, 0, ZLINK_POLLIN, 0};
        const int rc =
          zlink_poll (&item, 1, remaining_ms > 0 ? remaining_ms : 0);
        if (rc <= 0 || (item.revents & ZLINK_POLLIN) == 0)
            continue;

        zlink_service_event_t event;
        if (recv_service_event_from_socket (monitor_, &event, 0) != 0)
            continue;
        if (service_event_matches (event, expected_event_type_,
                                   endpoint_prefix_)) {
            return true;
        }
    }

    return false;
}

static bool wait_for_service_event_value (void *monitor_,
                                          uint32_t expected_event_type_,
                                          uint32_t expected_value_,
                                          const char *endpoint_prefix_,
                                          int timeout_ms_)
{
    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now () + std::chrono::milliseconds (timeout_ms_);

    while (std::chrono::steady_clock::now () < deadline) {
        const std::chrono::steady_clock::duration remaining =
          deadline - std::chrono::steady_clock::now ();
        const long remaining_ms =
          std::chrono::duration_cast<std::chrono::milliseconds> (remaining)
            .count ();
        zlink_pollitem_t item = {monitor_, 0, ZLINK_POLLIN, 0};
        const int rc =
          zlink_poll (&item, 1, remaining_ms > 0 ? remaining_ms : 0);
        if (rc <= 0 || (item.revents & ZLINK_POLLIN) == 0)
            continue;

        zlink_service_event_t event;
        if (recv_service_event_from_socket (monitor_, &event, 0) != 0)
            continue;
        if (event.value != expected_value_)
            continue;
        if (service_event_matches (event, expected_event_type_,
                                   endpoint_prefix_)) {
            return true;
        }
    }

    return false;
}

static void service_monitor_probe_handler (const zlink_service_event_t *event_,
                                           void *userdata_)
{
    service_monitor_probe_t *probe =
      static_cast<service_monitor_probe_t *> (userdata_);
    if (!probe || !event_)
        return;

    {
        std::lock_guard<std::mutex> lock (probe->mutex);
        probe->events.push_back (*event_);
    }
    probe->cv.notify_all ();
}

static bool pop_probe_service_event_value_locked (
  service_monitor_probe_t *probe_,
  uint32_t expected_event_type_,
  uint32_t expected_value_,
  const char *endpoint_prefix_)
{
    for (std::vector<zlink_service_event_t>::iterator it =
           probe_->events.begin ();
         it != probe_->events.end (); ++it) {
        if (it->value != expected_value_)
            continue;
        if (!service_event_matches (*it, expected_event_type_, endpoint_prefix_))
            continue;

        probe_->events.erase (it);
        return true;
    }

    return false;
}

static bool wait_for_probe_service_event_value (
  service_monitor_probe_t *probe_,
  uint32_t expected_event_type_,
  uint32_t expected_value_,
  const char *endpoint_prefix_,
  int timeout_ms_)
{
    std::unique_lock<std::mutex> lock (probe_->mutex);
    if (pop_probe_service_event_value_locked (
          probe_, expected_event_type_, expected_value_, endpoint_prefix_)) {
        return true;
    }

    return probe_->cv.wait_for (
      lock, std::chrono::milliseconds (timeout_ms_),
      [probe_, expected_event_type_, expected_value_, endpoint_prefix_]() {
          return pop_probe_service_event_value_locked (
            probe_, expected_event_type_, expected_value_, endpoint_prefix_);
      });
}

static bool wait_for_subscription_ready (void *sub_node_,
                                         const char *endpoint_,
                                         const char *topic_)
{
    void *sub_handle = default_sub_handle (sub_node_);
    zlink_service_monitor_open_options_t opts;
    memset (&opts, 0, sizeof (opts));
    opts.events = ZLINK_SPOT_SUB_FILTER_APPLIED
                  | ZLINK_SPOT_MONITOR_EVENT_SUBSCRIPTION_READY_CHANGED
                  | ZLINK_MONITOR_EVENT_ERROR;
    void *monitor = zlink_service_monitor_open (sub_handle, &opts);
    if (!monitor)
        return false;

    const bool ok =
      zlink_set_subscription (sub_handle, topic_) == 0
      && wait_for_service_event (monitor, ZLINK_SPOT_SUB_FILTER_APPLIED, NULL,
                                 3000)
      && wait_for_service_event (
        monitor, ZLINK_SPOT_MONITOR_EVENT_SUBSCRIPTION_READY_CHANGED, endpoint_,
        3000);
    zlink_monitor_close (&monitor);
    return ok;
}

static bool wait_for_existing_subscription_ready (void *sub_node_,
                                                  const char *endpoint_)
{
    void *sub_handle = default_sub_handle (sub_node_);
    zlink_service_monitor_open_options_t opts;
    memset (&opts, 0, sizeof (opts));
    opts.events = ZLINK_SPOT_MONITOR_EVENT_SUBSCRIPTION_READY_CHANGED | ZLINK_MONITOR_EVENT_ERROR;
    void *monitor = zlink_service_monitor_open (sub_handle, &opts);
    if (!monitor)
        return false;

    const bool ok = wait_for_service_event (
      monitor, ZLINK_SPOT_MONITOR_EVENT_SUBSCRIPTION_READY_CHANGED, endpoint_,
      3000);
    zlink_monitor_close (&monitor);
    return ok;
}

static bool wait_for_pub_ready (void *pub_node_)
{
    void *pub_handle = default_pub_handle (pub_node_);
    zlink_service_monitor_open_options_t opts;
    memset (&opts, 0, sizeof (opts));
    opts.events = ZLINK_SPOT_MONITOR_EVENT_READY_CHANGED
                  | ZLINK_MONITOR_EVENT_PEER_UP
                  | ZLINK_MONITOR_EVENT_ERROR
                  | ZLINK_SPOT_MONITOR_EVENT_PUB_FIRST_DELIVERY_READY_CHANGED;
    void *monitor = zlink_service_monitor_open (pub_handle, &opts);
    if (!monitor)
        return false;

    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now () + std::chrono::milliseconds (3000);
    while (std::chrono::steady_clock::now () < deadline) {
        zlink_monitor_snapshot_t snapshot;
        if (zlink_monitor_snapshot (monitor, &snapshot) == 0
            && (snapshot.state_flags & ZLINK_MONITOR_STATE_SEND_READY) != 0) {
            zlink_monitor_close (&monitor);
            return true;
        }
        (void) wait_for_service_event (monitor, ZLINK_SPOT_MONITOR_EVENT_READY_CHANGED,
                                       NULL, 200);
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

static void *default_pub_handle (void *node_)
{
    zlink::spot_node_t *node = zlink::spot_node_access_t::from_handle (node_);
    if (!node)
        return node_;

    std::lock_guard<std::mutex> lock (g_default_handle_mutex);
    std::map<void *, spot_handle_t *>::iterator it =
      g_default_spot_handles.find (node_);
    if (it != g_default_spot_handles.end ())
        return it->second;

    spot_handle_t *spot = new (std::nothrow) spot_handle_t ();
    if (!spot) {
        errno = ENOMEM;
        return NULL;
    }
    spot->node = node;
    g_default_spot_handles[node_] = spot;
    return spot;
}

static void *default_sub_handle (void *node_)
{
    return default_pub_handle (node_);
}

static void destroy_default_handle (void *node_)
{
    std::lock_guard<std::mutex> lock (g_default_handle_mutex);
    std::map<void *, spot_handle_t *>::iterator it =
      g_default_spot_handles.find (node_);
    if (it == g_default_spot_handles.end ())
        return;

    zlink::destroy_spot_handle_for_testing (it->second);
    g_default_spot_handles.erase (it);
}

static void destroy_node_and_default_handle (void **node_p_)
{
    if (!node_p_ || !*node_p_)
        return;

    destroy_default_handle (*node_p_);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (node_p_));
}

static void *create_node (void *ctx_, const char *service_name_)
{
    LIBZLINK_UNUSED (service_name_);
    void *node = zlink_spot_node_new (ctx_);
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

static void *create_registry (void *ctx_,
                              int *port_seed_,
                              char *pub_endpoint_out_,
                              char *router_endpoint_out_)
{
    void *registry = zlink_registry_new (ctx_);
    TEST_ASSERT_NOT_NULL (registry);

    for (int i = 0; i < 32; ++i) {
        snprintf (pub_endpoint_out_, MAX_SOCKET_STRING, "tcp://127.0.0.1:%d",
                  test_port ((*port_seed_)++));
        snprintf (router_endpoint_out_, MAX_SOCKET_STRING, "tcp://127.0.0.1:%d",
                  test_port ((*port_seed_)++));
        if (zlink_registry_bind (registry, pub_endpoint_out_,
                                 router_endpoint_out_)
            == 0) {
            TEST_ASSERT_SUCCESS_ERRNO (
              zlink_registry_set_broadcast_interval (registry, 50));
            return registry;
        }
        if (zlink_errno () != EADDRINUSE)
            break;
    }

    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
    return NULL;
}

static bool connect_discovery_registry_until_ready (void *discovery_,
                                                    const char *endpoint_,
                                                    int timeout_ms_)
{
    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now () + std::chrono::milliseconds (timeout_ms_);
    while (std::chrono::steady_clock::now () < deadline) {
        if (zlink_discovery_connect_registry (discovery_, endpoint_) == 0)
            return true;
        msleep (20);
    }
    return false;
}

static bool wait_for_registry_member_count (void *registry_,
                                            zlink_service_type_t service_type_,
                                            const char *service_name_,
                                            size_t expected_count_,
                                            int timeout_ms_)
{
    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now () + std::chrono::milliseconds (timeout_ms_);
    while (std::chrono::steady_clock::now () < deadline) {
        size_t count = 0;
        if (zlink_registry_member_peers (registry_, service_type_, service_name_,
                                         NULL, &count)
              == 0
            && count == expected_count_) {
            return true;
        }
        msleep (20);
    }
    return false;
}

static bool wait_for_discovery_member_count (void *discovery_,
                                             size_t expected_count_,
                                             int timeout_ms_)
{
    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now () + std::chrono::milliseconds (timeout_ms_);
    while (std::chrono::steady_clock::now () < deadline) {
        size_t count = 0;
        if (zlink_discovery_member_peers (discovery_, NULL, &count) == 0
            && count == expected_count_) {
            return true;
        }
        msleep (20);
    }
    return false;
}

static const zlink_member_peer_entry_t *find_member_peer (
  const std::vector<zlink_member_peer_entry_t> &entries_,
  const char *endpoint_)
{
    for (size_t i = 0; i < entries_.size (); ++i) {
        if (strcmp (entries_[i].endpoint, endpoint_) == 0)
            return &entries_[i];
    }
    return NULL;
}

static int publish_text (void *subject_,
                         const char *topic_,
                         const char *payload_)
{
    subject_ = default_pub_handle (subject_);
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

static int publish_two_parts (void *subject_,
                              const char *topic_,
                              const char *part_a_,
                              const char *part_b_)
{
    subject_ = default_pub_handle (subject_);
    zlink_msg_t parts[2];
    const size_t size_a = part_a_ ? strlen (part_a_) : 0;
    const size_t size_b = part_b_ ? strlen (part_b_) : 0;
    if (zlink_msg_init_size (&parts[0], size_a) != 0)
        return -1;
    if (size_a > 0)
        memcpy (zlink_msg_data (&parts[0]), part_a_, size_a);

    if (zlink_msg_init_size (&parts[1], size_b) != 0) {
        const int err = zlink_errno ();
        zlink_msg_close (&parts[0]);
        errno = err;
        return -1;
    }
    if (size_b > 0)
        memcpy (zlink_msg_data (&parts[1]), part_b_, size_b);

    const int rc = zlink_publish (subject_, topic_, parts, 2, 0);
    if (rc != 0) {
        const int err = zlink_errno ();
        zlink_msg_close (&parts[0]);
        zlink_msg_close (&parts[1]);
        errno = err;
    }
    return rc;
}

static bool wait_for_subscribe_payload (void *subject_,
                                        const char *expected_topic_,
                                        const char *expected_payload_,
                                        int timeout_ms_)
{
    subject_ = default_sub_handle (subject_);
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

static bool wait_for_subscribe_payload_parts (void *subject_,
                                              const char *expected_topic_,
                                              const char *expected_part_a_,
                                              const char *expected_part_b_,
                                              int timeout_ms_)
{
    subject_ = default_sub_handle (subject_);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_option (subject_, ZLINK_OPT_RCVTIMEO, &timeout_ms_,
                        sizeof (timeout_ms_)));

    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    char topic[256];
    memset (topic, 0, sizeof (topic));
    size_t topic_len = sizeof (topic);
    if (zlink_subscribe (subject_, NULL, &parts, &part_count, topic,
                         &topic_len, 0)
        != 0) {
        return false;
    }

    const std::string got_topic (topic, topic_len);
    std::string got_part_a;
    std::string got_part_b;
    if (part_count > 0) {
        got_part_a.assign (
          static_cast<const char *> (zlink_msg_data (&parts[0])),
          zlink_msg_size (&parts[0]));
    }
    if (part_count > 1) {
        got_part_b.assign (
          static_cast<const char *> (zlink_msg_data (&parts[1])),
          zlink_msg_size (&parts[1]));
    }
    for (size_t i = 0; i < part_count; ++i)
        zlink_msg_close (&parts[i]);
    free (parts);

    return got_topic == expected_topic_ && part_count == 2
           && got_part_a == expected_part_a_ && got_part_b == expected_part_b_;
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
    std::lock_guard<std::mutex> lock (g_default_handle_mutex);
    for (std::map<void *, spot_handle_t *>::iterator it =
           g_default_spot_handles.begin ();
         it != g_default_spot_handles.end (); ++it) {
        zlink::destroy_spot_handle_for_testing (it->second);
    }
    g_default_spot_handles.clear ();
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
      zlink_subscription_at (default_sub_handle (sub_node), 0, filter,
                             &filter_len, &is_pattern));
    TEST_ASSERT_EQUAL_UINT (strlen ("topic.alpha"), filter_len);
    TEST_ASSERT_EQUAL_MEMORY ("topic.alpha", filter, filter_len);
    TEST_ASSERT_EQUAL_INT (0, is_pattern);

    TEST_ASSERT_TRUE (wait_for_pub_ready (pub_node));
    TEST_ASSERT_SUCCESS_ERRNO (
      publish_text (pub_node, "topic.alpha", "payload"));
    TEST_ASSERT_TRUE (wait_for_subscribe_payload (
      sub_node, "topic.alpha", "payload", 3000));

    destroy_node_and_default_handle (&sub_node);
    destroy_node_and_default_handle (&pub_node);
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

    zlink_service_monitor_open_options_t opts;
    memset (&opts, 0, sizeof (opts));
    opts.events = ZLINK_SPOT_SUB_FILTER_APPLIED
                  | ZLINK_SPOT_MONITOR_EVENT_SUBSCRIPTION_READY_CHANGED
                  | ZLINK_SPOT_MONITOR_EVENT_READY_CHANGED
                  | ZLINK_MONITOR_EVENT_ERROR;
    void *monitor = zlink_service_monitor_open (default_sub_handle (sub_node),
                                                &opts);
    TEST_ASSERT_NOT_NULL (monitor);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_connect_peer (sub_node, endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_subscription (default_sub_handle (sub_node), "topic.monitor"));

    TEST_ASSERT_TRUE (wait_for_service_event (
      monitor, ZLINK_SPOT_SUB_FILTER_APPLIED, NULL, 3000));
    TEST_ASSERT_TRUE (wait_for_service_event (
      monitor, ZLINK_SPOT_MONITOR_EVENT_SUBSCRIPTION_READY_CHANGED, endpoint,
      3000));

    zlink_monitor_snapshot_t snapshot;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_monitor_snapshot (monitor, &snapshot));
    TEST_ASSERT_TRUE ((snapshot.state_flags & ZLINK_MONITOR_STATE_READY) != 0);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_monitor_close (&monitor));
    destroy_node_and_default_handle (&sub_node);
    destroy_node_and_default_handle (&pub_node);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

static void test_spot_late_connect_replays_existing_subscription ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *pub_node = create_node (ctx, "spot-replay");
    void *sub_node = create_node (ctx, "spot-replay");

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_subscription (default_sub_handle (sub_node), "topic.replay"));

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

    destroy_node_and_default_handle (&sub_node);
    destroy_node_and_default_handle (&pub_node);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

static void test_spot_subscription_ready_changed_reports_loss ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *pub_node = create_node (ctx, "spot-sub-ready-loss");
    void *sub_node = create_node (ctx, "spot-sub-ready-loss");

    char endpoint[MAX_SOCKET_STRING];
    int port_seed = next_port_seed ();
    TEST_ASSERT_SUCCESS_ERRNO (bind_node (pub_node, &port_seed, endpoint));

    zlink_service_monitor_open_options_t opts;
    memset (&opts, 0, sizeof (opts));
    opts.events =
      ZLINK_SPOT_MONITOR_EVENT_SUBSCRIPTION_READY_CHANGED | ZLINK_MONITOR_EVENT_ERROR;
    void *monitor = zlink_service_monitor_open (default_sub_handle (sub_node),
                                                &opts);
    TEST_ASSERT_NOT_NULL (monitor);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_connect_peer (sub_node, endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_subscription (default_sub_handle (sub_node), "topic.loss"));
    TEST_ASSERT_TRUE (wait_for_service_event_value (
      monitor, ZLINK_SPOT_MONITOR_EVENT_SUBSCRIPTION_READY_CHANGED, 1, endpoint,
      3000));

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_unset_subscription (default_sub_handle (sub_node), "topic.loss"));
    TEST_ASSERT_TRUE (wait_for_service_event_value (
      monitor, ZLINK_SPOT_MONITOR_EVENT_SUBSCRIPTION_READY_CHANGED, 0, NULL,
      3000));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_monitor_close (&monitor));
    destroy_node_and_default_handle (&sub_node);
    destroy_node_and_default_handle (&pub_node);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

static void test_spot_handler_monitor_close_after_ready_change_is_stable ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *pub_node = create_node (ctx, "spot-sub-handler-close");
    void *sub_node = create_node (ctx, "spot-sub-handler-close");

    char endpoint[MAX_SOCKET_STRING];
    int port_seed = next_port_seed ();
    TEST_ASSERT_SUCCESS_ERRNO (bind_node (pub_node, &port_seed, endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_connect_peer (sub_node, endpoint));

    for (int iteration = 0; iteration < 32; ++iteration) {
        service_monitor_probe_t probe;
        zlink_service_monitor_open_options_t opts;
        memset (&opts, 0, sizeof (opts));
        opts.events = ZLINK_SPOT_MONITOR_EVENT_SUBSCRIPTION_READY_CHANGED
                      | ZLINK_MONITOR_EVENT_ERROR;

        void *monitor = zlink_service_monitor_open (
          default_sub_handle (sub_node), &opts);
        TEST_ASSERT_NOT_NULL (monitor);
        TEST_ASSERT_SUCCESS_ERRNO (zlink_service_monitor_handler (
          monitor, &service_monitor_probe_handler, &probe));

        char topic[64];
        snprintf (topic, sizeof (topic), "topic.handler-close.%d", iteration);
        TEST_ASSERT_SUCCESS_ERRNO (
          zlink_set_subscription (default_sub_handle (sub_node), topic));
        TEST_ASSERT_TRUE (wait_for_probe_service_event_value (
          &probe, ZLINK_SPOT_MONITOR_EVENT_SUBSCRIPTION_READY_CHANGED, 1,
          endpoint, 3000));

        TEST_ASSERT_SUCCESS_ERRNO (
          zlink_unset_subscription (default_sub_handle (sub_node), topic));
        TEST_ASSERT_TRUE (wait_for_probe_service_event_value (
          &probe, ZLINK_SPOT_MONITOR_EVENT_SUBSCRIPTION_READY_CHANGED, 0, NULL,
          3000));

        TEST_ASSERT_SUCCESS_ERRNO (zlink_monitor_close (&monitor));
    }

    destroy_node_and_default_handle (&sub_node);
    destroy_node_and_default_handle (&pub_node);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

static void test_spot_callback_model_receive_regression ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *pub_node = create_node (ctx, "spot-callback");
    void *sub_node = create_node (ctx, "spot-callback");
    TEST_ASSERT_NOT_NULL (sub_node);
    void *sub = default_sub_handle (sub_node);
    TEST_ASSERT_NOT_NULL (sub);
    set_linger_zero (sub);

    subscribe_probe_t probe;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_subscribe_handler (sub, &subscribe_probe_handler, &probe));

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

    destroy_node_and_default_handle (&sub_node);
    destroy_node_and_default_handle (&pub_node);
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

    destroy_node_and_default_handle (&sub_node);
    destroy_node_and_default_handle (&pub_node);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

static void test_spot_node_send_ready_handler_isolated_by_service ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *pub_node = create_node (ctx, "spot-ready");
    void *sub_node = create_node (ctx, "spot-ready");
    void *pub = default_pub_handle (pub_node);
    TEST_ASSERT_NOT_NULL (pub);

    subscribe_probe_t callback_mode_probe;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_subscribe_handler (default_sub_handle (pub_node), &subscribe_probe_handler,
                               &callback_mode_probe));

    send_ready_probe_t probe;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_send_ready_handler (pub, &send_ready_probe_handler, &probe));

    char endpoint[MAX_SOCKET_STRING];
    int port_seed = next_port_seed ();
    TEST_ASSERT_SUCCESS_ERRNO (bind_node (pub_node, &port_seed, endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (
      publish_text (pub, "__warmup__", "ready"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_connect_peer (sub_node, endpoint));

    TEST_ASSERT_TRUE (wait_for_pub_ready (pub_node));
    TEST_ASSERT_TRUE (wait_for_send_ready (&probe, 3000));

    destroy_node_and_default_handle (&sub_node);
    destroy_node_and_default_handle (&pub_node);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

static void test_spot_unified_spot_basic ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *pub_spot = zlink_spot_new (ctx);
    void *sub_spot = zlink_spot_new (ctx);
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

static void test_spot_unified_spot_callback_self_delivery ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *spot = zlink_spot_new (ctx);
    TEST_ASSERT_NOT_NULL (spot);
    set_linger_zero (spot);

    subscribe_probe_t probe;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_subscribe_handler (spot, &subscribe_probe_handler, &probe));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_subscription (spot, "topic.unified.cb"));
    TEST_ASSERT_SUCCESS_ERRNO (
      publish_text (spot, "topic.unified.cb", "callback"));
    TEST_ASSERT_TRUE (wait_for_callback_payload (
      &probe, "topic.unified.cb", "callback", 3000));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot));
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
    TEST_ASSERT_EQUAL_STRING ("", status.service_name);
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

    destroy_node_and_default_handle (&sub_node);
    destroy_node_and_default_handle (&pub_node);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

static void test_spot_publish_rollback_preserves_next_topic_boundary ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *pub_node = create_node (ctx, "spot-rollback");
    void *sub_node = create_node (ctx, "spot-rollback");

    const int timeout_ms = 200;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_option (
      pub_node, ZLINK_OPT_SNDTIMEO, &timeout_ms, sizeof (timeout_ms)));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_option (
      sub_node, ZLINK_OPT_RCVTIMEO, &timeout_ms, sizeof (timeout_ms)));

    char endpoint[MAX_SOCKET_STRING];
    int port_seed = next_port_seed ();
    TEST_ASSERT_SUCCESS_ERRNO (bind_node (pub_node, &port_seed, endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_connect_peer (sub_node, endpoint));
    TEST_ASSERT_TRUE (
      wait_for_subscription_ready (sub_node, endpoint, "topic.rollback"));
    TEST_ASSERT_TRUE (wait_for_pub_ready (pub_node));

    zlink::spot_node_t *pub_impl =
      static_cast<zlink::spot_node_t *> (pub_node);
    zlink::spot_pub_t *spot_pub = pub_impl->ensure_default_pub ();
    TEST_ASSERT_NOT_NULL (spot_pub);
    zlink::socket_base_t *pub_socket = spot_pub->poller_socket ();
    TEST_ASSERT_NOT_NULL (pub_socket);

    zlink::msg_t topic_part;
    TEST_ASSERT_SUCCESS_ERRNO (topic_part.init_size (strlen ("topic.rollback")));
    memcpy (topic_part.data (), "topic.rollback", strlen ("topic.rollback"));
    TEST_ASSERT_SUCCESS_ERRNO (pub_socket->send (&topic_part, ZLINK_SNDMORE));
    TEST_ASSERT_SUCCESS_ERRNO (pub_socket->rollback ());
    topic_part.close ();

    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    char topic[256];
    memset (topic, 0, sizeof (topic));
    size_t topic_len = sizeof (topic);
    TEST_ASSERT_FAILURE_ERRNO (
      EAGAIN,
      zlink_subscribe (default_sub_handle (sub_node), NULL, &parts, &part_count,
                       topic, &topic_len, ZLINK_DONTWAIT));

    TEST_ASSERT_SUCCESS_ERRNO (
      publish_two_parts (pub_node, "topic.rollback", "part-a", "part-b"));
    TEST_ASSERT_TRUE (wait_for_subscribe_payload_parts (
      sub_node, "topic.rollback", "part-a", "part-b", 3000));

    destroy_node_and_default_handle (&sub_node);
    destroy_node_and_default_handle (&pub_node);
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

    zlink::spot_pub_t *default_pub = node->ensure_default_pub ();
    TEST_ASSERT_NOT_NULL (default_pub);
    TEST_ASSERT_EQUAL_PTR (default_pub, node->ensure_default_pub ());

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

    destroy_node_and_default_handle (&node_handle);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

static void test_discovery_local_value_metadata_contract ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *discovery =
      zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_SOCKET, "metadata-local");
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
    TEST_ASSERT_EQUAL_INT (-1,
                           zlink_discovery_set_metadata (discovery, "abcde", 5));
    TEST_ASSERT_EQUAL_INT (EMSGSIZE, errno);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

static void test_registry_and_discovery_member_peer_queries ()
{
    if (!zlink_has ("tcp")) {
        TEST_IGNORE_MESSAGE ("TCP not available");
        return;
    }

    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    int registry_seed = next_port_seed ();
    char registry_pub[MAX_SOCKET_STRING];
    char registry_router[MAX_SOCKET_STRING];
    void *registry =
      create_registry (ctx, &registry_seed, registry_pub, registry_router);
    TEST_ASSERT_NOT_NULL (registry);

    void *discovery_a =
      zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_SPOT, "spot-metadata");
    void *discovery_b =
      zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_SPOT, "spot-metadata");
    TEST_ASSERT_NOT_NULL (discovery_a);
    TEST_ASSERT_NOT_NULL (discovery_b);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_set_value (discovery_a, 11));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_set_metadata (discovery_a, "alpha", 5));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_set_value (discovery_b, 22));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_set_metadata (discovery_b, NULL, 0));

    TEST_ASSERT_TRUE (connect_discovery_registry_until_ready (
      discovery_a, registry_router, 2000));
    TEST_ASSERT_TRUE (connect_discovery_registry_until_ready (
      discovery_b, registry_router, 2000));

    void *node_a = create_node (ctx, "spot-metadata");
    void *node_b = create_node (ctx, "spot-metadata");
    TEST_ASSERT_NOT_NULL (node_a);
    TEST_ASSERT_NOT_NULL (node_b);

    int port_seed = next_port_seed ();
    char endpoint_a[MAX_SOCKET_STRING];
    char endpoint_b[MAX_SOCKET_STRING];
    TEST_ASSERT_SUCCESS_ERRNO (bind_node (node_a, &port_seed, endpoint_a));
    TEST_ASSERT_SUCCESS_ERRNO (bind_node (node_b, &port_seed, endpoint_b));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_attach_discovery (node_a, discovery_a));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_attach_discovery (node_b, discovery_b));

    TEST_ASSERT_TRUE (wait_for_registry_member_count (
      registry, ZLINK_SERVICE_TYPE_SPOT, "spot-metadata", 2, 5000));
    TEST_ASSERT_TRUE (
      wait_for_discovery_member_count (discovery_a, 1, 5000));

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_subscription (default_sub_handle (node_b), "topic.metadata"));
    TEST_ASSERT_TRUE (wait_for_pub_ready (node_a));
    TEST_ASSERT_SUCCESS_ERRNO (
      publish_text (node_a, "topic.metadata", "member-query"));
    TEST_ASSERT_TRUE (wait_for_subscribe_payload (
      node_b, "topic.metadata", "member-query", 5000));

    size_t count = 0;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_member_peers (
      registry, ZLINK_SERVICE_TYPE_SPOT, "spot-metadata", NULL, &count));
    TEST_ASSERT_EQUAL_UINT (2, count);
    std::vector<zlink_member_peer_entry_t> members (count);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_member_peers (
      registry, ZLINK_SERVICE_TYPE_SPOT, "spot-metadata", &members[0], &count));
    TEST_ASSERT_EQUAL_UINT (2, count);

    const zlink_member_peer_entry_t *member_a =
      find_member_peer (members, endpoint_a);
    const zlink_member_peer_entry_t *member_b =
      find_member_peer (members, endpoint_b);
    TEST_ASSERT_NOT_NULL (member_a);
    TEST_ASSERT_NOT_NULL (member_b);
    TEST_ASSERT_EQUAL_INT64 (11, member_a->value);
    TEST_ASSERT_EQUAL_INT64 (22, member_b->value);

    count = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_member_peers (discovery_a, NULL, &count));
    TEST_ASSERT_EQUAL_UINT (1, count);
    std::vector<zlink_member_peer_entry_t> remote_members (count);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_member_peers (
      discovery_a, &remote_members[0], &count));
    TEST_ASSERT_EQUAL_UINT (1, count);
    TEST_ASSERT_EQUAL_STRING (endpoint_b, remote_members[0].endpoint);
    TEST_ASSERT_EQUAL_INT64 (22, remote_members[0].value);

    zlink_msg_t metadata;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_member_peer_metadata (
      registry, ZLINK_SERVICE_TYPE_SPOT, "spot-metadata",
      member_a->service_role, endpoint_a, &metadata));
    TEST_ASSERT_EQUAL_UINT (5, zlink_msg_size (&metadata));
    TEST_ASSERT_EQUAL_MEMORY ("alpha", zlink_msg_data (&metadata), 5);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_close (&metadata));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_member_peer_metadata (
      discovery_a, remote_members[0].service_role, endpoint_b, &metadata));
    TEST_ASSERT_EQUAL_UINT (0, zlink_msg_size (&metadata));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_close (&metadata));

    destroy_node_and_default_handle (&node_b);
    destroy_node_and_default_handle (&node_a);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery_b));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery_a));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
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
    RUN_SPOT_INTROSPECTION_TEST (test_spot_subscription_ready_changed_reports_loss);
    RUN_SPOT_INTROSPECTION_TEST (test_spot_handler_monitor_close_after_ready_change_is_stable);
    RUN_SPOT_INTROSPECTION_TEST (test_spot_callback_model_receive_regression);
    RUN_SPOT_INTROSPECTION_TEST (test_spot_recv_model_receive_regression);
    RUN_SPOT_INTROSPECTION_TEST (test_spot_publish_rollback_preserves_next_topic_boundary);
    RUN_SPOT_INTROSPECTION_TEST (test_spot_node_default_handle_owner_keeps_defaults_private);
    RUN_SPOT_INTROSPECTION_TEST (test_spot_unified_spot_basic);
    RUN_SPOT_INTROSPECTION_TEST (test_spot_unified_spot_callback_self_delivery);
    RUN_SPOT_INTROSPECTION_TEST (test_spot_node_snapshot_status_peers_subjects);
    RUN_SPOT_INTROSPECTION_TEST (test_discovery_local_value_metadata_contract);
    RUN_SPOT_INTROSPECTION_TEST (test_registry_and_discovery_member_peer_queries);
#undef RUN_SPOT_INTROSPECTION_TEST
    return UNITY_END ();
}
