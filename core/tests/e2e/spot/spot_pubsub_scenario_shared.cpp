/* SPDX-License-Identifier: MPL-2.0 */

#include "spot_pubsub_scenario_shared.hpp"

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

    zlink_service_monitor_open_options_t opts;
    memset (&opts, 0, sizeof (opts));
    opts.events = ZLINK_SPOT_MONITOR_EVENT_READY_CHANGED
                  | ZLINK_MONITOR_EVENT_PEER_UP
                  | ZLINK_MONITOR_EVENT_PEER_DOWN
                  | ZLINK_MONITOR_EVENT_ERROR;
    void *monitor = zlink_service_monitor_open (spot_, &opts);
    if (!monitor)
        return false;

    const int rc = zlink_monitor_snapshot (monitor, out_);
    zlink_monitor_close (&monitor);
    return rc == 0;
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
    void *node = zlink_spot_node_new (ctx_, service_name_);
    if (!node)
        return NULL;

    queued_spot_probe_t *probe = ensure_queued_spot_probe (node, true);
    if (!probe) {
        zlink_spot_node_destroy (&node);
        errno = ENOMEM;
        return NULL;
    }
    if (zlink_subscribe_handler (node, &queued_spot_handler, probe) != 0) {
        const int err = errno;
        remove_queued_spot_probe (node, true);
        zlink_spot_node_destroy (&node);
        errno = err;
        return NULL;
    }

    const int linger = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_option (node, ZLINK_OPT_LINGER, &linger, sizeof (linger)));
    return node;
}

int publish_text (spot_publish_fn_t publish_fn_,
                  void *handle_,
                  const char *topic_id_,
                  const char *payload_,
                  zlink_send_flags_t flags_)
{
    zlink_msg_t part;
    const size_t size = payload_ ? strlen (payload_) : 0;
    if (zlink_msg_init_size (&part, size) != 0)
        return -1;
    if (size > 0)
        memcpy (zlink_msg_data (&part), payload_, size);
    const int rc = publish_fn_ (handle_, topic_id_, &part, 1, flags_);
    if (rc != 0) {
        const int err = errno;
        zlink_msg_close (&part);
        errno = err;
    }
    return rc;
}

static void cleanup_ipc_endpoint (const char *endpoint_)
{
#if defined(ZLINK_HAVE_IPC) && !defined(_WIN32)
    if (!endpoint_ || strncmp (endpoint_, "ipc://", 6) != 0)
        return;
    unlink (endpoint_ + 6);
#else
    (void) endpoint_;
#endif
}

void *create_spot_pub_handle (void *node_)
{
    void *spot_pub = node_;
    if (!spot_pub)
        return NULL;

    const int linger = 0;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_option (
      spot_pub, ZLINK_OPT_LINGER, &linger, sizeof (linger)));
    return spot_pub;
}

void *create_spot_sub_handle (void *node_, zlink_subscribe_handler_fn handler_)
{
    void *spot_sub = node_;
    if (!spot_sub)
        return NULL;

    queued_spot_probe_t *probe = NULL;
    void *handler_userdata = NULL;
    if (handler_ == &queued_spot_handler) {
        probe = ensure_queued_spot_probe (spot_sub, false);
        if (!probe) {
            errno = ENOMEM;
            return NULL;
        }
        handler_userdata = probe;
    }

    if (handler_
        && zlink_subscribe_handler (spot_sub, handler_, handler_userdata) != 0) {
        const int err = errno;
        if (probe)
            remove_queued_spot_probe (spot_sub, false);
        errno = err;
        return NULL;
    }

    const int linger = 0;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_option (
      spot_sub, ZLINK_OPT_LINGER, &linger, sizeof (linger)));
    return spot_sub;
}

int create_spot_pub_sub (void *node_, void **pub_p, void **sub_p)
{
    if (!pub_p || !sub_p) {
        errno = EINVAL;
        return -1;
    }

    *pub_p = create_spot_pub_handle (node_);
    if (!*pub_p)
        return -1;

    *sub_p = create_spot_sub_handle (node_, &queued_spot_handler);
    if (!*sub_p) {
        *pub_p = NULL;
        return -1;
    }
    return 0;
}

int destroy_spot_pub_sub (void **pub_p, void **sub_p)
{
    if (!pub_p || !sub_p) {
        errno = EFAULT;
        return -1;
    }
    *pub_p = NULL;
    *sub_p = NULL;
    errno = 0;
    return 0;
}

int set_node_pub_option (void *node_,
                         int option_,
                         const void *optval_,
                         size_t optvallen_)
{
    return zlink_set_option (node_, static_cast<zlink_option_t> (option_),
                             optval_, optvallen_);
}

int set_node_sub_option (void *node_,
                         int option_,
                         const void *optval_,
                         size_t optvallen_)
{
    return zlink_set_option (node_, static_cast<zlink_option_t> (option_),
                             optval_, optvallen_);
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

void tearDown ()
{
    std::lock_guard<std::mutex> lock (g_spot_probe_mutex);
    clear_spot_probe_map (&g_sub_probes);
    clear_spot_probe_map (&g_node_probes);
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

static void queued_service_monitor_handler (
  const zlink_service_event_t *event_, void *userdata_)
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

static bool pop_matching_service_event_locked (
  service_monitor_probe_t *probe_,
  uint32_t expected_event_type_,
  const char *endpoint_prefix_,
  const char *subject_,
  int min_value_,
  zlink_service_event_t *event_out_)
{
    if (!probe_)
        return false;

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
        if (subject_ && subject_[0] != '\0') {
            if ((it->detail_flags & ZLINK_EVENT_DETAIL_SUBJECT) == 0)
                continue;
            if (strcmp (it->subject, subject_) != 0)
                continue;
        }
        if (min_value_ >= 0 && static_cast<int> (it->value) < min_value_)
            continue;
        if (event_out_)
            *event_out_ = *it;
        probe_->events.erase (it);
        return true;
    }
    return false;
}

bool wait_for_service_event_match (service_monitor_probe_t *probe_,
                                   uint32_t expected_event_type_,
                                   const char *endpoint_prefix_,
                                   const char *subject_,
                                   int min_value_,
                                   zlink_service_event_t *event_out_,
                                   int timeout_ms_)
{
    if (!probe_)
        return false;

    std::unique_lock<std::mutex> lock (probe_->mutex);
    if (pop_matching_service_event_locked (
          probe_, expected_event_type_, endpoint_prefix_, subject_, min_value_,
          event_out_)) {
        return true;
    }
    return probe_->cv.wait_for (
      lock, std::chrono::milliseconds (timeout_ms_),
      [probe_, expected_event_type_, endpoint_prefix_, subject_, min_value_,
       event_out_]() {
          return pop_matching_service_event_locked (
            probe_, expected_event_type_, endpoint_prefix_, subject_,
            min_value_, event_out_);
      });
}

bool wait_for_service_event (service_monitor_probe_t *probe_,
                             uint32_t expected_event_type_,
                             const char *endpoint_prefix_,
                             int timeout_ms_)
{
    return wait_for_service_event_match (probe_, expected_event_type_,
                                         endpoint_prefix_, NULL, -1, NULL,
                                         timeout_ms_);
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

static bool monitor_snapshot_matches (const zlink_monitor_snapshot_t &snapshot_,
                                      zlink_monitor_state_mask_t flags_,
                                      uint32_t min_ready_peer_count_)
{
    if ((snapshot_.state_flags & flags_) != flags_)
        return false;
    return snapshot_.ready_count >= min_ready_peer_count_;
}

static bool wait_for_monitor_snapshot_state (void *monitor_,
                                             service_monitor_probe_t *probe_,
                                             zlink_monitor_state_mask_t flags_,
                                             uint32_t min_ready_peer_count_,
                                             int timeout_ms_)
{
    if (!monitor_ || !probe_)
        return false;

    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::milliseconds (timeout_ms_ > 0 ? timeout_ms_ : 1);

    while (true) {
        zlink_monitor_snapshot_t snapshot;
        if (zlink_monitor_snapshot (monitor_, &snapshot) == 0
            && monitor_snapshot_matches (snapshot, flags_,
                                         min_ready_peer_count_)) {
            return true;
        }

        std::unique_lock<std::mutex> lock (probe_->mutex);
        const uint64_t observed = probe_->event_count;
        if (std::chrono::steady_clock::now () >= deadline)
            break;
        if (!probe_->cv.wait_until (
              lock, deadline,
              [probe_, observed]() { return probe_->event_count > observed; })) {
            break;
        }
    }

    zlink_monitor_snapshot_t snapshot;
    return zlink_monitor_snapshot (monitor_, &snapshot) == 0
           && monitor_snapshot_matches (snapshot, flags_, min_ready_peer_count_);
}

void *open_spot_monitor_with_probe (void *spot_,
                                    zlink_spot_monitor_event_mask_t events_,
                                    service_monitor_probe_t *probe_)
{
    if (!probe_) {
        errno = EINVAL;
        return NULL;
    }

    {
        std::lock_guard<std::mutex> lock (probe_->mutex);
        probe_->events.clear ();
        probe_->event_count = 0;
    }

    zlink_service_monitor_open_options_t opts;
    memset (&opts, 0, sizeof (opts));
    opts.events = events_;
    void *monitor = zlink_service_monitor_open (spot_, &opts);
    if (!monitor)
        return NULL;
    if (zlink_service_monitor_handler (
          monitor, &queued_service_monitor_handler, probe_)
        != 0) {
        zlink_monitor_close (&monitor);
        return NULL;
    }
    return monitor;
}

void *open_spot_node_monitor_with_probe (
  void *node_,
  int role_,
  zlink_spot_monitor_event_mask_t events_,
  service_monitor_probe_t *probe_)
{
    if (!probe_) {
        errno = EINVAL;
        return NULL;
    }

    {
        std::lock_guard<std::mutex> lock (probe_->mutex);
        probe_->events.clear ();
        probe_->event_count = 0;
    }

    (void) role_;
    zlink_service_monitor_open_options_t opts;
    memset (&opts, 0, sizeof (opts));
    opts.events = events_;
    void *monitor = zlink_service_monitor_open (node_, &opts);
    if (!monitor)
        return NULL;
    if (zlink_service_monitor_handler (
          monitor, &queued_service_monitor_handler, probe_)
        != 0) {
        zlink_monitor_close (&monitor);
        return NULL;
    }
    return monitor;
}

int close_service_monitor_with_probe (void **monitor_)
{
    return zlink_monitor_close (monitor_);
}

bool wait_for_spot_ready_state (void *spot_,
                                zlink_monitor_state_mask_t required_flags_,
                                uint32_t min_ready_peer_count_,
                                int timeout_ms_)
{
    service_monitor_probe_t probe;
    void *monitor = open_spot_monitor_with_probe (
      spot_,
      ZLINK_SPOT_MONITOR_EVENT_READY_CHANGED
      | ZLINK_MONITOR_EVENT_PEER_UP | ZLINK_MONITOR_EVENT_PEER_DOWN
      | ZLINK_MONITOR_EVENT_ERROR,
      &probe);
    if (!monitor)
        return false;

    const bool ready = wait_for_monitor_snapshot_state (
      monitor, &probe, required_flags_, min_ready_peer_count_, timeout_ms_);
    close_service_monitor_with_probe (&monitor);
    return ready;
}

bool wait_for_spot_node_ready_state (
  void *node_,
  int role_,
  zlink_monitor_state_mask_t required_flags_,
  uint32_t min_ready_peer_count_,
  int timeout_ms_)
{
    service_monitor_probe_t probe;
    void *monitor = open_spot_node_monitor_with_probe (
      node_, role_,
      ZLINK_SPOT_MONITOR_EVENT_READY_CHANGED
      | ZLINK_MONITOR_EVENT_PEER_UP | ZLINK_MONITOR_EVENT_PEER_DOWN
      | ZLINK_MONITOR_EVENT_ERROR,
      &probe);
    if (!monitor)
        return false;

    const bool ready = wait_for_monitor_snapshot_state (
      monitor, &probe, required_flags_, min_ready_peer_count_, timeout_ms_);
    close_service_monitor_with_probe (&monitor);
    return ready;
}

bool wait_for_spot_pub_peers (void *pub_, int timeout_ms_)
{
    return wait_for_spot_ready_state (pub_, ZLINK_MONITOR_STATE_SEND_READY, 1,
                                      timeout_ms_);
}

bool wait_for_spot_sub_peers (void *sub_, int timeout_ms_)
{
    return wait_for_spot_ready_state (sub_, ZLINK_MONITOR_STATE_READY, 1,
                                      timeout_ms_);
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

void run_spot_peer_transport_test (peer_transport_t transport_)
{
    const bool is_ipc = transport_ == peer_transport_ipc;
    const bool use_tls =
      transport_ == peer_transport_tls || transport_ == peer_transport_wss;

    const char *topic = NULL;
    const char *payload = NULL;
    const char *bind_prefix = NULL;

    switch (transport_) {
        case peer_transport_ipc:
            topic = "ipc:test";
            payload = "ipc-msg";
            break;
        case peer_transport_tcp:
            topic = "tcp:test";
            payload = "tcp-msg";
            bind_prefix = "tcp://127.0.0.1:";
            break;
        case peer_transport_ws:
            topic = "ws:test";
            payload = "ws-msg";
            bind_prefix = "ws://127.0.0.1:";
            break;
        case peer_transport_tls:
            topic = "tls:test";
            payload = "tls-msg";
            bind_prefix = "tls://127.0.0.1:";
            break;
        case peer_transport_wss:
            topic = "wss:test";
            payload = "wss-msg";
            bind_prefix = "wss://localhost:";
            break;
        default:
            TEST_FAIL_MESSAGE ("Unknown peer transport");
            return;
    }

    tls_test_files_t files;
    if (use_tls)
        files = make_tls_test_files ();

    step_log ("spot peer transport: create ctx");
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    step_log ("spot peer transport: create nodes");
    void *node_a = create_spot_node (ctx, "spot-test");
    TEST_ASSERT_NOT_NULL (node_a);
    void *node_b = create_spot_node (ctx, "spot-test");
    TEST_ASSERT_NOT_NULL (node_b);
    service_monitor_probe_t node_b_monitor_probe;
    void *node_b_monitor = open_spot_node_monitor_with_probe (
      node_b, ZLINK_SPOT_ROLE_SUB,
      ZLINK_SPOT_SUB_FILTER_APPLIED | ZLINK_SPOT_MONITOR_EVENT_SUBSCRIPTION_READY_CHANGED
        | ZLINK_MONITOR_EVENT_CLOSED | ZLINK_MONITOR_EVENT_ERROR,
      &node_b_monitor_probe);
    TEST_ASSERT_NOT_NULL (node_b_monitor);

    char endpoint_a[MAX_SOCKET_STRING] = {0};
    int port_seed = 29000;

    if (is_ipc) {
#if defined(ZLINK_HAVE_IPC)
        make_random_ipc_endpoint (endpoint_a);
        TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_bind (node_a, endpoint_a));
#else
        TEST_IGNORE_MESSAGE ("IPC not compiled");
        return;
#endif
    } else {
        if (use_tls) {
            step_log ("spot peer transport: configure node_a tls server");
            TEST_ASSERT_SUCCESS_ERRNO (
              zlink_set_tls_server (node_a, files.server_cert.c_str (),
                                    files.server_key.c_str (), 0));
        }

        step_log ("spot peer transport: bind node_a");
        TEST_ASSERT_SUCCESS_ERRNO (bind_spot_node_with_port_seed (
          node_a, bind_prefix, &port_seed, endpoint_a));

        if (use_tls) {
            step_log ("spot peer transport: configure node_b tls");
            TEST_ASSERT_SUCCESS_ERRNO (zlink_set_tls_client (
              node_b, files.ca_cert.c_str (), "localhost", 0));
        }
    }

    step_log ("spot peer transport: warm node_a default pub");
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_publish (node_a, "__warmup__", NULL, 0, 0));

    step_log ("spot peer transport: connect node_b -> node_a");
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_connect_peer (node_b, endpoint_a));

    step_log ("spot peer transport: subscribe node_b");
    TEST_ASSERT_NOT_NULL (ensure_queued_spot_probe (node_b, true));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_subscription (node_b, topic));
    TEST_ASSERT_TRUE (wait_for_service_event (
      &node_b_monitor_probe, ZLINK_SPOT_SUB_FILTER_APPLIED, NULL, 3000));
    TEST_ASSERT_TRUE (wait_for_service_event (
      &node_b_monitor_probe, ZLINK_SPOT_MONITOR_EVENT_SUBSCRIPTION_READY_CHANGED, endpoint_a,
      use_tls ? 10000 : 3000));

    zlink_msg_t parts[1];
    const size_t payload_size = strlen (payload);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&parts[0], payload_size));
    memcpy (zlink_msg_data (&parts[0]), payload, payload_size);

    step_log ("spot peer transport: publish");
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_publish (node_a, topic, parts, 1, 0));

    step_log ("spot peer transport: wait delivery");
    TEST_ASSERT_TRUE (
      wait_for_node_message (node_b, topic, payload, payload_size, 2000));

    step_log ("spot peer transport: disconnect peer");
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_disconnect_peer (node_b, endpoint_a));

    step_log ("spot peer transport: detach subscriber");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_unset_subscription (node_b, topic));

    if (use_tls)
        msleep (200);

    step_log ("spot peer transport: destroy nodes");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node_b));
    TEST_ASSERT_TRUE (wait_for_service_event (
      &node_b_monitor_probe, ZLINK_MONITOR_EVENT_CLOSED, NULL,
      use_tls ? 10000 : 3000));
    TEST_ASSERT_SUCCESS_ERRNO (
      close_service_monitor_with_probe (&node_b_monitor));
    msleep (use_tls ? 50 : 10);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node_a));
    msleep (use_tls ? 100 : 50);

    if (use_tls)
        msleep (200);
    step_log ("spot peer transport: shutdown ctx");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_shutdown (ctx));
    if (use_tls)
        msleep (50);
    step_log ("spot peer transport: term ctx");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
    if (is_ipc)
        cleanup_ipc_endpoint (endpoint_a);
    step_log ("spot peer transport: term ctx done");

    if (use_tls) {
        step_log ("spot peer transport: cleanup tls files");
        cleanup_tls_test_files (files);
    }
}
