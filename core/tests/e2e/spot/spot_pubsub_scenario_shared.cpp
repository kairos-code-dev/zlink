/* SPDX-License-Identifier: MPL-2.0 */

#include "spot_pubsub_scenario_shared.hpp"
#include "../../../src/api/service_api_internal.hpp"
#include "../../../src/api/zlink_testing.hpp"
#include "../../../src/services/spot/spot_handle.hpp"
#include "../../../src/services/spot/spot_node.hpp"
#include "../../../src/services/spot/spot_node_access.hpp"
#include "../../../src/services/spot/spot_subject_access.hpp"

#include <chrono>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <thread>

#if defined(_WIN32)
#include <direct.h>
#include <windows.h>
#else
#include <sys/resource.h>
#include <sys/stat.h>
#endif

static std::mutex g_spot_probe_mutex;
static std::map<void *, queued_spot_probe_t *> g_sub_probes;
static std::map<void *, queued_spot_probe_t *> g_node_probes;
static std::map<void *, spot_handle_t *> g_spot_handles;

bool test_debug_enabled ()
{
    return getenv ("ZLINK_TEST_DEBUG") != NULL;
}

void step_log (const char *msg_)
{
    if (test_debug_enabled ()) {
        fprintf (stderr, "[test] %s\n", msg_ ? msg_ : "");
        fflush (stderr);
    }
}

bool read_spot_snapshot (void *spot_, zlink_monitor_snapshot_t *out_)
{
    if (!spot_ || !out_)
        return false;

    spot_handle_t *spot = as_spot_handle (spot_);
    if (!spot || !spot->node)
        return false;

    memset (out_, 0, sizeof (*out_));
    zlink_spot_node_status_t status;
    if (zlink_spot_node_status_snapshot (spot->node, &status) != 0)
        return false;

    const bool is_pub =
      !spot->logical_state || spot->logical_state->sub == NULL;
    out_->source_kind =
      is_pub ? ZLINK_MONITOR_SOURCE_SPOT_PUB : ZLINK_MONITOR_SOURCE_SPOT_SUB;
    if (status.active_peer_count > 0
        && (is_pub || status.subject_count > 0)) {
        out_->state_flags |= ZLINK_MONITOR_STATE_READY;
    }
    return true;
}

int env_int_or_default (const char *name_, int default_value_)
{
    const char *value = getenv (name_);
    if (!value || !*value)
        return default_value_;

    char *end = NULL;
    long parsed = strtol (value, &end, 10);
    if (end == value || (end && *end != '\0') || parsed <= 0)
        return default_value_;

    return parsed > INT_MAX ? INT_MAX : static_cast<int> (parsed);
}

void ignore_spot_handler (const zlink_routing_id_t *,
                          const char *,
                          size_t,
                          zlink_msg_t *parts_,
                          size_t part_count_,
                          void *)
{
    if (!parts_)
        return;
    for (size_t i = 0; i < part_count_; ++i)
        zlink_msg_close (&parts_[i]);
}

void *create_spot_node (void *ctx_, const char *service_name_)
{
    LIBZLINK_UNUSED (service_name_);
    void *node = zlink_spot_node_new (ctx_, NULL);
    if (!node)
        return NULL;

    const int linger = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_option (node, ZLINK_OPT_LINGER, &linger, sizeof (linger)));
    return node;
}

int publish_text (spot_publish_fn_t publish_fn_,
                  void *handle_,
                  const char *topic_id_,
                  const char *payload_,
                  int flags_)
{
    zlink_msg_t part;
    const size_t size = payload_ ? strlen (payload_) : 0;
    if (zlink_msg_init_size (&part, size) != 0)
        return -1;
    if (size > 0)
        memcpy (zlink_msg_data (&part), payload_, size);
    const zlink_submit_result_t rc =
      publish_fn_ (
        handle_, topic_id_, &part, 1,
        static_cast<zlink_send_flags_t> (flags_));
    if (rc != ZLINK_SUBMIT_OK) {
        const int err = errno;
        zlink_msg_close (&part);
        errno = err;
        return -1;
    }
    return 0;
}

void *create_spot_pub_handle (void *node_)
{
    zlink::spot_node_t *node = zlink::spot_node_access_t::from_handle (node_);
    if (!node)
        return NULL;

    spot_handle_t *spot = NULL;
    {
        std::lock_guard<std::mutex> lock (g_spot_probe_mutex);
        std::map<void *, spot_handle_t *>::iterator it = g_spot_handles.find (node_);
        if (it != g_spot_handles.end ())
            spot = it->second;
    }
    if (!spot) {
        spot = new (std::nothrow) spot_handle_t ();
        if (!spot) {
            errno = ENOMEM;
            return NULL;
        }
        spot->node = node;
        spot->logical_state = zlink::spot_node_access_t::entry_spot_state (node);
        TEST_ASSERT_NOT_NULL (spot->logical_state);
        spot->spot_routing_id = spot->logical_state->routing_id;
        register_spot_mode_state (spot);
        std::lock_guard<std::mutex> lock (g_spot_probe_mutex);
        g_spot_handles[node_] = spot;
    }

    const int linger = 0;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_option (
      spot, ZLINK_OPT_LINGER, &linger, sizeof (linger)));
    return spot;
}

void *create_spot_sub_handle (void *node_)
{
    void *spot_sub = create_spot_pub_handle (node_);
    if (!spot_sub)
        return NULL;

    const int linger = 0;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_option (
      spot_sub, ZLINK_OPT_LINGER, &linger, sizeof (linger)));
    return spot_sub;
}

void setUp ()
{
}

static void clear_spot_probe_map (std::map<void *, queued_spot_probe_t *> *map_)
{
    for (std::map<void *, queued_spot_probe_t *>::iterator it =
           map_->begin ();
         it != map_->end (); ++it) {
        delete it->second;
    }
    map_->clear ();
}

static void clear_spot_handle_map ()
{
    for (std::map<void *, spot_handle_t *>::iterator it = g_spot_handles.begin ();
         it != g_spot_handles.end (); ++it) {
        zlink::destroy_spot_handle_for_testing (it->second);
        erase_spot_mode_state (it->second);
    }
    g_spot_handles.clear ();
}

int destroy_spot_node_with_handles (void **node_p_)
{
    if (!node_p_ || !*node_p_) {
        errno = EFAULT;
        return -1;
    }

    {
        std::lock_guard<std::mutex> lock (g_spot_probe_mutex);
        std::map<void *, spot_handle_t *>::iterator it =
          g_spot_handles.find (*node_p_);
        if (it != g_spot_handles.end ()) {
            zlink::destroy_spot_handle_for_testing (it->second);
            erase_spot_mode_state (it->second);
            g_spot_handles.erase (it);
        }
        g_sub_probes.erase (*node_p_);
        g_node_probes.erase (*node_p_);
    }

    return zlink_spot_node_destroy (node_p_);
}

void tearDown ()
{
    std::lock_guard<std::mutex> lock (g_spot_probe_mutex);
    clear_spot_probe_map (&g_sub_probes);
    clear_spot_probe_map (&g_node_probes);
    clear_spot_handle_map ();
}

void close_spot_parts (zlink_msg_t *parts_, size_t part_count_)
{
    if (!parts_)
        return;
    for (size_t i = 0; i < part_count_; ++i)
        zlink_msg_close (&parts_[i]);
}

void queued_spot_handler (const zlink_routing_id_t *,
                          const char *topic_,
                          size_t topic_len_,
                          zlink_msg_t *parts_,
                          size_t part_count_,
                          void *userdata_)
{
    queued_spot_probe_t *probe =
      static_cast<queued_spot_probe_t *> (userdata_);
    if (!probe) {
        close_spot_parts (parts_, part_count_);
        return;
    }

    queued_spot_message_t message;
    if (topic_ && topic_len_ > 0)
        message.topic.assign (topic_, topic_len_);
    for (size_t i = 0; i < part_count_; ++i) {
        zlink_msg_t *part = &parts_[i];
        message.parts.push_back (
          std::string (static_cast<const char *> (zlink_msg_data (part)),
                       zlink_msg_size (part)));
    }

    {
        std::lock_guard<std::mutex> lock (probe->mutex);
        probe->messages.push_back (message);
    }
    probe->cv.notify_all ();
    close_spot_parts (parts_, part_count_);
}

queued_spot_probe_t *ensure_queued_spot_probe (void *handle_, bool node_owned_)
{
    std::map<void *, queued_spot_probe_t *> *probe_map =
      node_owned_ ? &g_node_probes : &g_sub_probes;
    {
        std::lock_guard<std::mutex> lock (g_spot_probe_mutex);
        std::map<void *, queued_spot_probe_t *>::iterator it =
          probe_map->find (handle_);
        if (it != probe_map->end ())
            return it->second;
    }

    queued_spot_probe_t *probe = new queued_spot_probe_t ();

    std::lock_guard<std::mutex> lock (g_spot_probe_mutex);
    (*probe_map)[handle_] = probe;
    return probe;
}

void remove_queued_spot_probe (void *handle_, bool node_owned_)
{
    if (!handle_)
        return;

    std::map<void *, queued_spot_probe_t *> *probe_map =
      node_owned_ ? &g_node_probes : &g_sub_probes;

    std::lock_guard<std::mutex> lock (g_spot_probe_mutex);
    std::map<void *, queued_spot_probe_t *>::iterator it =
      probe_map->find (handle_);
    if (it == probe_map->end ())
        return;

    queued_spot_probe_t *probe = it->second;
    probe_map->erase (it);
    delete probe;
}

static bool pop_matching_spot_message_locked (
  queued_spot_probe_t *probe_,
  const char *expected_topic_,
  const char *expected_payload_,
  size_t expected_payload_size_)
{
    if (!probe_)
        return false;

    for (std::vector<queued_spot_message_t>::iterator it =
           probe_->messages.begin ();
         it != probe_->messages.end (); ++it) {
        if (it->topic != expected_topic_ || it->parts.size () != 1
            || it->parts[0].size () != expected_payload_size_
            || memcmp (it->parts[0].data (), expected_payload_,
                       expected_payload_size_)
                 != 0) {
            continue;
        }
        probe_->messages.erase (it);
        return true;
    }
    return false;
}

static bool pop_next_spot_message_locked (queued_spot_probe_t *probe_,
                                          queued_spot_message_t *message_out_)
{
    if (!probe_ || !message_out_ || probe_->messages.empty ())
        return false;

    *message_out_ = probe_->messages.front ();
    probe_->messages.erase (probe_->messages.begin ());
    return true;
}

bool pop_next_spot_message (queued_spot_probe_t *probe_,
                            queued_spot_message_t *message_out_)
{
    if (!probe_ || !message_out_)
        return false;

    std::lock_guard<std::mutex> lock (probe_->mutex);
    return pop_next_spot_message_locked (probe_, message_out_);
}

int bind_spot_node_with_port_seed (void *node_,
                                   const char *prefix_,
                                   int *port_seed_,
                                   char *endpoint_out_)
{
    if (!node_ || !prefix_ || !port_seed_ || !endpoint_out_) {
        errno = EINVAL;
        return -1;
    }
    for (int i = 0; i < 256; ++i) {
        const int port = test_port ((*port_seed_)++);
        snprintf (endpoint_out_, MAX_SOCKET_STRING, "%s%d", prefix_, port);
        if (zlink_spot_node_bind (node_, endpoint_out_) == 0)
            return 0;
    }
    return -1;
}

static bool wait_for_spot_node_status (
  void *node_,
  int role_,
  zlink_monitor_state_mask_t required_flags_,
  uint32_t min_ready_peer_count_,
  int timeout_ms_)
{
    if (!node_)
        return false;

    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::milliseconds (timeout_ms_ > 0 ? timeout_ms_ : 1);

    while (std::chrono::steady_clock::now () < deadline) {
        zlink_spot_node_status_t snapshot;
        if (zlink_spot_node_status_snapshot (node_, &snapshot) == 0) {
            const uint32_t required_peers =
              min_ready_peer_count_ > 0 ? min_ready_peer_count_ : 1U;
            const bool pub_ready = snapshot.local_endpoint[0] != '\0';
            const bool sub_ready =
              snapshot.subject_count > 0
              && (snapshot.ready_subject_count > 0
                  || snapshot.connected_peer_count >= required_peers);

            if (required_flags_ == 0
                && ((role_ == ZLINK_SPOT_ROLE_PUB && pub_ready)
                    || (role_ == ZLINK_SPOT_ROLE_SUB && sub_ready))) {
                return true;
            }

            if ((required_flags_ & ZLINK_MONITOR_STATE_READY) != 0) {
                if (role_ == ZLINK_SPOT_ROLE_PUB && pub_ready)
                    return true;
                if (role_ == ZLINK_SPOT_ROLE_SUB && sub_ready)
                    return true;
            }
        }
        std::this_thread::yield ();
    }

    zlink_spot_node_status_t snapshot;
    if (zlink_spot_node_status_snapshot (node_, &snapshot) != 0)
        return false;

    if (test_debug_enabled ()) {
        fprintf (
          stderr,
          "[test] spot status timeout role=%d flags=%u active=%u connected=%u "
          "subjects=%u ready_subjects=%u state=%d\n",
          role_, static_cast<unsigned int> (required_flags_),
          static_cast<unsigned int> (snapshot.active_peer_count),
          static_cast<unsigned int> (snapshot.connected_peer_count),
          static_cast<unsigned int> (snapshot.subject_count),
          static_cast<unsigned int> (snapshot.ready_subject_count),
          static_cast<int> (snapshot.state));
        fflush (stderr);
    }

    const uint32_t required_peers =
      min_ready_peer_count_ > 0 ? min_ready_peer_count_ : 1U;
    const bool pub_ready = snapshot.local_endpoint[0] != '\0';
    const bool sub_ready =
      snapshot.subject_count > 0
      && (snapshot.ready_subject_count > 0
          || snapshot.connected_peer_count >= required_peers);
    if (required_flags_ == 0)
        return role_ == ZLINK_SPOT_ROLE_PUB ? pub_ready : sub_ready;
    if ((required_flags_ & ZLINK_MONITOR_STATE_READY) == 0)
        return false;
    if (role_ == ZLINK_SPOT_ROLE_PUB)
        return pub_ready;
    if (role_ == ZLINK_SPOT_ROLE_SUB)
        return sub_ready;
    return false;
}

bool wait_for_spot_node_ready_state (
  void *node_,
  int role_,
  zlink_monitor_state_mask_t required_flags_,
  uint32_t min_ready_peer_count_,
  int timeout_ms_)
{
    return wait_for_spot_node_status (node_, role_, required_flags_,
                                      min_ready_peer_count_, timeout_ms_);
}

void make_registry_endpoint (char *endpoint_out_,
                             size_t endpoint_size_,
                             int port_seed_)
{
    snprintf (endpoint_out_, endpoint_size_, "tcp://127.0.0.1:%d",
              test_port (port_seed_));
}

void *create_started_registry_with_port_seed (void *ctx_,
                                              int *port_seed_,
                                              char *pub_endpoint_out_,
                                              size_t pub_size_,
                                              char *router_endpoint_out_,
                                              size_t router_size_)
{
    if (!ctx_ || !port_seed_ || !pub_endpoint_out_ || !router_endpoint_out_
        || pub_size_ == 0 || router_size_ == 0) {
        errno = EINVAL;
        return NULL;
    }

    for (int attempt = 0; attempt < 32; ++attempt) {
        void *registry = zlink_registry_new (ctx_);
        if (!registry)
            return NULL;

        make_registry_endpoint (pub_endpoint_out_, pub_size_, *port_seed_);
        make_registry_endpoint (router_endpoint_out_, router_size_,
                                *port_seed_ + 1);

        if (zlink_registry_bind (registry, pub_endpoint_out_,
                                 router_endpoint_out_)
            == 0) {
            *port_seed_ += 2;
            return registry;
        }

        const int err = zlink_errno ();
        zlink_registry_destroy (&registry);
        if (err != EADDRINUSE) {
            errno = err;
            return NULL;
        }
        *port_seed_ += 2;
    }

    errno = EADDRINUSE;
    return NULL;
}

int connect_discovery_registry_with_retry (void *discovery_,
                                           const char *endpoint_,
                                           int timeout_ms_)
{
    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::milliseconds (timeout_ms_);
    while (std::chrono::steady_clock::now () < deadline) {
        if (zlink_discovery_connect_registry (discovery_, endpoint_) == 0)
            return 0;
        TEST_ASSERT_EQUAL_INT (EAGAIN, zlink_errno ());
        msleep (10);
    }
    errno = EAGAIN;
    return -1;
}

bool wait_for_spot_message (void *spot_sub_,
                            const char *expected_topic_,
                            const char *expected_payload_,
                            size_t expected_payload_size_,
                            int timeout_ms_)
{
    queued_spot_probe_t *probe = ensure_queued_spot_probe (spot_sub_, false);
    if (!probe)
        return false;

    std::unique_lock<std::mutex> lock (probe->mutex);
    if (pop_matching_spot_message_locked (probe, expected_topic_,
                                          expected_payload_,
                                          expected_payload_size_)) {
        return true;
    }
    return probe->cv.wait_for (
      lock, std::chrono::milliseconds (timeout_ms_),
      [probe, expected_topic_, expected_payload_, expected_payload_size_]() {
          return pop_matching_spot_message_locked (
            probe, expected_topic_, expected_payload_, expected_payload_size_);
      });
}

bool wait_for_node_message (void *node_,
                            const char *expected_topic_,
                            const char *expected_payload_,
                            size_t expected_payload_size_,
                            int timeout_ms_)
{
    queued_spot_probe_t *probe = ensure_queued_spot_probe (node_, true);
    if (!probe)
        return false;

    std::unique_lock<std::mutex> lock (probe->mutex);
    if (pop_matching_spot_message_locked (probe, expected_topic_,
                                          expected_payload_,
                                          expected_payload_size_)) {
        return true;
    }
    return probe->cv.wait_for (
      lock, std::chrono::milliseconds (timeout_ms_),
      [probe, expected_topic_, expected_payload_, expected_payload_size_]() {
          return pop_matching_spot_message_locked (
            probe, expected_topic_, expected_payload_, expected_payload_size_);
      });
}

bool wait_for_spot_recv_message (void *spot_sub_,
                                 const char *expected_topic_,
                                 const char *expected_payload_,
                                 size_t expected_payload_size_,
                                 int timeout_ms_)
{
    if (!spot_sub_ || !expected_topic_ || !expected_payload_
        || timeout_ms_ <= 0) {
        return false;
    }

    const size_t expected_topic_size = strlen (expected_topic_);
    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now () + std::chrono::milliseconds (timeout_ms_);

    while (std::chrono::steady_clock::now () < deadline) {
        zlink_msg_t *parts = NULL;
        size_t part_count = 0;
        char topic[256] = {0};
        size_t topic_len = sizeof (topic);
        const int rc = zlink_subscribe (
          spot_sub_, NULL, &parts, &part_count, topic, &topic_len,
          static_cast<zlink_recv_flags_t> (ZLINK_DONTWAIT));
        if (rc == 0) {
            const bool topic_ok =
              topic_len == expected_topic_size
              && memcmp (topic, expected_topic_, expected_topic_size) == 0;
            const bool payload_ok =
              parts && part_count == 1
              && zlink_msg_size (&parts[0]) == expected_payload_size_
              && memcmp (zlink_msg_data (&parts[0]), expected_payload_,
                         expected_payload_size_)
                   == 0;
            if (parts)
                zlink_multipart_close (parts, part_count);
            if (topic_ok && payload_ok)
                return true;
            continue;
        }

        const int err = zlink_errno ();
        if (err != EAGAIN && err != EINTR)
            return false;
        msleep (10);
    }

    return false;
}

void run_spot_peer_tcp_test ()
{
    const char *topic = "tcp:test";
    const char *payload = "tcp-msg";
    const char *warmup_payload = "tcp-warmup";
    const char *bind_prefix = "tcp://127.0.0.1:";

    step_log ("spot peer transport: create ctx");
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    step_log ("spot peer transport: create nodes");
    void *node_a = create_spot_node (ctx, "spot-test");
    TEST_ASSERT_NOT_NULL (node_a);
    void *node_b = create_spot_node (ctx, "spot-test");
    TEST_ASSERT_NOT_NULL (node_b);
    void *pub = create_spot_pub_handle (node_a);
    void *sub = create_spot_sub_handle (node_b);
    TEST_ASSERT_NOT_NULL (pub);
    TEST_ASSERT_NOT_NULL (sub);
    char endpoint_a[MAX_SOCKET_STRING] = {0};
    int port_seed = 29000;

    step_log ("spot peer transport: bind node_a");
    TEST_ASSERT_SUCCESS_ERRNO (bind_spot_node_with_port_seed (
      node_a, bind_prefix, &port_seed, endpoint_a));

    step_log ("spot peer transport: warm node_a default pub");
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_publish (pub, "__warmup__", NULL, 0, 0));

    step_log ("spot peer transport: connect node_b -> node_a");
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_connect_peer (node_b, endpoint_a));

    step_log ("spot peer transport: subscribe node_b");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_subscription (sub, topic));
    step_log ("spot peer transport: warm delivery path");
    {
        const std::chrono::steady_clock::time_point deadline =
          std::chrono::steady_clock::now () + std::chrono::milliseconds (3000);
        bool ready = false;
        while (std::chrono::steady_clock::now () < deadline) {
            TEST_ASSERT_SUCCESS_ERRNO (
              publish_text (&zlink_publish, pub, topic, warmup_payload, 0));
            if (wait_for_spot_recv_message (sub, topic, warmup_payload,
                                            strlen (warmup_payload), 100)) {
                ready = true;
                break;
            }
            msleep (10);
        }
        TEST_ASSERT_TRUE (ready);
    }

    zlink_msg_t parts[1];
    const size_t payload_size = strlen (payload);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&parts[0], payload_size));
    memcpy (zlink_msg_data (&parts[0]), payload, payload_size);

    step_log ("spot peer transport: publish");
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_publish (pub, topic, parts, 1, 0));

    step_log ("spot peer transport: wait delivery");
    TEST_ASSERT_TRUE (
      wait_for_spot_recv_message (sub, topic, payload, payload_size, 2000));

    step_log ("spot peer transport: disconnect peer");
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_disconnect_peer (node_b, endpoint_a));

    step_log ("spot peer transport: detach subscriber");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_unset_subscription (sub, topic));

    step_log ("spot peer transport: destroy nodes");
    TEST_ASSERT_SUCCESS_ERRNO (destroy_spot_node_with_handles (&node_b));
    TEST_ASSERT_SUCCESS_ERRNO (destroy_spot_node_with_handles (&node_a));
    step_log ("spot peer transport: shutdown ctx");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_shutdown (ctx));
    step_log ("spot peer transport: term ctx");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
    step_log ("spot peer transport: term ctx done");
}

void run_spot_peer_reverse_tcp_test ()
{
    const char *topic = "tcp:reverse";
    const char *payload = "reverse-msg";
    const char *bind_prefix = "tcp://127.0.0.1:";

    step_log ("spot peer reverse transport: create ctx");
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    step_log ("spot peer reverse transport: create nodes");
    void *node_a = create_spot_node (ctx, "spot-test");
    TEST_ASSERT_NOT_NULL (node_a);
    void *node_b = create_spot_node (ctx, "spot-test");
    TEST_ASSERT_NOT_NULL (node_b);
    void *sub_a = create_spot_sub_handle (node_a);
    void *pub_b = create_spot_pub_handle (node_b);
    TEST_ASSERT_NOT_NULL (sub_a);
    TEST_ASSERT_NOT_NULL (pub_b);
    char endpoint_a[MAX_SOCKET_STRING] = {0};
    char endpoint_b[MAX_SOCKET_STRING] = {0};
    int port_seed = 29020;

    step_log ("spot peer reverse transport: bind node_a");
    TEST_ASSERT_SUCCESS_ERRNO (bind_spot_node_with_port_seed (
      node_a, bind_prefix, &port_seed, endpoint_a));
    step_log ("spot peer reverse transport: bind node_b");
    TEST_ASSERT_SUCCESS_ERRNO (bind_spot_node_with_port_seed (
      node_b, bind_prefix, &port_seed, endpoint_b));

    step_log ("spot peer reverse transport: warm node_b default pub");
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_publish (pub_b, "__warmup__", NULL, 0, 0));

    step_log ("spot peer reverse transport: connect node_b -> node_a");
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_connect_peer (node_b, endpoint_a));
    step_log ("spot peer reverse transport: connect node_a -> node_b");
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_connect_peer (node_a, endpoint_b));

    step_log ("spot peer reverse transport: subscribe node_a");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_subscription (sub_a, topic));
    TEST_ASSERT_TRUE (wait_for_spot_node_ready_state (
      node_a, ZLINK_SPOT_ROLE_SUB, ZLINK_MONITOR_STATE_READY, 1, 3000));
    TEST_ASSERT_TRUE (wait_for_spot_node_ready_state (
      node_b, ZLINK_SPOT_ROLE_PUB, ZLINK_MONITOR_STATE_READY, 1, 3000));

    zlink_msg_t parts[1];
    const size_t payload_size = strlen (payload);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&parts[0], payload_size));
    memcpy (zlink_msg_data (&parts[0]), payload, payload_size);

    step_log ("spot peer reverse transport: publish");
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_publish (pub_b, topic, parts, 1, 0));

    step_log ("spot peer reverse transport: wait delivery");
    TEST_ASSERT_TRUE (
      wait_for_spot_recv_message (sub_a, topic, payload, payload_size, 2000));

    step_log ("spot peer reverse transport: detach subscriber");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_unset_subscription (sub_a, topic));

    step_log ("spot peer reverse transport: disconnect peers");
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_disconnect_peer (node_b, endpoint_a));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_disconnect_peer (node_a, endpoint_b));

    step_log ("spot peer reverse transport: destroy nodes");
    TEST_ASSERT_SUCCESS_ERRNO (destroy_spot_node_with_handles (&node_b));
    TEST_ASSERT_SUCCESS_ERRNO (destroy_spot_node_with_handles (&node_a));
    step_log ("spot peer reverse transport: shutdown ctx");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_shutdown (ctx));
    step_log ("spot peer reverse transport: term ctx");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
    step_log ("spot peer reverse transport: term ctx done");
}
