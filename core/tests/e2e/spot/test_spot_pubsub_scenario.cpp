/* SPDX-License-Identifier: MPL-2.0 */

#include "../../testutil_unity.hpp"
#include "../../testutil.hpp"
#include "../../../src/services/spot/spot_dispatch_internal.hpp"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <atomic>
#include <condition_variable>
#include <map>
#include <mutex>
#include <string>
#include <vector>
#include <chrono>
#include <thread>

#if defined(_WIN32)
#include <direct.h>
#include <windows.h>
#else
#include <sys/resource.h>
#include <sys/stat.h>
#endif

static bool should_run_spot_e2e_smoke_test (const char *name_)
{
    static const char *const smoke_cases[] = {"test_spot_peer_tcp",
                                              "test_spot_multi_publisher",
                                              "test_spot_node_direct_local_and_child_interop",
                                              "test_spot_node_discovery_direct_and_child_interop",
                                              "test_spot_pub_async_mode_local_delivery"};
    for (size_t i = 0; i < sizeof (smoke_cases) / sizeof (smoke_cases[0]); ++i) {
        if (strcmp (smoke_cases[i], name_) == 0)
            return true;
    }
    return false;
}

static bool should_run_spot_test (const char *name_)
{
    const char *selected = getenv ("ZLINK_TEST_CASE");
    if (selected && *selected)
        return strcmp (selected, name_) == 0;

    const char *suite_mode = getenv ("ZLINK_TEST_SUITE_MODE");
    if (suite_mode && strcmp (suite_mode, "e2e") == 0)
        return should_run_spot_e2e_smoke_test (name_);

    return true;
}

static bool test_debug_enabled ()
{
    return getenv ("ZLINK_TEST_DEBUG") != NULL;
}

static void step_log (const char *msg_)
{
    if (test_debug_enabled ()) {
        fprintf (stderr, "[test] %s\n", msg_ ? msg_ : "");
        fflush (stderr);
    }
}

static void ignore_spot_handler (const zlink_routing_id_t *,
                                 const char *,
                                 size_t,
                                 zlink_msg_t *parts_,
                                 size_t part_count_)
{
    if (!parts_)
        return;
    for (size_t i = 0; i < part_count_; ++i)
        zlink_msg_close (&parts_[i]);
}

static void queued_spot_handler (const zlink_routing_id_t *,
                                 const char *topic_,
                                 size_t topic_len_,
                                 zlink_msg_t *parts_,
                                 size_t part_count_);

static void *create_spot_node (void *ctx_, const char *service_name_)
{
    void *node =
      zlink_spot_node_new (ctx_, service_name_, &queued_spot_handler);
    if (!node)
        return NULL;

    const int linger = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_set_pub_option (node, ZLINK_SPOT_PUB_OPT_LINGER,
                                      &linger, sizeof (linger)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_set_sub_option (node, ZLINK_SPOT_SUB_OPT_LINGER,
                                      &linger, sizeof (linger)));
    return node;
}

typedef int (*spot_publish_fn_t) (void *,
                                  const char *,
                                  zlink_msg_t *,
                                  size_t,
                                  zlink_send_flags_t);

static int publish_text (spot_publish_fn_t publish_fn_,
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

static void *create_spot_handle (void *node_, zlink_spot_handler_fn handler_)
{
    void *spot = zlink_spot_new (node_, handler_);
    if (!spot)
        return NULL;

    const int linger = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_set_pub_option (spot, ZLINK_SPOT_PUB_OPT_LINGER, &linger,
                                 sizeof (linger)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_set_sub_option (spot, ZLINK_SPOT_SUB_OPT_LINGER, &linger,
                                 sizeof (linger)));
    return spot;
}

static int create_spot_pub_sub (void *node, void **pub_p, void **sub_p)
{
    if (!pub_p || !sub_p) {
        errno = EINVAL;
        return -1;
    }

    *pub_p = create_spot_handle (node, &ignore_spot_handler);
    if (!*pub_p)
        return -1;

    *sub_p = create_spot_handle (node, &queued_spot_handler);
    if (!*sub_p) {
        void *pub = *pub_p;
        *pub_p = NULL;
        zlink_spot_destroy (&pub);
        return -1;
    }
    return 0;
}

static int destroy_spot_pub_sub (void **pub_p, void **sub_p)
{
    if (!pub_p || !sub_p) {
        errno = EFAULT;
        return -1;
    }
    int rc_pub = zlink_spot_destroy (pub_p);
    int rc_sub = zlink_spot_destroy (sub_p);
    return (rc_pub == 0 && rc_sub == 0) ? 0 : -1;
}

static int set_node_pub_option (void *node_,
                                zlink_spot_pub_option_t option_,
                                const void *optval_,
                                size_t optvallen_)
{
    return zlink_spot_node_set_pub_option (node_, option_, optval_, optvallen_);
}

static int set_node_sub_option (void *node_,
                                zlink_spot_sub_option_t option_,
                                const void *optval_,
                                size_t optvallen_)
{
    return zlink_spot_node_set_sub_option (node_, option_, optval_, optvallen_);
}

struct service_monitor_probe_t;

static bool wait_for_spot_message (void *spot_sub_,
                                   const char *expected_topic_,
                                   const char *expected_payload_,
                                   size_t expected_payload_size_,
                                   int timeout_ms_);
static bool wait_for_node_message (void *node_,
                                   const char *expected_topic_,
                                   const char *expected_payload_,
                                   size_t expected_payload_size_,
                                   int timeout_ms_);
static bool wait_for_service_event (service_monitor_probe_t *probe_,
                                    uint32_t expected_event_type_,
                                    const char *endpoint_prefix_,
                                    int timeout_ms_);

void setUp ()
{
}

struct queued_spot_message_t
{
    std::string topic;
    std::vector<std::string> parts;
};

struct queued_spot_probe_t
{
    std::mutex mutex;
    std::condition_variable cv;
    std::vector<queued_spot_message_t> messages;
};

struct service_monitor_probe_t
{
    std::mutex mutex;
    std::condition_variable cv;
    std::vector<zlink_service_event_t> events;
};

struct callback_probe_t;

static std::mutex g_spot_probe_mutex;
static std::map<void *, queued_spot_probe_t *> g_sub_probes;
static std::map<void *, queued_spot_probe_t *> g_node_probes;
static std::map<void *, callback_probe_t *> g_callback_probes;
static service_monitor_probe_t *g_service_monitor_probe = NULL;

static void clear_spot_probe_map (std::map<void *, queued_spot_probe_t *> *map_)
{
    map_->clear ();
}

void tearDown ()
{
    std::lock_guard<std::mutex> lock (g_spot_probe_mutex);
    clear_spot_probe_map (&g_sub_probes);
    clear_spot_probe_map (&g_node_probes);
    g_callback_probes.clear ();
    g_service_monitor_probe = NULL;
}

static queued_spot_probe_t *find_queued_spot_probe_for_current_dispatch ()
{
    void *handle = zlink::current_spot_dispatch_handle ();
    if (!handle)
        return NULL;

    std::map<void *, queued_spot_probe_t *> *probe_map =
      zlink::current_spot_dispatch_is_node () ? &g_node_probes : &g_sub_probes;
    std::lock_guard<std::mutex> lock (g_spot_probe_mutex);
    std::map<void *, queued_spot_probe_t *>::iterator it =
      probe_map->find (handle);
    return it != probe_map->end () ? it->second : NULL;
}

static callback_probe_t *find_callback_probe_for_current_dispatch ()
{
    void *handle = zlink::current_spot_dispatch_handle ();
    if (!handle)
        return NULL;

    std::lock_guard<std::mutex> lock (g_spot_probe_mutex);
    std::map<void *, callback_probe_t *>::iterator it =
      g_callback_probes.find (handle);
    return it != g_callback_probes.end () ? it->second : NULL;
}

static void close_spot_parts (zlink_msg_t *parts_, size_t part_count_)
{
    if (!parts_)
        return;
    for (size_t i = 0; i < part_count_; ++i)
        zlink_msg_close (&parts_[i]);
}

static void queued_spot_handler (const zlink_routing_id_t *,
                                 const char *topic_,
                                 size_t topic_len_,
                                 zlink_msg_t *parts_,
                                 size_t part_count_)
{
    queued_spot_probe_t *probe = find_queued_spot_probe_for_current_dispatch ();
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
  const zlink_service_event_t *event_)
{
    service_monitor_probe_t *probe = g_service_monitor_probe;
    if (!probe || !event_)
        return;

    {
        std::lock_guard<std::mutex> lock (probe->mutex);
        probe->events.push_back (*event_);
    }
    probe->cv.notify_all ();
}

static queued_spot_probe_t *ensure_queued_spot_probe (
  void *handle_,
  bool node_owned_)
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

static void remove_queued_spot_probe (void *handle_, bool node_owned_)
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

    probe_map->erase (it);
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

static bool pop_matching_spot_message (queued_spot_probe_t *probe_,
                                       const char *expected_topic_,
                                       const char *expected_payload_,
                                       size_t expected_payload_size_)
{
    if (!probe_)
        return false;

    std::lock_guard<std::mutex> lock (probe_->mutex);
    return pop_matching_spot_message_locked (
      probe_, expected_topic_, expected_payload_, expected_payload_size_);
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

static bool pop_matching_service_event (service_monitor_probe_t *probe_,
                                        uint32_t expected_event_type_,
                                        const char *endpoint_prefix_)
{
    if (!probe_)
        return false;

    std::lock_guard<std::mutex> lock (probe_->mutex);
    return pop_matching_service_event_locked (
      probe_, expected_event_type_, endpoint_prefix_, NULL, -1, NULL);
}

static bool wait_for_service_event_match (service_monitor_probe_t *probe_,
                                          uint32_t expected_event_type_,
                                          const char *endpoint_prefix_,
                                          const char *subject_,
                                          int min_value_,
                                          zlink_service_event_t *event_out_,
                                          int timeout_ms_);

static bool pop_next_spot_message_locked (queued_spot_probe_t *probe_,
                                          queued_spot_message_t *message_out_)
{
    if (!probe_ || !message_out_ || probe_->messages.empty ())
        return false;

    *message_out_ = probe_->messages.front ();
    probe_->messages.erase (probe_->messages.begin ());
    return true;
}

static bool pop_next_spot_message (queued_spot_probe_t *probe_,
                                   queued_spot_message_t *message_out_)
{
    if (!probe_ || !message_out_)
        return false;

    std::lock_guard<std::mutex> lock (probe_->mutex);
    return pop_next_spot_message_locked (probe_, message_out_);
}

static int bind_spot_node_with_port_seed (void *node_,
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

static bool wait_for_spot_pub_peers (void *pub_, int timeout_ms_)
{
    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::milliseconds (timeout_ms_ > 0 ? timeout_ms_ : 1);

    while (std::chrono::steady_clock::now () < deadline) {
        size_t count = 0;
        if (zlink_spot_peers_pub (pub_, NULL, &count) == 0 && count > 0)
            return true;
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }

    size_t count = 0;
    return zlink_spot_peers_pub (pub_, NULL, &count) == 0 && count > 0;
}

static bool wait_for_spot_sub_peers (void *sub_, int timeout_ms_)
{
    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::milliseconds (timeout_ms_ > 0 ? timeout_ms_ : 1);

    while (std::chrono::steady_clock::now () < deadline) {
        size_t count = 0;
        if (zlink_spot_peers_sub (sub_, NULL, &count) == 0 && count > 0)
            return true;
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }

    size_t count = 0;
    return zlink_spot_peers_sub (sub_, NULL, &count) == 0 && count > 0;
}

static void make_registry_endpoint (char *endpoint_out_,
                                    size_t endpoint_size_,
                                    int port_seed_)
{
    snprintf (endpoint_out_, endpoint_size_, "tcp://127.0.0.1:%d",
              test_port (port_seed_));
}

static void *create_started_registry_with_port_seed (void *ctx_,
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

        if (zlink_registry_set_endpoints (registry, pub_endpoint_out_,
                                          router_endpoint_out_)
              == 0
            && zlink_registry_start (registry) == 0) {
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

static int connect_discovery_registry_with_retry (void *discovery_,
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

enum peer_transport_t
{
    peer_transport_ipc = 0,
    peer_transport_tcp,
    peer_transport_ws,
    peer_transport_tls,
    peer_transport_wss
};

static void run_spot_peer_transport_test (peer_transport_t transport_)
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
    g_service_monitor_probe = &node_b_monitor_probe;
    void *node_b_monitor = zlink_spot_node_monitor_open (
      node_b, ZLINK_SPOT_ROLE_SUB,
      ZLINK_SPOT_SUB_FILTER_APPLIED | ZLINK_SPOT_SUB_SUBSCRIPTION_READY
        | ZLINK_MONITOR_EVENT_CLOSED | ZLINK_MONITOR_EVENT_ERROR,
      &queued_service_monitor_handler);
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
              zlink_spot_node_set_tls_server (node_a, files.server_cert.c_str (),
                                              files.server_key.c_str ()));
        }

        step_log ("spot peer transport: bind node_a");
        TEST_ASSERT_SUCCESS_ERRNO (bind_spot_node_with_port_seed (
          node_a, bind_prefix, &port_seed, endpoint_a));

        if (use_tls) {
            step_log ("spot peer transport: configure node_b tls");
            TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_set_tls_client (
              node_b, files.ca_cert.c_str (), "localhost", 0));
        }
    }

    step_log ("spot peer transport: warm node_a default pub");
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_publish (node_a, "__warmup__", NULL, 0, 0));

    step_log ("spot peer transport: connect node_b -> node_a");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_connect_peer_pub (node_b, endpoint_a));

    step_log ("spot peer transport: subscribe node_b");
    TEST_ASSERT_NOT_NULL (ensure_queued_spot_probe (node_b, true));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_subscribe (node_b, topic));
    TEST_ASSERT_TRUE (wait_for_service_event (
      &node_b_monitor_probe, ZLINK_SPOT_SUB_FILTER_APPLIED, NULL, 3000));
    TEST_ASSERT_TRUE (wait_for_service_event (
      &node_b_monitor_probe, ZLINK_SPOT_SUB_SUBSCRIPTION_READY, endpoint_a,
      use_tls ? 10000 : 3000));

    zlink_msg_t parts[1];
    const size_t payload_size = strlen (payload);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&parts[0], payload_size));
    memcpy (zlink_msg_data (&parts[0]), payload, payload_size);

    step_log ("spot peer transport: publish");
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_publish (node_a, topic, parts, 1, 0));

    step_log ("spot peer transport: wait delivery");
    TEST_ASSERT_TRUE (
      wait_for_node_message (node_b, topic, payload, payload_size, 2000));

    step_log ("spot peer transport: disconnect peer");
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_disconnect_peer_pub (node_b, endpoint_a));

    step_log ("spot peer transport: detach subscriber");
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_unsubscribe_filter (node_b, topic));

    if (use_tls)
        msleep (200);

    step_log ("spot peer transport: destroy nodes");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node_b));
    TEST_ASSERT_TRUE (wait_for_service_event (
      &node_b_monitor_probe, ZLINK_MONITOR_EVENT_CLOSED, NULL,
      use_tls ? 10000 : 3000));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_service_monitor_close (&node_b_monitor));
    g_service_monitor_probe = NULL;
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

static void test_spot_peer_ipc ()
{
#if !defined(ZLINK_HAVE_IPC)
    TEST_IGNORE_MESSAGE ("IPC not compiled");
    return;
#else
    if (!zlink_has ("ipc")) {
        TEST_IGNORE_MESSAGE ("IPC not available");
        return;
    }
    run_spot_peer_transport_test (peer_transport_ipc);
#endif
}

static void test_spot_peer_tcp ()
{
    if (!zlink_has ("tcp")) {
        TEST_IGNORE_MESSAGE ("TCP not available");
        return;
    }
    run_spot_peer_transport_test (peer_transport_tcp);
}

static void test_spot_peer_ws ()
{
    if (!zlink_has ("ws")) {
        TEST_IGNORE_MESSAGE ("WS not available");
        return;
    }
    run_spot_peer_transport_test (peer_transport_ws);
}

static void test_spot_peer_tls ()
{
    if (!zlink_has ("tls")) {
        TEST_IGNORE_MESSAGE ("TLS not available");
        return;
    }
    run_spot_peer_transport_test (peer_transport_tls);
}

static void test_spot_peer_wss ()
{
    if (!zlink_has ("wss")) {
        TEST_IGNORE_MESSAGE ("WSS not available");
        return;
    }
    run_spot_peer_transport_test (peer_transport_wss);
}

static void test_spot_unified_wss_subscription_ready_first_delivery ()
{
    if (!zlink_has ("wss")) {
        TEST_IGNORE_MESSAGE ("WSS not available");
        return;
    }

    const int iteration_count = 16;
    const char *const topic = "wss:ready:first-delivery";
    const char *const payload = "wss-ready";
    tls_test_files_t files = make_tls_test_files ();
    int port_seed = 35200;

    for (int iteration = 0; iteration < iteration_count; ++iteration) {
        step_log ("spot unified wss ready delivery: create ctx");
        void *ctx = zlink_ctx_new ();
        TEST_ASSERT_NOT_NULL (ctx);

        void *pub_node =
          zlink_spot_node_new (ctx, "perf-spot", &ignore_spot_handler);
        void *sub_node =
          zlink_spot_node_new (ctx, "perf-spot-client", &ignore_spot_handler);
        TEST_ASSERT_NOT_NULL (pub_node);
        TEST_ASSERT_NOT_NULL (sub_node);

        const int linger = 0;
        const int sndhwm = 1000;
        const int rcvhwm = 1000;
        const int sndtimeo_ms = 200;
        TEST_ASSERT_SUCCESS_ERRNO (
          zlink_spot_node_set_pub_option (pub_node, ZLINK_SPOT_PUB_OPT_LINGER,
                                          &linger, sizeof (linger)));
        TEST_ASSERT_SUCCESS_ERRNO (
          zlink_spot_node_set_pub_option (pub_node, ZLINK_SPOT_PUB_OPT_SNDHWM,
                                          &sndhwm, sizeof (sndhwm)));
        TEST_ASSERT_SUCCESS_ERRNO (
          zlink_spot_node_set_pub_option (pub_node, ZLINK_SPOT_PUB_OPT_SNDTIMEO,
                                          &sndtimeo_ms,
                                          sizeof (sndtimeo_ms)));
        TEST_ASSERT_SUCCESS_ERRNO (
          zlink_spot_node_set_sub_option (sub_node, ZLINK_SPOT_SUB_OPT_LINGER,
                                          &linger, sizeof (linger)));
        TEST_ASSERT_SUCCESS_ERRNO (
          zlink_spot_node_set_sub_option (sub_node, ZLINK_SPOT_SUB_OPT_RCVHWM,
                                          &rcvhwm, sizeof (rcvhwm)));

        TEST_ASSERT_SUCCESS_ERRNO (
          zlink_spot_node_set_tls_server (pub_node, files.server_cert.c_str (),
                                          files.server_key.c_str ()));
        TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_set_tls_client (
          sub_node, files.ca_cert.c_str (), "localhost", 0));

        step_log ("spot unified wss ready delivery: create handles");
        void *pub = zlink_spot_new (pub_node, &ignore_spot_handler);
        void *sub = zlink_spot_new (sub_node, &queued_spot_handler);
        TEST_ASSERT_NOT_NULL (pub);
        TEST_ASSERT_NOT_NULL (sub);
        TEST_ASSERT_NOT_NULL (ensure_queued_spot_probe (sub, false));

        service_monitor_probe_t monitor_probe;
        g_service_monitor_probe = &monitor_probe;
        void *sub_monitor = zlink_spot_monitor_open (
          sub,
          ZLINK_SPOT_ROLE_SUB,
          ZLINK_SPOT_SUB_FILTER_APPLIED
            | ZLINK_SPOT_SUB_DELIVERY_READY_CHANGED
            | ZLINK_MONITOR_EVENT_ERROR,
          &queued_service_monitor_handler);
        TEST_ASSERT_NOT_NULL (sub_monitor);

        char endpoint[MAX_SOCKET_STRING] = {0};
        step_log ("spot unified wss ready delivery: bind/connect");
        TEST_ASSERT_SUCCESS_ERRNO (bind_spot_node_with_port_seed (
          pub_node, "wss://localhost:", &port_seed, endpoint));
        TEST_ASSERT_SUCCESS_ERRNO (
          zlink_spot_node_connect_peer_pub (sub_node, endpoint));

        step_log ("spot unified wss ready delivery: subscribe and wait");
        TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_subscribe (sub, topic));
        TEST_ASSERT_TRUE (wait_for_spot_pub_peers (pub, 5000));
        TEST_ASSERT_TRUE (wait_for_spot_sub_peers (sub, 5000));
        TEST_ASSERT_TRUE (wait_for_service_event_match (
          &monitor_probe, ZLINK_SPOT_SUB_FILTER_APPLIED, NULL, topic, -1, NULL,
          3000));
        TEST_ASSERT_TRUE (wait_for_service_event_match (
          &monitor_probe, ZLINK_SPOT_SUB_DELIVERY_READY_CHANGED, endpoint,
          topic, 1, NULL, 10000));

        step_log ("spot unified wss ready delivery: publish immediately");
        TEST_ASSERT_SUCCESS_ERRNO (
          publish_text (&zlink_spot_publish, pub, topic, payload, 0));
        TEST_ASSERT_TRUE (
          wait_for_spot_message (sub, topic, payload, strlen (payload), 3000));

        g_service_monitor_probe = NULL;
        TEST_ASSERT_SUCCESS_ERRNO (zlink_service_monitor_close (&sub_monitor));
        remove_queued_spot_probe (sub, false);
        TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&sub));
        TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&pub));
        TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&sub_node));
        TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&pub_node));
        TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_shutdown (ctx));
        TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
    }

    cleanup_tls_test_files (files);
}

static void test_spot_multi_publisher ()
{
    step_log ("multi_publisher: create ctx");
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    step_log ("multi_publisher: create nodes");
    void *node_a = create_spot_node (ctx, "spot-test");
    TEST_ASSERT_NOT_NULL (node_a);
    void *node_b = create_spot_node (ctx, "spot-test");
    TEST_ASSERT_NOT_NULL (node_b);
    void *node_c = create_spot_node (ctx, "spot-test");
    TEST_ASSERT_NOT_NULL (node_c);

    step_log ("multi_publisher: bind nodes");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_bind (node_a, "inproc://pub-a"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_bind (node_b, "inproc://pub-b"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_bind (node_c, "inproc://sub-c"));

    step_log ("multi_publisher: connect peers");
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_connect_peer_pub (node_c, "inproc://pub-a"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_connect_peer_pub (node_c, "inproc://pub-b"));

    void *spot_c_pub = NULL;
    void *spot_c_sub = NULL;
    step_log ("multi_publisher: create node_c child handles");
    TEST_ASSERT_SUCCESS_ERRNO (create_spot_pub_sub (node_c, &spot_c_pub, &spot_c_sub));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_subscribe (spot_c_sub, "multi:topic"));
    TEST_ASSERT_NOT_NULL (ensure_queued_spot_probe (spot_c_sub, false));

    msleep (250);

    void *spot_a_pub = NULL;
    void *spot_a_sub = NULL;
    step_log ("multi_publisher: create node_a child handles");
    TEST_ASSERT_SUCCESS_ERRNO (create_spot_pub_sub (node_a, &spot_a_pub, &spot_a_sub));
    void *spot_b_pub = NULL;
    void *spot_b_sub = NULL;
    step_log ("multi_publisher: create node_b child handles");
    TEST_ASSERT_SUCCESS_ERRNO (create_spot_pub_sub (node_b, &spot_b_pub, &spot_b_sub));
    msleep (250);

    zlink_msg_t parts_a[1];
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&parts_a[0], 5));
    memcpy (zlink_msg_data (&parts_a[0]), "from-a", 5);

    zlink_msg_t parts_b[1];
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&parts_b[0], 6));
    memcpy (zlink_msg_data (&parts_b[0]), "from-b", 6);

    step_log ("multi_publisher: publish");
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_publish (spot_a_pub, "multi:topic", parts_a, 1, 0));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_publish (spot_b_pub, "multi:topic", parts_b, 1, 0));

    step_log ("multi_publisher: wait receives");
    TEST_ASSERT_TRUE (
      wait_for_spot_message (spot_c_sub, "multi:topic", "from-a", 5, 2000));
    TEST_ASSERT_TRUE (
      wait_for_spot_message (spot_c_sub, "multi:topic", "from-b", 6, 2000));

    step_log ("multi_publisher: disconnect peers");
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_unsubscribe (spot_c_sub, "multi:topic"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_disconnect_peer_pub (node_c, "inproc://pub-a"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_disconnect_peer_pub (node_c, "inproc://pub-b"));
    msleep (50);

    step_log ("multi_publisher: destroy child handles");
    TEST_ASSERT_SUCCESS_ERRNO (destroy_spot_pub_sub (&spot_a_pub, &spot_a_sub));
    TEST_ASSERT_SUCCESS_ERRNO (destroy_spot_pub_sub (&spot_b_pub, &spot_b_sub));
    TEST_ASSERT_SUCCESS_ERRNO (destroy_spot_pub_sub (&spot_c_pub, &spot_c_sub));
    step_log ("multi_publisher: destroy nodes");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node_a));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node_b));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node_c));
    step_log ("multi_publisher: term ctx");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

struct callback_probe_t
{
    void *sub;
    std::atomic<int> calls;
    std::atomic<int> payload_ok;
    std::atomic<int> topic_ok;
    std::atomic<int> replace_inside_rc;
    std::atomic<int> replace_inside_done;
    std::atomic<int> replacement_calls;
};

struct direct_spot_probe_t
{
    std::mutex mutex;
    int calls;
    bool saw_source_rid;
    zlink_routing_id_t source_rid;
    std::string topic;
    std::string payload;
};

static direct_spot_probe_t *g_direct_spot_probe = NULL;

static void direct_spot_handler (const zlink_routing_id_t *source_rid_,
                                 const char *topic_,
                                 size_t topic_len_,
                                 zlink_msg_t *parts_,
                                 size_t part_count_)
{
    if (!g_direct_spot_probe || part_count_ != 1) {
        close_spot_parts (parts_, part_count_);
        return;
    }

    std::lock_guard<std::mutex> lock (g_direct_spot_probe->mutex);
    ++g_direct_spot_probe->calls;
    g_direct_spot_probe->saw_source_rid = source_rid_ && source_rid_->size > 0;
    if (g_direct_spot_probe->saw_source_rid)
        g_direct_spot_probe->source_rid = *source_rid_;
    g_direct_spot_probe->topic.assign (topic_, topic_len_);
    g_direct_spot_probe->payload.assign (
      static_cast<const char *> (zlink_msg_data (&parts_[0])),
      zlink_msg_size (&parts_[0]));
    close_spot_parts (parts_, part_count_);
}

static void spot_sub_probe_handler (const zlink_routing_id_t *,
                                    const char *topic_,
                                    size_t topic_len_,
                                    zlink_msg_t *parts_,
                                    size_t part_count_)
{
    callback_probe_t *probe = find_callback_probe_for_current_dispatch ();
    if (!probe) {
        close_spot_parts (parts_, part_count_);
        return;
    }

    probe->calls.fetch_add (1);
    if (topic_ && topic_len_ == 14 && memcmp (topic_, "zone:auto:test", 14) == 0)
        probe->topic_ok.store (1);
    if (parts_ && part_count_ == 1
        && zlink_msg_size (&parts_[0]) == 4
        && memcmp (zlink_msg_data (&parts_[0]), "ping", 4)
             == 0)
        probe->payload_ok.store (1);
    close_spot_parts (parts_, part_count_);
}

static void spot_sub_replacement_handler (const zlink_routing_id_t *source_rid_,
                                          const char *topic_,
                                          size_t topic_len_,
                                          zlink_msg_t *parts_,
                                          size_t part_count_)
{
    spot_sub_probe_handler (source_rid_, topic_, topic_len_, parts_,
                            part_count_);

    callback_probe_t *probe = find_callback_probe_for_current_dispatch ();
    if (probe)
        probe->replacement_calls.fetch_add (1);
}

static void spot_sub_replace_inside_handler (const zlink_routing_id_t *,
                                             const char *topic_,
                                             size_t topic_len_,
                                             zlink_msg_t *parts_,
                                             size_t part_count_)
{
    (void) topic_;
    (void) topic_len_;
    (void) parts_;
    (void) part_count_;

    callback_probe_t *probe = find_callback_probe_for_current_dispatch ();
    if (!probe)
        return;

    const int call_idx = probe->calls.fetch_add (1);
    if (call_idx == 0) {
        probe->replace_inside_rc.store (-1);
        probe->replace_inside_done.store (1);
    }
    close_spot_parts (parts_, part_count_);
}

static bool wait_until_counter_at_least (std::atomic<int> *value_,
                                         int expected_,
                                         int timeout_ms_)
{
    const int sleep_ms_step = 10;
    const int max_attempts = timeout_ms_ / sleep_ms_step;
    for (int i = 0; i < max_attempts; ++i) {
        if (value_->load () >= expected_)
            return true;
        msleep (sleep_ms_step);
    }
    return value_->load () >= expected_;
}

static void test_spot_sub_handler_basic ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    void *node = create_spot_node (ctx, "spot-test");
    TEST_ASSERT_NOT_NULL (node);

    void *pub = create_spot_handle (node, &ignore_spot_handler);
    void *sub = create_spot_handle (node, &spot_sub_probe_handler);
    TEST_ASSERT_NOT_NULL (pub);
    TEST_ASSERT_NOT_NULL (sub);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_subscribe (sub, "zone:auto:test"));
    msleep (50);

    callback_probe_t probe;
    probe.sub = sub;
    probe.calls.store (0);
    probe.payload_ok.store (0);
    probe.topic_ok.store (0);
    probe.replace_inside_rc.store (-1);
    probe.replace_inside_done.store (0);
    probe.replacement_calls.store (0);

    {
        std::lock_guard<std::mutex> lock (g_spot_probe_mutex);
        g_callback_probes[sub] = &probe;
    }

    zlink_msg_t part;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&part, 4));
    memcpy (zlink_msg_data (&part), "ping", 4);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_publish (pub, "zone:auto:test", &part, 1, 0));

    TEST_ASSERT_TRUE (wait_until_counter_at_least (&probe.calls, 1, 1000));
    TEST_ASSERT_EQUAL_INT (1, probe.payload_ok.load ());
    TEST_ASSERT_EQUAL_INT (1, probe.topic_ok.load ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&sub));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&pub));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

static void test_spot_recv_callback_isolated_by_handle ()
{
    if (!zlink_has ("tcp")) {
        TEST_IGNORE_MESSAGE ("TCP not available");
        return;
    }

    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *pub_node = create_spot_node (ctx, "spot-handle-isolation");
    void *sub_node = create_spot_node (ctx, "spot-handle-isolation");
    TEST_ASSERT_NOT_NULL (pub_node);
    TEST_ASSERT_NOT_NULL (sub_node);

    char pub_endpoint[MAX_SOCKET_STRING];
    int port_seed = 33140;
    TEST_ASSERT_SUCCESS_ERRNO (bind_spot_node_with_port_seed (
      pub_node, "tcp://127.0.0.1:", &port_seed, pub_endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_connect_peer_pub (sub_node, pub_endpoint));

    void *pub = create_spot_handle (pub_node, &ignore_spot_handler);
    void *sub_a = create_spot_handle (sub_node, &queued_spot_handler);
    void *sub_b = create_spot_handle (sub_node, &queued_spot_handler);
    TEST_ASSERT_NOT_NULL (pub);
    TEST_ASSERT_NOT_NULL (sub_a);
    TEST_ASSERT_NOT_NULL (sub_b);

    queued_spot_probe_t *probe_a = ensure_queued_spot_probe (sub_a, false);
    queued_spot_probe_t *probe_b = ensure_queued_spot_probe (sub_b, false);
    TEST_ASSERT_NOT_NULL (probe_a);
    TEST_ASSERT_NOT_NULL (probe_b);

    service_monitor_probe_t sub_a_monitor_probe;
    g_service_monitor_probe = &sub_a_monitor_probe;
    void *sub_a_monitor = zlink_spot_monitor_open (
      sub_a, ZLINK_SPOT_ROLE_SUB,
      ZLINK_SPOT_SUB_FILTER_APPLIED | ZLINK_SPOT_SUB_SUBSCRIPTION_READY
        | ZLINK_MONITOR_EVENT_ERROR,
      &queued_service_monitor_handler);
    TEST_ASSERT_NOT_NULL (sub_a_monitor);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_subscribe (sub_a, "iso:handle"));
    TEST_ASSERT_TRUE (wait_for_service_event (
      &sub_a_monitor_probe, ZLINK_SPOT_SUB_FILTER_APPLIED, NULL, 3000));
    TEST_ASSERT_TRUE (wait_for_service_event (
      &sub_a_monitor_probe, ZLINK_SPOT_SUB_SUBSCRIPTION_READY, pub_endpoint,
      3000));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_service_monitor_close (&sub_a_monitor));
    g_service_monitor_probe = NULL;

    service_monitor_probe_t sub_b_monitor_probe;
    g_service_monitor_probe = &sub_b_monitor_probe;
    void *sub_b_monitor = zlink_spot_monitor_open (
      sub_b, ZLINK_SPOT_ROLE_SUB,
      ZLINK_SPOT_SUB_FILTER_APPLIED | ZLINK_SPOT_SUB_SUBSCRIPTION_READY
        | ZLINK_MONITOR_EVENT_ERROR,
      &queued_service_monitor_handler);
    TEST_ASSERT_NOT_NULL (sub_b_monitor);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_subscribe (sub_b, "iso:handle"));
    TEST_ASSERT_TRUE (wait_for_service_event (
      &sub_b_monitor_probe, ZLINK_SPOT_SUB_FILTER_APPLIED, NULL, 3000));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_service_monitor_close (&sub_b_monitor));
    g_service_monitor_probe = NULL;

    TEST_ASSERT_SUCCESS_ERRNO (
      publish_text (&zlink_spot_publish, pub, "iso:handle", "fanout", 0));

    TEST_ASSERT_TRUE (
      wait_for_spot_message (sub_a, "iso:handle", "fanout", 6, 1000));
    TEST_ASSERT_TRUE (
      wait_for_spot_message (sub_b, "iso:handle", "fanout", 6, 1000));

    queued_spot_message_t extra;
    TEST_ASSERT_FALSE (pop_next_spot_message (probe_a, &extra));
    TEST_ASSERT_FALSE (pop_next_spot_message (probe_b, &extra));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&sub_b));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&sub_a));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&pub));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&sub_node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&pub_node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

static void test_spot_facade_handler_receives_source_rid ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *pub_node = create_spot_node (ctx, "spot-test");
    void *sub_node = create_spot_node (ctx, "spot-test");
    TEST_ASSERT_NOT_NULL (pub_node);
    TEST_ASSERT_NOT_NULL (sub_node);

    void *pub = create_spot_handle (pub_node, &ignore_spot_handler);
    TEST_ASSERT_NOT_NULL (pub);

    direct_spot_probe_t probe;
    probe.calls = 0;
    probe.saw_source_rid = false;
    memset (&probe.source_rid, 0, sizeof (probe.source_rid));
    g_direct_spot_probe = &probe;

    void *sub = create_spot_handle (sub_node, &direct_spot_handler);
    TEST_ASSERT_NOT_NULL (sub);

    char endpoint[MAX_SOCKET_STRING];
    int port_seed = 22102;
    TEST_ASSERT_SUCCESS_ERRNO (bind_spot_node_with_port_seed (
      pub_node, "tcp://127.0.0.1:", &port_seed, endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_connect_peer_pub (sub_node, endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_subscribe (sub, "rid:test"));
    msleep (200);

    TEST_ASSERT_SUCCESS_ERRNO (
      publish_text (&zlink_spot_publish, pub, "rid:test", "pong", 0));

    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now () + std::chrono::milliseconds (1000);
    while (std::chrono::steady_clock::now () < deadline) {
        {
            std::lock_guard<std::mutex> lock (probe.mutex);
            if (probe.calls > 0)
                break;
        }
        msleep (10);
    }

    {
        std::lock_guard<std::mutex> lock (probe.mutex);
        TEST_ASSERT_EQUAL_INT (1, probe.calls);
        TEST_ASSERT_TRUE (probe.saw_source_rid);
        TEST_ASSERT_TRUE (probe.source_rid.size > 0);
        TEST_ASSERT_EQUAL_STRING_LEN ("rid:test", probe.topic.c_str (), 8);
        TEST_ASSERT_EQUAL_STRING_LEN ("pong", probe.payload.c_str (), 4);
    }

    g_direct_spot_probe = NULL;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_unsubscribe (sub, "rid:test"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_disconnect_peer_pub (sub_node, endpoint));
    msleep (50);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&sub));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&pub));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&sub_node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&pub_node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

static void test_spot_recv_callback_isolated_by_service_with_discovery ()
{
    if (!zlink_has ("tcp")) {
        TEST_IGNORE_MESSAGE ("TCP not available");
        return;
    }

    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    int registry_seed = 33240;
    char registry_pub[MAX_SOCKET_STRING];
    char registry_router[MAX_SOCKET_STRING];
    void *registry = create_started_registry_with_port_seed (
      ctx, &registry_seed, registry_pub, sizeof (registry_pub), registry_router,
      sizeof (registry_router));
    TEST_ASSERT_NOT_NULL (registry);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_registry_set_broadcast_interval (registry, 50));

    void *discovery =
      zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_SPOT);
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_SUCCESS_ERRNO (
      connect_discovery_registry_with_retry (discovery, registry_router, 2000));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_subscribe (discovery, "spot-svc-a"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_subscribe (discovery, "spot-svc-b"));

    void *pub_node_a = create_spot_node (ctx, "spot-svc-a");
    void *sub_node_a = create_spot_node (ctx, "spot-svc-a");
    void *pub_node_b = create_spot_node (ctx, "spot-svc-b");
    void *sub_node_b = create_spot_node (ctx, "spot-svc-b");
    TEST_ASSERT_NOT_NULL (pub_node_a);
    TEST_ASSERT_NOT_NULL (sub_node_a);
    TEST_ASSERT_NOT_NULL (pub_node_b);
    TEST_ASSERT_NOT_NULL (sub_node_b);

    char pub_endpoint_a[MAX_SOCKET_STRING];
    char pub_endpoint_b[MAX_SOCKET_STRING];
    int port_seed = 33200;
    TEST_ASSERT_SUCCESS_ERRNO (bind_spot_node_with_port_seed (
      pub_node_a, "tcp://127.0.0.1:", &port_seed, pub_endpoint_a));
    TEST_ASSERT_SUCCESS_ERRNO (bind_spot_node_with_port_seed (
      pub_node_b, "tcp://127.0.0.1:", &port_seed, pub_endpoint_b));

    void *pub_a = create_spot_handle (pub_node_a, &ignore_spot_handler);
    void *sub_a = create_spot_handle (sub_node_a, &queued_spot_handler);
    void *pub_b = create_spot_handle (pub_node_b, &ignore_spot_handler);
    void *sub_b = create_spot_handle (sub_node_b, &queued_spot_handler);
    TEST_ASSERT_NOT_NULL (pub_a);
    TEST_ASSERT_NOT_NULL (sub_a);
    TEST_ASSERT_NOT_NULL (pub_b);
    TEST_ASSERT_NOT_NULL (sub_b);

    TEST_ASSERT_NOT_NULL (ensure_queued_spot_probe (sub_a, false));
    TEST_ASSERT_NOT_NULL (ensure_queued_spot_probe (sub_b, false));

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_attach_discovery (pub_node_a, discovery));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_attach_discovery (sub_node_a, discovery));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_attach_discovery (pub_node_b, discovery));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_attach_discovery (sub_node_b, discovery));

    service_monitor_probe_t sub_a_monitor_probe;
    g_service_monitor_probe = &sub_a_monitor_probe;
    void *sub_a_monitor = zlink_spot_monitor_open (
      sub_a, ZLINK_SPOT_ROLE_SUB,
      ZLINK_SPOT_SUB_FILTER_APPLIED | ZLINK_SPOT_SUB_SUBSCRIPTION_READY
        | ZLINK_MONITOR_EVENT_ERROR,
      &queued_service_monitor_handler);
    TEST_ASSERT_NOT_NULL (sub_a_monitor);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_subscribe (sub_a, "iso:service"));
    TEST_ASSERT_TRUE (wait_for_service_event (
      &sub_a_monitor_probe, ZLINK_SPOT_SUB_FILTER_APPLIED, NULL, 3000));
    TEST_ASSERT_TRUE (wait_for_service_event (
      &sub_a_monitor_probe, ZLINK_SPOT_SUB_SUBSCRIPTION_READY, pub_endpoint_a,
      3000));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_service_monitor_close (&sub_a_monitor));
    g_service_monitor_probe = NULL;

    service_monitor_probe_t sub_b_monitor_probe;
    g_service_monitor_probe = &sub_b_monitor_probe;
    void *sub_b_monitor = zlink_spot_monitor_open (
      sub_b, ZLINK_SPOT_ROLE_SUB,
      ZLINK_SPOT_SUB_FILTER_APPLIED | ZLINK_SPOT_SUB_SUBSCRIPTION_READY
        | ZLINK_MONITOR_EVENT_ERROR,
      &queued_service_monitor_handler);
    TEST_ASSERT_NOT_NULL (sub_b_monitor);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_subscribe (sub_b, "iso:service"));
    TEST_ASSERT_TRUE (wait_for_service_event (
      &sub_b_monitor_probe, ZLINK_SPOT_SUB_FILTER_APPLIED, NULL, 3000));
    TEST_ASSERT_TRUE (wait_for_service_event (
      &sub_b_monitor_probe, ZLINK_SPOT_SUB_SUBSCRIPTION_READY, pub_endpoint_b,
      3000));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_service_monitor_close (&sub_b_monitor));
    g_service_monitor_probe = NULL;

    TEST_ASSERT_SUCCESS_ERRNO (
      publish_text (&zlink_spot_publish, pub_a, "iso:service", "from-a", 0));
    TEST_ASSERT_TRUE (
      wait_for_spot_message (sub_a, "iso:service", "from-a", 6, 5000));
    TEST_ASSERT_FALSE (
      wait_for_spot_message (sub_b, "iso:service", "from-a", 6, 200));

    TEST_ASSERT_SUCCESS_ERRNO (
      publish_text (&zlink_spot_publish, pub_b, "iso:service", "from-b", 0));
    TEST_ASSERT_TRUE (
      wait_for_spot_message (sub_b, "iso:service", "from-b", 6, 5000));
    TEST_ASSERT_FALSE (
      wait_for_spot_message (sub_a, "iso:service", "from-b", 6, 200));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&sub_b));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&pub_b));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&sub_a));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&pub_a));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&sub_node_b));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&pub_node_b));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&sub_node_a));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&pub_node_a));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

static void test_spot_node_direct_local_and_child_interop ()
{
    step_log ("node_child_interop: create ctx");
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    step_log ("node_child_interop: create node");
    void *node = create_spot_node (ctx, "spot-test");
    TEST_ASSERT_NOT_NULL (node);

    step_log ("node_child_interop: create child handles");
    void *child_pub = create_spot_handle (node, &ignore_spot_handler);
    void *child_sub = create_spot_handle (node, &queued_spot_handler);
    TEST_ASSERT_NOT_NULL (child_pub);
    TEST_ASSERT_NOT_NULL (child_sub);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_subscribe (node, "mix:direct"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_subscribe (node, "mix:child"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_subscribe (child_sub, "mix:direct"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_subscribe (child_sub, "mix:child"));
    TEST_ASSERT_NOT_NULL (ensure_queued_spot_probe (node, true));
    TEST_ASSERT_NOT_NULL (ensure_queued_spot_probe (child_sub, false));
    msleep (50);

    step_log ("node_child_interop: publish");
    TEST_ASSERT_SUCCESS_ERRNO (publish_text (
      &zlink_spot_publish, child_pub, "mix:child", "child-msg", 0));
    step_log ("node_child_interop: wait node recv");
    TEST_ASSERT_TRUE (wait_for_node_message (node, "mix:child", "child-msg", 9,
                                             1000));
    step_log ("node_child_interop: wait child recv");
    TEST_ASSERT_TRUE (
      wait_for_spot_message (child_sub, "mix:child", "child-msg", 9, 1000));

    step_log ("node_child_interop: visibility subscribe");
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_subscribe (node, "mix:visibility"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_subscribe (child_sub, "mix:visibility"));
    msleep (50);

    TEST_ASSERT_SUCCESS_ERRNO (publish_text (
      &zlink_spot_publish, child_pub, "mix:visibility", "before-unsub", 0));
    TEST_ASSERT_TRUE (wait_for_node_message (
      node, "mix:visibility", "before-unsub", 12, 1000));
    TEST_ASSERT_TRUE (wait_for_spot_message (
      child_sub, "mix:visibility", "before-unsub", 12, 1000));

    step_log ("node_child_interop: visibility unsubscribe");
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_unsubscribe_filter (node, "mix:visibility"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_unsubscribe (child_sub, "mix:visibility"));
    TEST_ASSERT_SUCCESS_ERRNO (publish_text (
      &zlink_spot_publish, child_pub, "mix:visibility", "after-unsub", 0));
    TEST_ASSERT_FALSE (wait_for_node_message (
      node, "mix:visibility", "after-unsub", 11, 200));
    TEST_ASSERT_FALSE (wait_for_spot_message (
      child_sub, "mix:visibility", "after-unsub", 11, 200));

    step_log ("node_child_interop: visibility resubscribe");
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_subscribe (node, "mix:visibility"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_subscribe (child_sub, "mix:visibility"));
    msleep (50);
    TEST_ASSERT_SUCCESS_ERRNO (publish_text (
      &zlink_spot_publish, child_pub, "mix:visibility", "after-resub", 0));
    TEST_ASSERT_TRUE (wait_for_node_message (
      node, "mix:visibility", "after-resub", 11, 1000));
    TEST_ASSERT_TRUE (wait_for_spot_message (
      child_sub, "mix:visibility", "after-resub", 11, 1000));

    step_log ("node_child_interop: unsubscribe and reset handlers");
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_unsubscribe_filter (node, "mix:direct"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_unsubscribe_filter (node, "mix:child"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_unsubscribe_filter (node, "mix:visibility"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_unsubscribe (child_sub, "mix:direct"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_unsubscribe (child_sub, "mix:child"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_unsubscribe (child_sub, "mix:visibility"));
    msleep (50);

    step_log ("node_child_interop: destroy child_sub");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&child_sub));
    step_log ("node_child_interop: destroy child_pub");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&child_pub));
    step_log ("node_child_interop: destroy node");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    step_log ("node_child_interop: ctx term");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

static void test_spot_node_direct_remote_peer_mesh ()
{
    step_log ("node_remote_mesh: create ctx");
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    step_log ("node_remote_mesh: create nodes");
    void *node_a = create_spot_node (ctx, "spot-test");
    void *node_b = create_spot_node (ctx, "spot-test");
    TEST_ASSERT_NOT_NULL (node_a);
    TEST_ASSERT_NOT_NULL (node_b);

    char endpoint[MAX_SOCKET_STRING];
    int port_seed = 22103;
    step_log ("node_remote_mesh: bind and connect");
    TEST_ASSERT_SUCCESS_ERRNO (bind_spot_node_with_port_seed (
      node_a, "tcp://127.0.0.1:", &port_seed, endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_connect_peer_pub (node_b, endpoint));

    step_log ("node_remote_mesh: subscribe");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_subscribe (node_b, "mesh:direct"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_subscribe (node_b, "mesh:child"));
    TEST_ASSERT_NOT_NULL (ensure_queued_spot_probe (node_b, true));
    msleep (250);

    step_log ("node_remote_mesh: publish direct");
    bool got_direct = false;
    for (int i = 0; i < 15 && !got_direct; ++i) {
        TEST_ASSERT_SUCCESS_ERRNO (publish_text (
          &zlink_spot_node_publish, node_a, "mesh:direct", "from-node", 0));
        got_direct =
          wait_for_node_message (node_b, "mesh:direct", "from-node", 9, 100);
    }
    TEST_ASSERT_TRUE (got_direct);

    step_log ("node_remote_mesh: create child pub");
    void *child_pub = create_spot_handle (node_a, &ignore_spot_handler);
    TEST_ASSERT_NOT_NULL (child_pub);
    step_log ("node_remote_mesh: publish child");
    bool got_child = false;
    for (int i = 0; i < 15 && !got_child; ++i) {
        TEST_ASSERT_SUCCESS_ERRNO (publish_text (
          &zlink_spot_publish, child_pub, "mesh:child", "from-child", 0));
        got_child =
          wait_for_node_message (node_b, "mesh:child", "from-child", 10, 100);
    }
    TEST_ASSERT_TRUE (got_child);

    step_log ("node_remote_mesh: destroy child");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&child_pub));
    step_log ("node_remote_mesh: destroy node_b");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node_b));
    step_log ("node_remote_mesh: destroy node_a");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node_a));
    step_log ("node_remote_mesh: term ctx");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

static void test_spot_node_direct_sub_option_inheritance_and_handler_conflict ()
{
    step_log ("sub_option_inheritance: create ctx");
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    step_log ("sub_option_inheritance: create node");
    void *node = create_spot_node (ctx, "spot-test");
    TEST_ASSERT_NOT_NULL (node);
    step_log ("sub_option_inheritance: create child");
    void *child_old = create_spot_handle (node, &ignore_spot_handler);
    TEST_ASSERT_NOT_NULL (child_old);

    step_log ("sub_option_inheritance: destroy child");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&child_old));
    step_log ("sub_option_inheritance: destroy node");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    step_log ("sub_option_inheritance: term ctx");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

struct node_publish_probe_t
{
    void *node;
    const char *topic;
    const char *payload;
    size_t payload_size;
    std::atomic<int> rc;
    std::atomic<int> err;
};

static void spot_node_publish_worker (node_publish_probe_t *probe_)
{
    if (!probe_)
        return;

    const int rc = publish_text (&zlink_spot_node_publish, probe_->node,
                                 probe_->topic, probe_->payload, 0);
    probe_->rc.store (rc);
    probe_->err.store (rc == 0 ? 0 : zlink_errno ());
}

static void test_spot_node_direct_first_publish_race ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *node = create_spot_node (ctx, "spot-test");
    TEST_ASSERT_NOT_NULL (node);

    void *child_sub = create_spot_handle (node, &ignore_spot_handler);
    TEST_ASSERT_NOT_NULL (child_sub);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_subscribe (child_sub, "race:topic"));
    msleep (50);

    node_publish_probe_t probe_a;
    probe_a.node = node;
    probe_a.topic = "race:topic";
    probe_a.payload = "a";
    probe_a.payload_size = 1;
    probe_a.rc.store (-999);
    probe_a.err.store (0);

    node_publish_probe_t probe_b;
    probe_b.node = node;
    probe_b.topic = "race:topic";
    probe_b.payload = "b";
    probe_b.payload_size = 1;
    probe_b.rc.store (-999);
    probe_b.err.store (0);

    std::thread worker_a (spot_node_publish_worker, &probe_a);
    std::thread worker_b (spot_node_publish_worker, &probe_b);
    worker_a.join ();
    worker_b.join ();

    TEST_ASSERT_EQUAL_INT (0, probe_a.rc.load ());
    TEST_ASSERT_EQUAL_INT (0, probe_a.err.load ());
    TEST_ASSERT_EQUAL_INT (0, probe_b.rc.load ());
    TEST_ASSERT_EQUAL_INT (0, probe_b.err.load ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&child_sub));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

static void test_spot_pub_async_mode_local_delivery ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    void *node = create_spot_node (ctx, "spot-test");
    TEST_ASSERT_NOT_NULL (node);

    void *pub = NULL;
    void *sub = NULL;
    TEST_ASSERT_SUCCESS_ERRNO (create_spot_pub_sub (node, &pub, &sub));
    int pub_mode = ZLINK_SPOT_PUB_MODE_ASYNC;
    TEST_ASSERT_EQUAL_INT (
      -1, zlink_spot_set_pub_option (pub, ZLINK_SPOT_PUB_OPT_MODE, &pub_mode,
                                     sizeof (pub_mode)));
    TEST_ASSERT_EQUAL_INT (ENOTSUP, zlink_errno ());
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_subscribe (sub, "pub:async:test"));
    TEST_ASSERT_NOT_NULL (ensure_queued_spot_probe (sub, false));
    msleep (50);

    zlink_msg_t part;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&part, 4));
    memcpy (zlink_msg_data (&part), "pong", 4);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_publish (pub, "pub:async:test", &part, 1, 0));

    TEST_ASSERT_TRUE_MESSAGE (
      wait_for_spot_message (sub, "pub:async:test", "pong", 4, 1000),
      "async publish message was not delivered");

    TEST_ASSERT_SUCCESS_ERRNO (destroy_spot_pub_sub (&pub, &sub));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

static void test_spot_pub_async_setsockopt_validation ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    void *node = create_spot_node (ctx, "spot-test");
    TEST_ASSERT_NOT_NULL (node);

    int value = 7;
    TEST_ASSERT_EQUAL_INT (
      -1, set_node_pub_option (node, ZLINK_SPOT_PUB_OPT_MODE, &value,
                               sizeof (value)));
    TEST_ASSERT_EQUAL_INT (ENOTSUP, zlink_errno ());

    value = ZLINK_SPOT_PUB_MODE_ASYNC;
    TEST_ASSERT_EQUAL_INT (
      -1, set_node_pub_option (node, ZLINK_SPOT_PUB_OPT_MODE, &value,
                               sizeof (value)));
    TEST_ASSERT_EQUAL_INT (ENOTSUP, zlink_errno ());
    value = ZLINK_SPOT_PUB_MODE_SYNC;
    TEST_ASSERT_EQUAL_INT (
      -1, set_node_pub_option (node, ZLINK_SPOT_PUB_OPT_MODE, &value,
                               sizeof (value)));
    TEST_ASSERT_EQUAL_INT (ENOTSUP, zlink_errno ());

    value = 0;
    TEST_ASSERT_EQUAL_INT (
      -1, set_node_pub_option (node, ZLINK_SPOT_PUB_OPT_QUEUE_HWM, &value,
                               sizeof (value)));
    TEST_ASSERT_EQUAL_INT (ENOTSUP, zlink_errno ());
    value = 8;
    TEST_ASSERT_EQUAL_INT (
      -1, set_node_pub_option (node, ZLINK_SPOT_PUB_OPT_QUEUE_HWM, &value,
                               sizeof (value)));
    TEST_ASSERT_EQUAL_INT (ENOTSUP, zlink_errno ());

    value = 3;
    TEST_ASSERT_EQUAL_INT (
      -1, set_node_pub_option (node, ZLINK_SPOT_PUB_OPT_QUEUE_FULL_POLICY,
                               &value, sizeof (value)));
    TEST_ASSERT_EQUAL_INT (ENOTSUP, zlink_errno ());
    value = ZLINK_SPOT_PUB_QUEUE_FULL_DROP;
    TEST_ASSERT_EQUAL_INT (
      -1, set_node_pub_option (node, ZLINK_SPOT_PUB_OPT_QUEUE_FULL_POLICY,
                               &value, sizeof (value)));
    TEST_ASSERT_EQUAL_INT (ENOTSUP, zlink_errno ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

static void test_spot_pub_async_queue_full_eagain ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    void *node = create_spot_node (ctx, "spot-test");
    TEST_ASSERT_NOT_NULL (node);

    void *pub = NULL;
    void *sub = NULL;
    TEST_ASSERT_SUCCESS_ERRNO (create_spot_pub_sub (node, &pub, &sub));
    int value = ZLINK_SPOT_PUB_MODE_ASYNC;
    TEST_ASSERT_EQUAL_INT (
      -1, zlink_spot_set_pub_option (pub, ZLINK_SPOT_PUB_OPT_MODE, &value,
                                     sizeof (value)));
    TEST_ASSERT_EQUAL_INT (ENOTSUP, zlink_errno ());
    value = 1;
    TEST_ASSERT_EQUAL_INT (
      -1, zlink_spot_set_pub_option (pub, ZLINK_SPOT_PUB_OPT_QUEUE_HWM, &value,
                                     sizeof (value)));
    TEST_ASSERT_EQUAL_INT (ENOTSUP, zlink_errno ());
    value = ZLINK_SPOT_PUB_QUEUE_FULL_EAGAIN;
    TEST_ASSERT_EQUAL_INT (
      -1, zlink_spot_set_pub_option (
            pub, ZLINK_SPOT_PUB_OPT_QUEUE_FULL_POLICY, &value,
            sizeof (value)));
    TEST_ASSERT_EQUAL_INT (ENOTSUP, zlink_errno ());
    TEST_ASSERT_SUCCESS_ERRNO (destroy_spot_pub_sub (&pub, &sub));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

static void test_spot_pub_async_queue_full_drop ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    void *node = create_spot_node (ctx, "spot-test");
    TEST_ASSERT_NOT_NULL (node);

    void *pub = NULL;
    void *sub = NULL;
    TEST_ASSERT_SUCCESS_ERRNO (create_spot_pub_sub (node, &pub, &sub));
    int value = ZLINK_SPOT_PUB_MODE_ASYNC;
    TEST_ASSERT_EQUAL_INT (
      -1, zlink_spot_set_pub_option (pub, ZLINK_SPOT_PUB_OPT_MODE, &value,
                                     sizeof (value)));
    TEST_ASSERT_EQUAL_INT (ENOTSUP, zlink_errno ());
    value = 1;
    TEST_ASSERT_EQUAL_INT (
      -1, zlink_spot_set_pub_option (pub, ZLINK_SPOT_PUB_OPT_QUEUE_HWM, &value,
                                     sizeof (value)));
    TEST_ASSERT_EQUAL_INT (ENOTSUP, zlink_errno ());
    value = ZLINK_SPOT_PUB_QUEUE_FULL_DROP;
    TEST_ASSERT_EQUAL_INT (
      -1, zlink_spot_set_pub_option (
            pub, ZLINK_SPOT_PUB_OPT_QUEUE_FULL_POLICY, &value,
            sizeof (value)));
    TEST_ASSERT_EQUAL_INT (ENOTSUP, zlink_errno ());
    TEST_ASSERT_SUCCESS_ERRNO (destroy_spot_pub_sub (&pub, &sub));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

static int zone_idx (int x_, int y_, int width_)
{
    return y_ * width_ + x_;
}

static int env_int_or_default (const char *name_, int default_)
{
    const char *val = getenv (name_);
    if (!val || !*val)
        return default_;

    const int parsed = atoi (val);
    if (parsed <= 0)
        return default_;
    return parsed;
}

static bool wait_for_provider_count (void *discovery_,
                                     const char *service_name_,
                                     int expected_count_,
                                     int timeout_ms_)
{
    const int sleep_ms_step = 25;
    const int max_attempts = timeout_ms_ / sleep_ms_step;

    for (int i = 0; i < max_attempts; ++i) {
        const int count =
          zlink_discovery_receiver_count (discovery_, service_name_);
        if (count == expected_count_)
            return true;
        msleep (sleep_ms_step);
    }
    return false;
}

static bool wait_for_service_event_match (service_monitor_probe_t *probe_,
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

static bool wait_for_service_event (service_monitor_probe_t *probe_,
                                    uint32_t expected_event_type_,
                                    const char *endpoint_prefix_,
                                    int timeout_ms_)
{
    return wait_for_service_event_match (probe_, expected_event_type_,
                                         endpoint_prefix_, NULL, -1, NULL,
                                         timeout_ms_);
}

static bool wait_for_spot_message (void *spot_sub_,
                                   const char *expected_topic_,
                                   const char *expected_payload_,
                                   size_t expected_payload_size_,
                                   int timeout_ms_)
{
    queued_spot_probe_t *probe =
      ensure_queued_spot_probe (spot_sub_, false);
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

static bool wait_for_node_message (void *node_,
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

static void test_spot_node_discovery_direct_and_child_interop ()
{
    if (!zlink_has ("tcp")) {
        TEST_IGNORE_MESSAGE ("TCP not available");
        return;
    }

    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    int registry_seed = 33190;
    char registry_pub[MAX_SOCKET_STRING];
    char registry_router[MAX_SOCKET_STRING];
    void *registry = create_started_registry_with_port_seed (
      ctx, &registry_seed, registry_pub, sizeof (registry_pub), registry_router,
      sizeof (registry_router));
    TEST_ASSERT_NOT_NULL (registry);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_registry_set_broadcast_interval (registry, 50));

    void *discovery =
      zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_SPOT);
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_SUCCESS_ERRNO (
      connect_discovery_registry_with_retry (discovery, registry_router, 2000));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_subscribe (discovery, "spot-discovery-interop"));

    void *pub_node = create_spot_node (ctx, "spot-discovery-interop");
    void *sub_node = create_spot_node (ctx, "spot-discovery-interop");
    TEST_ASSERT_NOT_NULL (pub_node);
    TEST_ASSERT_NOT_NULL (sub_node);

    char pub_endpoint[MAX_SOCKET_STRING];
    char sub_endpoint[MAX_SOCKET_STRING];
    int port_seed = 33100;
    TEST_ASSERT_SUCCESS_ERRNO (bind_spot_node_with_port_seed (
      pub_node, "tcp://127.0.0.1:", &port_seed, pub_endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (bind_spot_node_with_port_seed (
      sub_node, "tcp://127.0.0.1:", &port_seed, sub_endpoint));

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_publish (pub_node, "__warmup__", NULL, 0, 0));

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_attach_discovery (pub_node, discovery));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_attach_discovery (sub_node, discovery));
    TEST_ASSERT_TRUE (wait_for_provider_count (
      discovery, "spot-discovery-interop", 2, 3000));

    TEST_ASSERT_NOT_NULL (ensure_queued_spot_probe (sub_node, true));
    service_monitor_probe_t node_sub_monitor_probe;
    g_service_monitor_probe = &node_sub_monitor_probe;
    void *node_sub_monitor = zlink_spot_node_monitor_open (
      sub_node, ZLINK_SPOT_ROLE_SUB,
      ZLINK_SPOT_SUB_FILTER_APPLIED | ZLINK_SPOT_SUB_SUBSCRIPTION_READY
        | ZLINK_MONITOR_EVENT_ERROR,
      &queued_service_monitor_handler);
    TEST_ASSERT_NOT_NULL (node_sub_monitor);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_subscribe (sub_node, "interop:node"));
    TEST_ASSERT_TRUE (wait_for_service_event (
      &node_sub_monitor_probe, ZLINK_SPOT_SUB_FILTER_APPLIED, NULL, 3000));
    TEST_ASSERT_TRUE (wait_for_service_event (
      &node_sub_monitor_probe, ZLINK_SPOT_SUB_SUBSCRIPTION_READY, pub_endpoint,
      3000));

    TEST_ASSERT_SUCCESS_ERRNO (publish_text (
      &zlink_spot_node_publish, pub_node, "interop:node", "node-hop", 0));
    TEST_ASSERT_TRUE (wait_for_node_message (
      sub_node, "interop:node", "node-hop", 8, 5000));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_service_monitor_close (&node_sub_monitor));
    g_service_monitor_probe = NULL;

    void *child_sub = create_spot_handle (pub_node, &queued_spot_handler);
    void *child_pub = create_spot_handle (sub_node, &ignore_spot_handler);
    TEST_ASSERT_NOT_NULL (child_sub);
    TEST_ASSERT_NOT_NULL (child_pub);

    TEST_ASSERT_NOT_NULL (ensure_queued_spot_probe (child_sub, false));
    service_monitor_probe_t child_sub_monitor_probe;
    g_service_monitor_probe = &child_sub_monitor_probe;
    void *child_sub_monitor = zlink_spot_monitor_open (
      child_sub, ZLINK_SPOT_ROLE_SUB,
      ZLINK_SPOT_SUB_FILTER_APPLIED | ZLINK_SPOT_SUB_SUBSCRIPTION_READY
        | ZLINK_MONITOR_EVENT_ERROR,
      &queued_service_monitor_handler);
    TEST_ASSERT_NOT_NULL (child_sub_monitor);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_subscribe (child_sub, "interop:child"));
    TEST_ASSERT_TRUE (wait_for_service_event (
      &child_sub_monitor_probe, ZLINK_SPOT_SUB_FILTER_APPLIED, NULL, 3000));
    TEST_ASSERT_TRUE (wait_for_service_event (
      &child_sub_monitor_probe, ZLINK_SPOT_SUB_SUBSCRIPTION_READY,
      sub_endpoint, 3000));

    TEST_ASSERT_SUCCESS_ERRNO (publish_text (
      &zlink_spot_publish, child_pub, "interop:child", "child-hop", 0));
    TEST_ASSERT_TRUE (wait_for_spot_message (
      child_sub, "interop:child", "child-hop", 9, 5000));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_service_monitor_close (&child_sub_monitor));
    g_service_monitor_probe = NULL;

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_unsubscribe_filter (sub_node, "interop:node"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_unsubscribe (child_sub, "interop:child"));
    msleep (50);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&child_sub));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&child_pub));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&sub_node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&pub_node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

static void test_spot_mmorpg_zone_adjacency_scale_multi_node_discovery ()
{
    if (env_int_or_default ("ZLINK_SPOT_RUN_MULTI_NODE_DISCOVERY", 0) == 0) {
        TEST_IGNORE_MESSAGE (
          "Multi-node discovery scale test disabled "
          "(set ZLINK_SPOT_RUN_MULTI_NODE_DISCOVERY=1)");
        return;
    }

    const int field_width =
      env_int_or_default ("ZLINK_SPOT_MULTI_NODE_FIELD_WIDTH", 8);
    const int field_height =
      env_int_or_default ("ZLINK_SPOT_MULTI_NODE_FIELD_HEIGHT", 8);
    const int zone_count = field_width * field_height;
    const int spot_node_count =
      env_int_or_default ("ZLINK_SPOT_MULTI_NODE_COUNT", 4);
    const int auto_peer_settle_ms = 800;
    const int sub_propagation_ms = 1200;
    const int recv_timeout_ms = 1000;

    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    int registry_seed = 33090;
    char registry_pub[MAX_SOCKET_STRING];
    char registry_router[MAX_SOCKET_STRING];
    void *registry = create_started_registry_with_port_seed (
      ctx, &registry_seed, registry_pub, sizeof (registry_pub), registry_router,
      sizeof (registry_router));
    TEST_ASSERT_NOT_NULL (registry);

    void *discovery =
      zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_SPOT);
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_SUCCESS_ERRNO (
      connect_discovery_registry_with_retry (discovery, registry_router, 2000));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_subscribe (discovery, "spot-field-mmorpg"));

    std::vector<void *> nodes (spot_node_count, static_cast<void *> (NULL));
    std::vector<std::string> node_endpoints (spot_node_count);
    int port_seed = 33000;
    for (int i = 0; i < spot_node_count; ++i) {
        nodes[i] = create_spot_node (ctx, "spot-field-mmorpg");
        TEST_ASSERT_NOT_NULL (nodes[i]);
        int sndhwm = 1000000;
        int rcvhwm = 1000000;
        int linger = 0;
        TEST_ASSERT_SUCCESS_ERRNO (set_node_pub_option (
          nodes[i], ZLINK_SPOT_PUB_OPT_SNDHWM, &sndhwm, sizeof (sndhwm)));
        TEST_ASSERT_SUCCESS_ERRNO (set_node_sub_option (
          nodes[i], ZLINK_SPOT_SUB_OPT_RCVHWM, &rcvhwm, sizeof (rcvhwm)));
        TEST_ASSERT_SUCCESS_ERRNO (set_node_pub_option (
          nodes[i], ZLINK_SPOT_PUB_OPT_LINGER, &linger, sizeof (linger)));
        TEST_ASSERT_SUCCESS_ERRNO (set_node_sub_option (
          nodes[i], ZLINK_SPOT_SUB_OPT_LINGER, &linger, sizeof (linger)));
        char endpoint[256] = {0};
        TEST_ASSERT_SUCCESS_ERRNO (bind_spot_node_with_port_seed (
          nodes[i], "tcp://127.0.0.1:", &port_seed, endpoint));
        node_endpoints[i] = endpoint;
    }

    for (int i = 0; i < spot_node_count; ++i) {
        TEST_ASSERT_SUCCESS_ERRNO (
          zlink_spot_node_attach_discovery (nodes[i], discovery));
    }

    TEST_ASSERT_TRUE (wait_for_provider_count (
      discovery, "spot-field-mmorpg", spot_node_count, 3000));
    // Auto peer connections can be delayed under load.
    msleep (auto_peer_settle_ms);

    std::vector<void *> spot_pubs (zone_count, static_cast<void *> (NULL));
    std::vector<void *> spot_subs (zone_count, static_cast<void *> (NULL));
    std::vector<std::string> topics (zone_count);

    for (int y = 0; y < field_height; ++y) {
        for (int x = 0; x < field_width; ++x) {
            const int idx = zone_idx (x, y, field_width);
            const int owner_node = idx % spot_node_count;

            TEST_ASSERT_SUCCESS_ERRNO (create_spot_pub_sub (
              nodes[owner_node], &spot_pubs[idx], &spot_subs[idx]));

            char topic_buf[64];
            snprintf (topic_buf, sizeof (topic_buf), "field-mm:%d:%d:state", x, y);
            topics[idx] = topic_buf;
        }
    }

    for (int y = 0; y < field_height; ++y) {
        for (int x = 0; x < field_width; ++x) {
            const int dst_idx = zone_idx (x, y, field_width);

            for (int oy = -1; oy <= 1; ++oy) {
                for (int ox = -1; ox <= 1; ++ox) {
                    if (ox != 0 && oy != 0)
                        continue;

                    const int src_x = x + ox;
                    const int src_y = y + oy;
                    if (src_x < 0 || src_x >= field_width || src_y < 0
                        || src_y >= field_height)
                        continue;

                    const int src_idx = zone_idx (src_x, src_y, field_width);
                    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_subscribe (
                      spot_subs[dst_idx], topics[src_idx].c_str ()));
                }
            }
        }
    }

    for (int i = 0; i < zone_count; ++i)
        TEST_ASSERT_NOT_NULL (ensure_queued_spot_probe (spot_subs[i], false));

    // Allow subscription propagation across all spot nodes before publishing.
    msleep (sub_propagation_ms);

    const int sample_coords[][2] = {{0, 0},
                                    {field_width - 1, 0},
                                    {0, field_height - 1},
                                    {field_width - 1, field_height - 1},
                                    {field_width / 2, field_height / 2},
                                    {field_width / 4, field_height / 4},
                                    {field_width / 5, (field_height * 2) / 5},
                                    {(field_width * 33) / 100,
                                     (field_height * 77) / 100},
                                    {(field_width * 44) / 100,
                                     (field_height * 55) / 100},
                                    {(field_width * 7) / 10,
                                     (field_height * 3) / 10},
                                    {(field_width * 88) / 100,
                                     (field_height * 11) / 100},
                                    {(field_width * 95) / 100,
                                     (field_height * 95) / 100}};
    const size_t sample_count = sizeof (sample_coords) / sizeof (sample_coords[0]);

    for (size_t i = 0; i < sample_count; ++i) {
        const int src_idx =
          zone_idx (sample_coords[i][0], sample_coords[i][1], field_width);
        const int src_x = src_idx % field_width;
        const int src_y = src_idx / field_width;

        zlink_msg_t part;
        TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&part, sizeof (int)));
        memcpy (zlink_msg_data (&part), &src_idx, sizeof (int));
        TEST_ASSERT_SUCCESS_ERRNO (
          zlink_spot_publish (spot_pubs[src_idx], topics[src_idx].c_str (),
                              &part, 1, 0));

        for (int oy = -1; oy <= 1; ++oy) {
            for (int ox = -1; ox <= 1; ++ox) {
                if (ox != 0 && oy != 0)
                    continue;

                const int dst_x = src_x + ox;
                const int dst_y = src_y + oy;
                if (dst_x < 0 || dst_x >= field_width || dst_y < 0
                    || dst_y >= field_height)
                    continue;

                const int dst_idx = zone_idx (dst_x, dst_y, field_width);
                TEST_ASSERT_TRUE (wait_for_spot_message (
                  spot_subs[dst_idx], topics[src_idx].c_str (),
                  (const char *) &src_idx, sizeof (int), recv_timeout_ms));
            }
        }
    }

    for (int i = 0; i < zone_count; ++i)
        TEST_ASSERT_SUCCESS_ERRNO (
          destroy_spot_pub_sub (&spot_pubs[i], &spot_subs[i]));

    for (int i = 0; i < spot_node_count; ++i) {
        TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&nodes[i]));
    }

    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

int main (int, char **)
{
    setup_test_environment (600);

    UNITY_BEGIN ();
#define RUN_SPOT_TEST(name)                                                    \
    do {                                                                       \
        if (should_run_spot_test (#name))                                      \
            RUN_TEST (name);                                                   \
    } while (0)
    RUN_SPOT_TEST (test_spot_peer_ipc);
    RUN_SPOT_TEST (test_spot_peer_tcp);
    RUN_SPOT_TEST (test_spot_peer_ws);
    RUN_SPOT_TEST (test_spot_peer_tls);
    RUN_SPOT_TEST (test_spot_peer_wss);
    RUN_SPOT_TEST (test_spot_unified_wss_subscription_ready_first_delivery);
    RUN_SPOT_TEST (test_spot_multi_publisher);
    RUN_SPOT_TEST (test_spot_node_direct_local_and_child_interop);
    RUN_SPOT_TEST (test_spot_node_direct_remote_peer_mesh);
    RUN_SPOT_TEST (test_spot_node_direct_sub_option_inheritance_and_handler_conflict);
    RUN_SPOT_TEST (test_spot_node_direct_first_publish_race);
    RUN_SPOT_TEST (test_spot_sub_handler_basic);
    RUN_SPOT_TEST (test_spot_recv_callback_isolated_by_handle);
    RUN_SPOT_TEST (test_spot_recv_callback_isolated_by_service_with_discovery);
    RUN_SPOT_TEST (test_spot_facade_handler_receives_source_rid);
    RUN_SPOT_TEST (test_spot_node_discovery_direct_and_child_interop);
    RUN_SPOT_TEST (test_spot_pub_async_mode_local_delivery);
    RUN_SPOT_TEST (test_spot_pub_async_setsockopt_validation);
    RUN_SPOT_TEST (test_spot_pub_async_queue_full_eagain);
    RUN_SPOT_TEST (test_spot_pub_async_queue_full_drop);
    RUN_SPOT_TEST (test_spot_mmorpg_zone_adjacency_scale_multi_node_discovery);
#undef RUN_SPOT_TEST
    return UNITY_END ();
}
