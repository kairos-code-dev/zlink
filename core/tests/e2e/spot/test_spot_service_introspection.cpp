/* SPDX-License-Identifier: MPL-2.0 */

#include "../../testutil.hpp"
#include "../../testutil_monitoring.hpp"
#include "../../testutil_unity.hpp"

#include "../../../src/services/spot/spot_dispatch_internal.hpp"
#include "../../../src/services/spot/spot_pub.hpp"
#include "../../../src/sockets/socket_base.hpp"
#include "services/spot/spot_node.hpp"
#include "services/discovery/discovery.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <string>
#include <string.h>
#include <vector>

static bool should_run_named_test (const char *name_)
{
    const char *selected = getenv ("ZLINK_TEST_CASE");
    return !selected || !*selected || strcmp (selected, name_) == 0;
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

struct spot_probe_t;

static std::mutex g_spot_probe_registry_mutex;
static std::map<void *, spot_probe_t *> g_spot_handle_probes;
static void spot_probe_handler (const zlink_routing_id_t *,
                                const char *topic_,
                                size_t topic_len_,
                                zlink_msg_t *parts_,
                                size_t part_count_);
static void *g_spot_reentrant_ready_subject = NULL;
static std::atomic<int> *g_spot_reentrant_ready_calls = NULL;
static int g_spot_reentrant_ready_rc = 0;
static int g_spot_reentrant_ready_errno = 0;
static void *g_spot_ready_subject = NULL;
static std::atomic<int> *g_spot_ready_calls = NULL;
static int g_spot_ready_publish_rc = 0;
static int g_spot_ready_publish_errno = 0;
struct service_monitor_probe_t;
static service_monitor_probe_t *g_service_monitor_probe_a = NULL;
static service_monitor_probe_t *g_service_monitor_probe_b = NULL;
struct send_ready_probe_t;
static send_ready_probe_t *g_send_ready_probe_a = NULL;
static send_ready_probe_t *g_send_ready_probe_b = NULL;
static send_ready_probe_t *g_send_ready_probe_replace = NULL;

void setUp ()
{
    std::lock_guard<std::mutex> lock (g_spot_probe_registry_mutex);
    g_spot_handle_probes.clear ();
}

void tearDown ()
{
    {
        std::lock_guard<std::mutex> lock (g_spot_probe_registry_mutex);
        g_spot_handle_probes.clear ();
    }
    g_service_monitor_probe_a = NULL;
    g_service_monitor_probe_b = NULL;
    g_send_ready_probe_a = NULL;
    g_send_ready_probe_b = NULL;
    g_send_ready_probe_replace = NULL;
}

static bool wait_for_registry_uplink (void *discovery_, int timeout_ms_)
{
    zlink::discovery_t *discovery =
      static_cast<zlink::discovery_t *> (discovery_);
    if (!discovery)
        return false;

    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::milliseconds (timeout_ms_);
    while (std::chrono::steady_clock::now () < deadline) {
        std::string uplink;
        if (discovery->latest_registry_uplink (&uplink) && !uplink.empty ())
            return true;
        msleep (10);
    }
    return false;
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

static void make_registry_endpoint (char *endpoint_out_,
                                    size_t endpoint_size_,
                                    int port_seed_)
{
    snprintf (endpoint_out_, endpoint_size_, "tcp://127.0.0.1:%d",
              test_port (port_seed_));
}

static int bind_spot_node_with_port_seed (void *node_,
                                          const char *prefix_,
                                          int *port_seed_,
                                          char *endpoint_out_,
                                          size_t endpoint_size_)
{
    if (!node_ || !prefix_ || !port_seed_ || !endpoint_out_ || endpoint_size_ == 0) {
        errno = EINVAL;
        return -1;
    }

    for (int attempt = 0; attempt < 32; ++attempt) {
        snprintf (endpoint_out_, endpoint_size_, "%s%d", prefix_,
                  test_port (*port_seed_));
        if (zlink_spot_node_bind (node_, endpoint_out_) == 0)
            return 0;
        if (zlink_errno () != EADDRINUSE)
            return -1;
        ++(*port_seed_);
    }

    errno = EADDRINUSE;
    return -1;
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

        snprintf (pub_endpoint_out_, pub_size_, "tcp://127.0.0.1:%d",
                  test_port (*port_seed_));
        snprintf (router_endpoint_out_, router_size_, "tcp://127.0.0.1:%d",
                  test_port (*port_seed_ + 1));

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

static void spot_reentrant_ready_handler (void *subject_)
{
    g_spot_reentrant_ready_subject = subject_;
    if (g_spot_reentrant_ready_calls)
        g_spot_reentrant_ready_calls->fetch_add (1);
    errno = 0;
    g_spot_reentrant_ready_rc = zlink_spot_node_set_send_ready_handler (
      subject_, &spot_reentrant_ready_handler);
    g_spot_reentrant_ready_errno = errno;
}

static void spot_counting_ready_handler (void *subject_)
{
    g_spot_ready_subject = subject_;
    if (g_spot_ready_calls)
        g_spot_ready_calls->fetch_add (1);
}

static void spot_publish_from_ready_handler (void *subject_)
{
    g_spot_ready_subject = subject_;
    if (g_spot_ready_calls)
        g_spot_ready_calls->fetch_add (1);

    zlink_msg_t part;
    if (zlink_msg_init_size (&part, 5) != 0) {
        g_spot_ready_publish_rc = -1;
        g_spot_ready_publish_errno = errno;
        return;
    }
    memcpy (zlink_msg_data (&part), "ready", 5);

    errno = 0;
    g_spot_ready_publish_rc =
      zlink_spot_node_publish (subject_, "ready:topic", &part, 1, 0);
    g_spot_ready_publish_errno =
      g_spot_ready_publish_rc == 0 ? 0 : errno;
    if (g_spot_ready_publish_rc != 0) {
        const int err = errno;
        zlink_msg_close (&part);
        errno = err;
    }
}

static void destroy_test_ctx (void *ctx_)
{
    TEST_ASSERT_NOT_NULL (ctx_);
    step_log ("ctx: pre-shutdown sleep");
    msleep (50);
    step_log ("ctx: shutdown");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_shutdown (ctx_));
    step_log ("ctx: post-shutdown sleep");
    msleep (10);
    step_log ("ctx: term");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx_));
    step_log ("ctx: term done");
}

static void *create_spot_node (void *ctx_, const char *service_name_)
{
    void *node = zlink_spot_node_new (ctx_, service_name_, &ignore_spot_handler);
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

static void *create_spot_pub_handle (void *node_)
{
    return create_spot_handle (node_, &ignore_spot_handler);
}

static void *create_spot_sub_handle (void *node_)
{
    return create_spot_handle (node_, &spot_probe_handler);
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
    const size_t size = payload_ ? strlen (payload_) : 0;
    zlink_msg_t part;
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

static bool wait_for_provider_count (void *registry_,
                                     const char *service_name_,
                                     int expected_count_,
                                     int timeout_ms_)
{
    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::milliseconds (timeout_ms_);
    while (std::chrono::steady_clock::now () < deadline) {
        zlink_registry_topology_filter_t filter;
        memset (&filter, 0, sizeof (filter));
        filter.service_kind = ZLINK_SERVICE_KIND_SPOT_PUB;
        filter.state = ZLINK_TOPOLOGY_STATE_READY;
        strncpy (filter.service_name, service_name_,
                 sizeof (filter.service_name) - 1);

        size_t count = 0;
        if (zlink_registry_topology_query (registry_, &filter, NULL, &count)
                == 0
            && count >= static_cast<size_t> (expected_count_)) {
            return true;
        }
        msleep (10);
    }
    size_t count = 0;
    zlink_registry_topology_filter_t filter;
    memset (&filter, 0, sizeof (filter));
    filter.service_kind = ZLINK_SERVICE_KIND_SPOT_PUB;
    filter.state = ZLINK_TOPOLOGY_STATE_READY;
    strncpy (filter.service_name, service_name_,
             sizeof (filter.service_name) - 1);
    if (zlink_registry_topology_query (registry_, &filter, NULL, &count)
        != 0)
        return false;
    return count >= static_cast<size_t> (expected_count_);
}

struct spot_probe_message_t
{
    std::string topic;
    std::vector<std::string> parts;
};

struct spot_probe_t
{
    std::mutex mutex;
    std::vector<spot_probe_message_t> messages;
};

struct service_monitor_probe_t
{
    std::mutex mutex;
    std::condition_variable cv;
    std::vector<zlink_service_event_t> events;
};

struct send_ready_probe_t
{
    send_ready_probe_t () : calls (0), last_subject (NULL) {}

    std::atomic<int> calls;
    void *last_subject;
};

static void spot_probe_handler (const zlink_routing_id_t *,
                                const char *,
                                size_t,
                                zlink_msg_t *,
                                size_t);

static spot_probe_t *find_spot_probe_for_current_dispatch ()
{
    void *handle = zlink::current_spot_dispatch_handle ();
    if (!handle)
        return NULL;

    std::lock_guard<std::mutex> lock (g_spot_probe_registry_mutex);
    std::map<void *, spot_probe_t *>::iterator it =
      g_spot_handle_probes.find (handle);
    return it != g_spot_handle_probes.end () ? it->second : NULL;
}

static int attach_spot_probe (void *spot_, spot_probe_t *probe_)
{
    if (!spot_ || !probe_) {
        errno = EINVAL;
        return -1;
    }

    {
        std::lock_guard<std::mutex> lock (g_spot_probe_registry_mutex);
        g_spot_handle_probes[spot_] = probe_;
    }
    return 0;
}

static void send_ready_probe_handler_a (void *subject_)
{
    if (!g_send_ready_probe_a)
        return;
    g_send_ready_probe_a->last_subject = subject_;
    g_send_ready_probe_a->calls.fetch_add (1);
}

static void send_ready_probe_handler_b (void *subject_)
{
    if (!g_send_ready_probe_b)
        return;
    g_send_ready_probe_b->last_subject = subject_;
    g_send_ready_probe_b->calls.fetch_add (1);
}

static void send_ready_probe_handler_replace (void *subject_)
{
    if (!g_send_ready_probe_replace)
        return;
    g_send_ready_probe_replace->last_subject = subject_;
    g_send_ready_probe_replace->calls.fetch_add (1);
}

static void queued_service_monitor_handler_a (
  const zlink_service_event_t *event_)
{
    service_monitor_probe_t *probe = g_service_monitor_probe_a;
    if (!probe || !event_)
        return;

    if (test_debug_enabled ()) {
        fprintf (stderr,
                 "[spot-monitor-a] type=%u kind=%u flags=0x%x endpoint=%s subject=%s value=%u\n",
                 event_->event_type, event_->service_kind,
                 static_cast<unsigned int> (event_->detail_flags),
                 event_->endpoint, event_->subject, event_->value);
        fflush (stderr);
    }

    {
        std::lock_guard<std::mutex> lock (probe->mutex);
        probe->events.push_back (*event_);
    }
    probe->cv.notify_all ();
}

static void queued_service_monitor_handler_b (
  const zlink_service_event_t *event_)
{
    service_monitor_probe_t *probe = g_service_monitor_probe_b;
    if (!probe || !event_)
        return;

    if (test_debug_enabled ()) {
        fprintf (stderr,
                 "[spot-monitor-b] type=%u kind=%u flags=0x%x endpoint=%s subject=%s value=%u\n",
                 event_->event_type, event_->service_kind,
                 static_cast<unsigned int> (event_->detail_flags),
                 event_->endpoint, event_->subject, event_->value);
        fflush (stderr);
    }

    {
        std::lock_guard<std::mutex> lock (probe->mutex);
        probe->events.push_back (*event_);
    }
    probe->cv.notify_all ();
}

static bool consume_matching_service_event_locked (
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

static bool consume_exact_service_event_locked (
  service_monitor_probe_t *probe_,
  uint32_t expected_event_type_,
  const char *endpoint_prefix_,
  const char *subject_,
  int expected_value_,
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
        if (expected_value_ >= 0
            && static_cast<int> (it->value) != expected_value_) {
            continue;
        }
        if (event_out_)
            *event_out_ = *it;
        probe_->events.erase (it);
        return true;
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
    if (consume_matching_service_event_locked (
          probe_, expected_event_type_, endpoint_prefix_, subject_, min_value_,
          event_out_)) {
        return true;
    }
    return probe_->cv.wait_for (
      lock, std::chrono::milliseconds (timeout_ms_),
      [probe_, expected_event_type_, endpoint_prefix_, subject_, min_value_,
       event_out_]() {
          return consume_matching_service_event_locked (
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

static bool wait_for_service_event_exact_value (
  service_monitor_probe_t *probe_,
  uint32_t expected_event_type_,
  const char *endpoint_prefix_,
  const char *subject_,
  int expected_value_,
  zlink_service_event_t *event_out_,
  int timeout_ms_)
{
    if (!probe_)
        return false;

    std::unique_lock<std::mutex> lock (probe_->mutex);
    if (consume_exact_service_event_locked (
          probe_, expected_event_type_, endpoint_prefix_, subject_,
          expected_value_, event_out_)) {
        return true;
    }
    return probe_->cv.wait_for (
      lock, std::chrono::milliseconds (timeout_ms_),
      [probe_, expected_event_type_, endpoint_prefix_, subject_,
       expected_value_, event_out_]() {
          return consume_exact_service_event_locked (
            probe_, expected_event_type_, endpoint_prefix_, subject_,
            expected_value_, event_out_);
      });
}

static void spot_probe_handler (const zlink_routing_id_t *,
                                const char *topic_,
                                size_t topic_len_,
                                zlink_msg_t *parts_,
                                size_t part_count_)
{
    spot_probe_t *probe = find_spot_probe_for_current_dispatch ();
    if (!probe) {
        for (size_t i = 0; i < part_count_; ++i)
            zlink_msg_close (&parts_[i]);
        return;
    }

    spot_probe_message_t message;
    if (topic_ && topic_len_ > 0)
        message.topic.assign (topic_, topic_len_);
    for (size_t i = 0; i < part_count_; ++i) {
        zlink_msg_t *part = &parts_[i];
        message.parts.push_back (
          std::string (static_cast<const char *> (zlink_msg_data (part)),
                       zlink_msg_size (part)));
    }

    std::lock_guard<std::mutex> lock (probe->mutex);
    probe->messages.push_back (message);

    for (size_t i = 0; i < part_count_; ++i)
        zlink_msg_close (&parts_[i]);
}

static bool wait_for_spot_message_bytes (spot_probe_t *probe_,
                                         const char *topic_,
                                         const char *payload_,
                                         size_t payload_size_,
                                         int timeout_ms_)
{
    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::milliseconds (timeout_ms_);
    while (std::chrono::steady_clock::now () < deadline) {
        {
            std::lock_guard<std::mutex> lock (probe_->mutex);
            for (std::vector<spot_probe_message_t>::iterator it =
                   probe_->messages.begin ();
                 it != probe_->messages.end (); ++it) {
                if (it->topic == topic_ && it->parts.size () == 1
                    && it->parts[0].size () == payload_size_
                    && memcmp (it->parts[0].data (), payload_, payload_size_)
                         == 0) {
                    probe_->messages.erase (it);
                    return true;
                }
            }
        }
        msleep (10);
    }
    return false;
}

static void ignore_service_monitor_event (const zlink_service_event_t *)
{
}

static bool wait_for_topology_state (void *registry_,
                                     zlink_service_kind_t service_kind_,
                                     const char *service_name_,
                                     const zlink_routing_id_t *routing_id_,
                                     zlink_topology_state_t state_,
                                     int timeout_ms_)
{
    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::milliseconds (timeout_ms_);
    while (std::chrono::steady_clock::now () < deadline) {
        zlink_registry_topology_filter_t filter;
        memset (&filter, 0, sizeof (filter));
        filter.service_kind = service_kind_;
        if (service_name_)
            strncpy (filter.service_name, service_name_,
                     sizeof (filter.service_name) - 1);
        if (routing_id_)
            filter.routing_id = *routing_id_;

        size_t count = 0;
        if (zlink_registry_topology_query (registry_, &filter, NULL, &count) == 0
            && count > 0) {
            std::vector<zlink_registry_topology_entry_t> entries (count);
            if (zlink_registry_topology_query (registry_, &filter, &entries[0],
                                               &count)
                == 0) {
                for (size_t i = 0; i < count; ++i) {
                    if (entries[i].state == state_)
                        return true;
                }
            }
        }
        msleep (10);
    }
    return false;
}

static void test_spot_pub_sub_options_and_routing_ids ()
{
    step_log ("pub_sub_options: create ctx");
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    TEST_ASSERT_NULL (zlink_spot_node_new (ctx, "spot-null-handler", NULL));
    TEST_ASSERT_EQUAL_INT (EINVAL, zlink_errno ());

    step_log ("pub_sub_options: create nodes");
    void *pub_node = create_spot_node (ctx, "spot-test");
    void *sub_node = create_spot_node (ctx, "spot-test");
    TEST_ASSERT_NOT_NULL (pub_node);
    TEST_ASSERT_NOT_NULL (sub_node);
    TEST_ASSERT_NULL (zlink_spot_new (pub_node, NULL));
    TEST_ASSERT_EQUAL_INT (EINVAL, zlink_errno ());

    step_log ("pub_sub_options: create handles");
    void *pub = create_spot_pub_handle (pub_node);
    void *sub = create_spot_sub_handle (sub_node);
    TEST_ASSERT_NOT_NULL (pub);
    TEST_ASSERT_NOT_NULL (sub);
    TEST_ASSERT_EQUAL_INT (
      -1, zlink_spot_node_set_send_ready_handler (pub_node, NULL));
    TEST_ASSERT_EQUAL_INT (EINVAL, zlink_errno ());
    TEST_ASSERT_EQUAL_INT (-1, zlink_spot_set_send_ready_handler (pub, NULL));
    TEST_ASSERT_EQUAL_INT (EINVAL, zlink_errno ());

    const int pub_sndhwm = 222;
    const int pub_sndtimeo = 90;
    const int sub_rcvhwm = 333;
    const int linger = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_set_pub_option (pub, ZLINK_SPOT_PUB_OPT_SNDHWM, &pub_sndhwm,
                                 sizeof (pub_sndhwm)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_set_pub_option (pub, ZLINK_SPOT_PUB_OPT_SNDTIMEO,
                                 &pub_sndtimeo, sizeof (pub_sndtimeo)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_set_pub_option (pub, ZLINK_SPOT_PUB_OPT_LINGER, &linger,
                                 sizeof (linger)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_set_sub_option (sub, ZLINK_SPOT_SUB_OPT_RCVHWM, &sub_rcvhwm,
                                 sizeof (sub_rcvhwm)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_set_sub_option (sub, ZLINK_SPOT_SUB_OPT_LINGER, &linger,
                                 sizeof (linger)));

    step_log ("pub_sub_options: bind/connect");
    char endpoint[MAX_SOCKET_STRING];
    int endpoint_seed = 22760;
    TEST_ASSERT_SUCCESS_ERRNO (bind_spot_node_with_port_seed (
      pub_node, "tcp://127.0.0.1:", &endpoint_seed, endpoint,
      sizeof (endpoint)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_connect_peer_pub (sub_node, endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_subscribe (sub, "svc-int"));
    spot_probe_t *sub_probe = new spot_probe_t;
    TEST_ASSERT_SUCCESS_ERRNO (attach_spot_probe (sub, sub_probe));
    msleep (100);

    zlink_peer_info_t peers[4];
    size_t peer_count = 4;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_peers_pub (pub, peers, &peer_count));
    TEST_ASSERT_TRUE (peer_count > 0);
    peer_count = 4;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_peers_sub (sub, peers, &peer_count));
    TEST_ASSERT_TRUE (peer_count > 0);

    step_log ("pub_sub_options: send-ready guards");
    std::atomic<int> ready_calls (0);
    g_spot_ready_subject = NULL;
    g_spot_ready_calls = &ready_calls;
    g_spot_ready_publish_rc = -999;
    g_spot_ready_publish_errno = 0;

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_set_send_ready_handler (
      pub_node, &spot_counting_ready_handler));
    TEST_ASSERT_EQUAL_INT (
      -1, zlink_spot_node_set_send_ready_handler (pub_node, NULL));
    TEST_ASSERT_EQUAL_INT (EINVAL, zlink_errno ());

    zlink::spot_node_t *pub_node_impl =
      static_cast<zlink::spot_node_t *> (pub_node);
    zlink::spot_pub_t *pub_impl = pub_node_impl->ensure_default_pub ();
    TEST_ASSERT_NOT_NULL (pub_impl);
    pub_impl->invoke_send_ready_for_testing ();

    TEST_ASSERT_EQUAL_INT (1, ready_calls.load ());
    TEST_ASSERT_EQUAL_PTR (pub_node, g_spot_ready_subject);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_set_send_ready_handler (
      pub_node, &spot_publish_from_ready_handler));
    pub_impl->invoke_send_ready_for_testing ();

    TEST_ASSERT_EQUAL_INT (2, ready_calls.load ());
    TEST_ASSERT_EQUAL_PTR (pub_node, g_spot_ready_subject);
    TEST_ASSERT_EQUAL_INT (0, g_spot_ready_publish_rc);
    TEST_ASSERT_EQUAL_INT (0, g_spot_ready_publish_errno);

    step_log ("pub_sub_options: send-ready reentrant replace");
    std::atomic<int> reentrant_ready_calls (0);
    g_spot_reentrant_ready_subject = NULL;
    g_spot_reentrant_ready_calls = &reentrant_ready_calls;
    g_spot_reentrant_ready_rc = 0;
    g_spot_reentrant_ready_errno = 0;

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_set_send_ready_handler (
      pub_node, &spot_reentrant_ready_handler));
    pub_impl->invoke_send_ready_for_testing ();

    TEST_ASSERT_EQUAL_INT (1, reentrant_ready_calls.load ());
    TEST_ASSERT_EQUAL_PTR (pub_node, g_spot_reentrant_ready_subject);
    TEST_ASSERT_EQUAL_INT (-1, g_spot_reentrant_ready_rc);
    TEST_ASSERT_EQUAL_INT (EDEADLK, g_spot_reentrant_ready_errno);

    step_log ("pub_sub_options: publish");
    TEST_ASSERT_SUCCESS_ERRNO (
      publish_text (&zlink_spot_publish, pub, "svc-int", "pong", 0));
    TEST_ASSERT_TRUE (
      wait_for_spot_message_bytes (sub_probe, "svc-int", "pong", 4, 1000));

    step_log ("pub_sub_options: unsubscribe/disconnect");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_unsubscribe (sub, "svc-int"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_disconnect_peer_pub (sub_node, endpoint));
    msleep (50);

    // No remove API exists for send-ready handlers. Clear test-owned probe
    // pointers before teardown so late callbacks cannot touch stack state.
    g_spot_reentrant_ready_subject = NULL;
    g_spot_reentrant_ready_calls = NULL;
    g_spot_ready_subject = NULL;
    g_spot_ready_calls = NULL;

    step_log ("pub_sub_options: destroy handles");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&sub));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&pub));
    step_log ("pub_sub_options: destroy nodes");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&sub_node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&pub_node));
    step_log ("pub_sub_options: destroy ctx");
    destroy_test_ctx (ctx);
}

static void test_spot_monitors_and_monitor_poller ()
{
    step_log ("monitors: create ctx");
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    zlink::ctx_t *ctx_impl = static_cast<zlink::ctx_t *> (ctx);
    TEST_ASSERT_NOT_NULL (ctx_impl);

    step_log ("monitors: create nodes");
    void *pub_node = create_spot_node (ctx, "spot-test");
    void *sub_node = create_spot_node (ctx, "spot-test");
    TEST_ASSERT_NOT_NULL (pub_node);
    TEST_ASSERT_NOT_NULL (sub_node);

    step_log ("monitors: create handles");
    void *pub = create_spot_pub_handle (pub_node);
    void *sub = create_spot_sub_handle (sub_node);
    TEST_ASSERT_NOT_NULL (pub);
    TEST_ASSERT_NOT_NULL (sub);
    const size_t socket_count_before_null_monitor_checks =
      ctx_impl->socket_count ();

    TEST_ASSERT_NULL (zlink_spot_monitor_open (
      sub, ZLINK_SPOT_ROLE_SUB, ZLINK_MONITOR_EVENT_READY, NULL));
    TEST_ASSERT_EQUAL_INT (EINVAL, zlink_errno ());
    TEST_ASSERT_NULL (zlink_spot_monitor_open (
      pub, ZLINK_SPOT_ROLE_PUB, ZLINK_MONITOR_EVENT_READY, NULL));
    TEST_ASSERT_EQUAL_INT (EINVAL, zlink_errno ());
    TEST_ASSERT_NULL (zlink_spot_node_monitor_open (
      sub_node, ZLINK_SPOT_ROLE_SUB, ZLINK_MONITOR_EVENT_READY, NULL));
    TEST_ASSERT_EQUAL_INT (EINVAL, zlink_errno ());
    TEST_ASSERT_NULL (zlink_spot_node_monitor_open (
      pub_node, ZLINK_SPOT_ROLE_PUB, ZLINK_MONITOR_EVENT_READY, NULL));
    TEST_ASSERT_EQUAL_INT (EINVAL, zlink_errno ());
    TEST_ASSERT_SUCCESS_ERRNO (
      ctx_impl->wait_for_socket_count_at_most (
        socket_count_before_null_monitor_checks, 1000));
    TEST_ASSERT_EQUAL_UINT (
      socket_count_before_null_monitor_checks, ctx_impl->socket_count ());

    spot_probe_t *sub_probe = new spot_probe_t;
    TEST_ASSERT_SUCCESS_ERRNO (attach_spot_probe (sub, sub_probe));

    service_monitor_probe_t sub_monitor_probe;
    service_monitor_probe_t pub_monitor_probe;
    g_service_monitor_probe_a = &sub_monitor_probe;
    g_service_monitor_probe_b = &pub_monitor_probe;

    void *sub_monitor =
      zlink_spot_monitor_open (sub, ZLINK_SPOT_ROLE_SUB,
                               ZLINK_MONITOR_EVENT_READY
                                 | ZLINK_MONITOR_EVENT_LOST
                                 | ZLINK_MONITOR_EVENT_PEER_UP
                                 | ZLINK_MONITOR_EVENT_PEER_DOWN
                                 | ZLINK_MONITOR_EVENT_CLOSED
                                 | ZLINK_MONITOR_EVENT_ERROR
                                 | ZLINK_SPOT_SUB_FILTER_APPLIED
                                 | ZLINK_SPOT_SUB_SUBSCRIPTION_READY
                                 | ZLINK_SPOT_SUB_DELIVERY_READY_CHANGED,
                               queued_service_monitor_handler_a);
    void *pub_monitor =
      zlink_spot_monitor_open (pub, ZLINK_SPOT_ROLE_PUB,
                               ZLINK_MONITOR_EVENT_READY
                                 | ZLINK_MONITOR_EVENT_PEER_UP
                                 | ZLINK_MONITOR_EVENT_LOST
                                 | ZLINK_MONITOR_EVENT_PEER_DOWN
                                 | ZLINK_MONITOR_EVENT_CLOSED
                                 | ZLINK_MONITOR_EVENT_ERROR
                                 | ZLINK_SPOT_PUB_DELIVERY_READY_CHANGED,
                               queued_service_monitor_handler_b);
    TEST_ASSERT_NOT_NULL (sub_monitor);
    TEST_ASSERT_NOT_NULL (pub_monitor);

    step_log ("monitors: bind/connect");
    char endpoint[MAX_SOCKET_STRING];
    int endpoint_seed = 22761;
    TEST_ASSERT_SUCCESS_ERRNO (bind_spot_node_with_port_seed (
      pub_node, "tcp://127.0.0.1:", &endpoint_seed, endpoint,
      sizeof (endpoint)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_connect_peer_pub (sub_node, endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_subscribe (sub, "svc-mon"));
    zlink_service_event_t filter_event;
    zlink_service_event_t sub_ready_event;
    zlink_service_event_t pub_ready_event;
    TEST_ASSERT_TRUE (wait_for_service_event_match (
      &sub_monitor_probe, ZLINK_SPOT_SUB_FILTER_APPLIED, NULL, "svc-mon", -1,
      &filter_event, 3000));
    TEST_ASSERT_TRUE ((filter_event.detail_flags & ZLINK_EVENT_DETAIL_SUBJECT)
                      != 0);
    TEST_ASSERT_TRUE (
      (filter_event.detail_flags & ZLINK_EVENT_DETAIL_SUBJECT_KIND) != 0);
    TEST_ASSERT_EQUAL_UINT32 (ZLINK_SERVICE_EVENT_SUBJECT_TOPIC,
                              filter_event.subject_kind);
    TEST_ASSERT_TRUE (wait_for_service_event_match (
      &sub_monitor_probe, ZLINK_SPOT_SUB_DELIVERY_READY_CHANGED, endpoint,
      "svc-mon", 1, &sub_ready_event, 3000));
    TEST_ASSERT_EQUAL_UINT32 (1u, sub_ready_event.value);
    TEST_ASSERT_TRUE (wait_for_service_event_match (
      &pub_monitor_probe, ZLINK_SPOT_PUB_DELIVERY_READY_CHANGED, NULL,
      "svc-mon", 1, &pub_ready_event, 3000));
    TEST_ASSERT_EQUAL_UINT32 (1u, pub_ready_event.value);

    step_log ("monitors: publish");
    TEST_ASSERT_SUCCESS_ERRNO (
      publish_text (&zlink_spot_publish, pub, "svc-mon", "payload", 0));
    TEST_ASSERT_TRUE (
      wait_for_spot_message_bytes (sub_probe, "svc-mon", "payload", 7, 1000));

    step_log ("monitors: pattern subscribe");
    zlink_service_event_t pattern_filter_event;
    zlink_service_event_t pattern_ready_event;
    zlink_service_event_t pattern_pub_ready_event;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_subscribe_pattern (sub, "svc-pat*"));
    TEST_ASSERT_TRUE (wait_for_service_event_match (
      &sub_monitor_probe, ZLINK_SPOT_SUB_FILTER_APPLIED, NULL, "svc-pat*", -1,
      &pattern_filter_event, 3000));
    TEST_ASSERT_EQUAL_UINT32 (ZLINK_SERVICE_EVENT_SUBJECT_PATTERN,
                              pattern_filter_event.subject_kind);
    TEST_ASSERT_TRUE (wait_for_service_event_match (
      &sub_monitor_probe, ZLINK_SPOT_SUB_DELIVERY_READY_CHANGED, endpoint,
      "svc-pat*", 1, &pattern_ready_event, 3000));
    TEST_ASSERT_EQUAL_UINT32 (ZLINK_SERVICE_EVENT_SUBJECT_PATTERN,
                              pattern_ready_event.subject_kind);
    TEST_ASSERT_TRUE (wait_for_service_event_match (
      &pub_monitor_probe, ZLINK_SPOT_PUB_DELIVERY_READY_CHANGED, NULL,
      "svc-pat", 1, &pattern_pub_ready_event, 3000));
    TEST_ASSERT_EQUAL_UINT32 (1u, pattern_pub_ready_event.value);
    TEST_ASSERT_SUCCESS_ERRNO (
      publish_text (&zlink_spot_publish, pub, "svc-pat-42", "pattern", 0));
    TEST_ASSERT_TRUE (wait_for_spot_message_bytes (sub_probe, "svc-pat-42",
                                                   "pattern", 7, 1000));

    step_log ("monitors: disconnect");
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_disconnect_peer_pub (sub_node, endpoint));
    zlink_service_event_t pub_lost_event;
    zlink_service_event_t sub_lost_event;
    TEST_ASSERT_TRUE (wait_for_service_event_exact_value (
      &pub_monitor_probe, ZLINK_SPOT_PUB_DELIVERY_READY_CHANGED, NULL,
      "svc-mon", 0, &pub_lost_event, 3000));
    TEST_ASSERT_EQUAL_UINT32 (0u, pub_lost_event.value);
    TEST_ASSERT_TRUE (wait_for_service_event_exact_value (
      &sub_monitor_probe, ZLINK_SPOT_SUB_DELIVERY_READY_CHANGED, NULL,
      "svc-mon", 0, &sub_lost_event, 3000));
    TEST_ASSERT_EQUAL_UINT32 (0u, sub_lost_event.value);

    step_log ("monitors: destroy handles");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&sub));
    TEST_ASSERT_TRUE (wait_for_service_event (
      &sub_monitor_probe, ZLINK_MONITOR_EVENT_CLOSED, NULL, 3000));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&pub));
    TEST_ASSERT_TRUE (wait_for_service_event (
      &pub_monitor_probe, ZLINK_MONITOR_EVENT_CLOSED, NULL, 3000));

    step_log ("monitors: close monitors");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_service_monitor_close (&sub_monitor));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_service_monitor_close (&pub_monitor));
    step_log ("monitors: destroy nodes");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&sub_node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&pub_node));
    TEST_ASSERT_SUCCESS_ERRNO (ctx_impl->wait_for_socket_count_at_most (0, 3000));
    TEST_ASSERT_EQUAL_UINT (0u, ctx_impl->socket_count ());
    g_service_monitor_probe_a = NULL;
    g_service_monitor_probe_b = NULL;
    destroy_test_ctx (ctx);
}

static void test_spot_node_direct_apis_and_explicit_handles_interop ()
{
    step_log ("explicit_handles: create ctx");
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    step_log ("explicit_handles: create nodes");
    void *pub_node = create_spot_node (ctx, "spot-test");
    void *sub_node = create_spot_node (ctx, "spot-test");
    TEST_ASSERT_NOT_NULL (pub_node);
    TEST_ASSERT_NOT_NULL (sub_node);

    step_log ("explicit_handles: create handles");
    void *pub = create_spot_pub_handle (pub_node);
    void *sub = create_spot_sub_handle (sub_node);
    TEST_ASSERT_NOT_NULL (pub);
    TEST_ASSERT_NOT_NULL (sub);

    char endpoint[MAX_SOCKET_STRING];
    int endpoint_seed = 22762;
    TEST_ASSERT_SUCCESS_ERRNO (bind_spot_node_with_port_seed (
      pub_node, "tcp://127.0.0.1:", &endpoint_seed, endpoint,
      sizeof (endpoint)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_connect_peer_pub (sub_node, endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_subscribe (sub_node, "svc-node"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_subscribe (sub, "svc-node"));
    spot_probe_t *sub_probe = new spot_probe_t;
    TEST_ASSERT_SUCCESS_ERRNO (attach_spot_probe (sub, sub_probe));
    msleep (100);
    TEST_ASSERT_SUCCESS_ERRNO (
      publish_text (&zlink_spot_publish, pub, "svc-node", "node", 0));
    TEST_ASSERT_TRUE (
      wait_for_spot_message_bytes (sub_probe, "svc-node", "node", 4, 1000));

    step_log ("explicit_handles: destroy handles");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&sub));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&pub));
    step_log ("explicit_handles: destroy nodes");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&sub_node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&pub_node));
    destroy_test_ctx (ctx);
}

static void test_spot_node_send_ready_handler_isolated_by_service ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *node_a = create_spot_node (ctx, "spot-ready-a");
    void *node_b = create_spot_node (ctx, "spot-ready-b");
    TEST_ASSERT_NOT_NULL (node_a);
    TEST_ASSERT_NOT_NULL (node_b);

    send_ready_probe_t probe_a;
    send_ready_probe_t probe_b;
    send_ready_probe_t probe_replace;
    g_send_ready_probe_a = &probe_a;
    g_send_ready_probe_b = &probe_b;
    g_send_ready_probe_replace = &probe_replace;

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_set_send_ready_handler (
      node_a, &send_ready_probe_handler_a));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_set_send_ready_handler (
      node_b, &send_ready_probe_handler_b));

    zlink::spot_node_t *node_a_impl =
      static_cast<zlink::spot_node_t *> (node_a);
    zlink::spot_node_t *node_b_impl =
      static_cast<zlink::spot_node_t *> (node_b);
    zlink::spot_pub_t *pub_a = node_a_impl->ensure_default_pub ();
    zlink::spot_pub_t *pub_b = node_b_impl->ensure_default_pub ();
    TEST_ASSERT_NOT_NULL (pub_a);
    TEST_ASSERT_NOT_NULL (pub_b);

    pub_a->invoke_send_ready_for_testing ();
    TEST_ASSERT_EQUAL_INT (1, probe_a.calls.load ());
    TEST_ASSERT_EQUAL_PTR (node_a, probe_a.last_subject);
    TEST_ASSERT_EQUAL_INT (0, probe_b.calls.load ());

    pub_b->invoke_send_ready_for_testing ();
    TEST_ASSERT_EQUAL_INT (1, probe_a.calls.load ());
    TEST_ASSERT_EQUAL_INT (1, probe_b.calls.load ());
    TEST_ASSERT_EQUAL_PTR (node_b, probe_b.last_subject);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_set_send_ready_handler (
      node_a, &send_ready_probe_handler_replace));

    pub_b->invoke_send_ready_for_testing ();
    TEST_ASSERT_EQUAL_INT (0, probe_replace.calls.load ());
    TEST_ASSERT_EQUAL_INT (2, probe_b.calls.load ());
    TEST_ASSERT_EQUAL_PTR (node_b, probe_b.last_subject);

    pub_a->invoke_send_ready_for_testing ();
    TEST_ASSERT_EQUAL_INT (1, probe_a.calls.load ());
    TEST_ASSERT_EQUAL_INT (1, probe_replace.calls.load ());
    TEST_ASSERT_EQUAL_PTR (node_a, probe_replace.last_subject);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node_b));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node_a));
    destroy_test_ctx (ctx);
}

static void test_spot_topology_summary_lifecycle ()
{
    step_log ("topology_summary: create ctx");
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    step_log ("topology_summary: create registry");
    int registry_seed = 22670;
    char registry_pub[MAX_SOCKET_STRING];
    char registry_router[MAX_SOCKET_STRING];
    void *registry = create_started_registry_with_port_seed (
      ctx, &registry_seed, registry_pub, sizeof (registry_pub), registry_router,
      sizeof (registry_router));
    TEST_ASSERT_NOT_NULL (registry);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_registry_set_broadcast_interval (registry, 50));

    step_log ("topology_summary: create discovery");
    void *discovery =
      zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_SPOT);
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_set_routing_id (discovery, "spot-summary-disc", 17));
    TEST_ASSERT_SUCCESS_ERRNO (connect_discovery_registry_with_retry (
      discovery, registry_router, 2000));
    step_log ("topology_summary: create node");
    void *node = create_spot_node (ctx, "svc-summary");
    TEST_ASSERT_NOT_NULL (node);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_attach_discovery (node, discovery));

    step_log ("topology_summary: bind node");
    char endpoint[MAX_SOCKET_STRING];
    int endpoint_seed = 22672;
    TEST_ASSERT_SUCCESS_ERRNO (bind_spot_node_with_port_seed (
      node, "tcp://127.0.0.1:", &endpoint_seed, endpoint,
      sizeof (endpoint)));

    step_log ("topology_summary: create unified handle");
    void *spot = create_spot_handle (node, &ignore_spot_handler);
    TEST_ASSERT_NOT_NULL (spot);

    step_log ("topology_summary: wait initial states");
    TEST_ASSERT_TRUE (wait_for_topology_state (
      registry, ZLINK_SERVICE_KIND_SPOT_PUB, "svc-summary", NULL,
      ZLINK_TOPOLOGY_STATE_READY, 2000));
    TEST_ASSERT_TRUE (wait_for_topology_state (
      registry, ZLINK_SERVICE_KIND_SPOT_SUB, "svc-summary", NULL,
      ZLINK_TOPOLOGY_STATE_CONNECTING, 2000));

    step_log ("topology_summary: subscribe");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_subscribe (spot, "svc-summary"));
    TEST_ASSERT_TRUE (wait_for_topology_state (
      registry, ZLINK_SERVICE_KIND_SPOT_SUB, "svc-summary", NULL,
      ZLINK_TOPOLOGY_STATE_READY, 2000));

    step_log ("topology_summary: destroy unified handle");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot));
    TEST_ASSERT_TRUE (wait_for_topology_state (
      registry, ZLINK_SERVICE_KIND_SPOT_SUB, "svc-summary", NULL,
      ZLINK_TOPOLOGY_STATE_STOPPED, 2000));
    TEST_ASSERT_TRUE (wait_for_topology_state (
      registry, ZLINK_SERVICE_KIND_SPOT_PUB, "svc-summary", NULL,
      ZLINK_TOPOLOGY_STATE_STOPPED, 2000));

    step_log ("topology_summary: destroy node");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    step_log ("topology_summary: destroy discovery");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    step_log ("topology_summary: destroy registry");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
    destroy_test_ctx (ctx);
}

static void test_spot_register_null_derivation_and_wildcard_rejection ()
{
    if (!zlink_has ("tcp")) {
        TEST_IGNORE_MESSAGE ("TCP not available");
        return;
    }

    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    int registry_seed = 22618;
    char registry_pub[MAX_SOCKET_STRING];
    char registry_router[MAX_SOCKET_STRING];
    void *registry = create_started_registry_with_port_seed (
      ctx, &registry_seed, registry_pub, sizeof (registry_pub), registry_router,
      sizeof (registry_router));
    TEST_ASSERT_NOT_NULL (registry);

    void *discovery =
      zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_SPOT);
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_SUCCESS_ERRNO (connect_discovery_registry_with_retry (
      discovery, registry_router, 2000));
    TEST_ASSERT_TRUE (wait_for_registry_uplink (discovery, 2000));

    void *ok_node = create_spot_node (ctx, "svc-regnull");
    void *wild_node = create_spot_node (ctx, "svc-regnull");
    TEST_ASSERT_NOT_NULL (ok_node);
    TEST_ASSERT_NOT_NULL (wild_node);

    char concrete_endpoint[MAX_SOCKET_STRING];
    int concrete_seed = 22620;
    TEST_ASSERT_SUCCESS_ERRNO (bind_spot_node_with_port_seed (
      ok_node, "tcp://127.0.0.1:", &concrete_seed, concrete_endpoint,
      sizeof (concrete_endpoint)));
    char wildcard_endpoint[MAX_SOCKET_STRING];
    snprintf (wildcard_endpoint, sizeof (wildcard_endpoint), "tcp://*:%d",
              test_port (22621));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_bind (wild_node, wildcard_endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_attach_discovery (ok_node, discovery));
    TEST_ASSERT_TRUE (wait_for_provider_count (registry, "svc-regnull", 1,
                                               5000));
    TEST_ASSERT_EQUAL_INT (
      -1, zlink_spot_node_attach_discovery (wild_node, discovery));
    TEST_ASSERT_EQUAL_INT (EINVAL, zlink_errno ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&wild_node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&ok_node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
    destroy_test_ctx (ctx);
}

static void test_spot_tls_settings_lock_after_bind_connect_and_register ()
{
    if (!zlink_has ("tls")) {
        TEST_IGNORE_MESSAGE ("TLS not available");
        return;
    }

    step_log ("tls_lock: create ctx");
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    tls_test_files_t files = make_tls_test_files ();

    step_log ("tls_lock: create registry");
    int registry_seed = 22625;
    char registry_pub[MAX_SOCKET_STRING];
    char registry_router[MAX_SOCKET_STRING];
    void *registry = create_started_registry_with_port_seed (
      ctx, &registry_seed, registry_pub, sizeof (registry_pub), registry_router,
      sizeof (registry_router));
    TEST_ASSERT_NOT_NULL (registry);

    step_log ("tls_lock: create discovery");
    void *discovery =
      zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_SPOT);
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_SUCCESS_ERRNO (connect_discovery_registry_with_retry (
      discovery, registry_router, 2000));
    TEST_ASSERT_TRUE (wait_for_registry_uplink (discovery, 2000));

    step_log ("tls_lock: create nodes");
    void *server_node = create_spot_node (ctx, "spot-test");
    void *client_node = create_spot_node (ctx, "spot-test");
    void *reg_node = create_spot_node (ctx, "svc-tlslock");
    TEST_ASSERT_NOT_NULL (server_node);
    TEST_ASSERT_NOT_NULL (client_node);
    TEST_ASSERT_NOT_NULL (reg_node);

    char tls_endpoint[MAX_SOCKET_STRING];
    int tls_seed = 22622;
    char reg_endpoint[MAX_SOCKET_STRING];
    int reg_seed = 22623;

    step_log ("tls_lock: bind server tls");
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_set_tls_server (server_node, files.server_cert.c_str (),
                                      files.server_key.c_str ()));
    TEST_ASSERT_SUCCESS_ERRNO (bind_spot_node_with_port_seed (
      server_node, "tls://localhost:", &tls_seed, tls_endpoint,
      sizeof (tls_endpoint)));
    TEST_ASSERT_EQUAL_INT (
      -1,
      zlink_spot_node_set_tls_server (server_node, files.server_cert.c_str (),
                                      files.server_key.c_str ()));
    TEST_ASSERT_EQUAL_INT (EBUSY, zlink_errno ());

    step_log ("tls_lock: connect client tls");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_set_tls_client (
      client_node, files.ca_cert.c_str (), "localhost", 0));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_connect_peer_pub (client_node, tls_endpoint));
    TEST_ASSERT_EQUAL_INT (
      -1, zlink_spot_node_set_tls_client (client_node, files.ca_cert.c_str (),
                                          "localhost", 0));
    TEST_ASSERT_EQUAL_INT (EBUSY, zlink_errno ());

    step_log ("tls_lock: bind reg node and attach discovery");
    TEST_ASSERT_SUCCESS_ERRNO (bind_spot_node_with_port_seed (
      reg_node, "tcp://127.0.0.1:", &reg_seed, reg_endpoint,
      sizeof (reg_endpoint)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_attach_discovery (reg_node, discovery));
    step_log ("tls_lock: wait provider");
    TEST_ASSERT_TRUE (wait_for_provider_count (registry, "svc-tlslock", 1,
                                               5000));
    TEST_ASSERT_EQUAL_INT (
      -1, zlink_spot_node_set_tls_client (reg_node, files.ca_cert.c_str (),
                                          "localhost", 0));
    TEST_ASSERT_EQUAL_INT (EBUSY, zlink_errno ());

    step_log ("tls_lock: destroy reg node");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&reg_node));
    step_log ("tls_lock: destroy client node");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&client_node));
    step_log ("tls_lock: destroy server node");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&server_node));
    step_log ("tls_lock: destroy discovery");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    step_log ("tls_lock: destroy registry");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
    step_log ("tls_lock: cleanup tls");
    cleanup_tls_test_files (files);
    destroy_test_ctx (ctx);
}

static void test_spot_late_connect_replays_existing_subscription ()
{
    if (!zlink_has ("tcp")) {
        TEST_IGNORE_MESSAGE ("TCP not available");
        return;
    }

    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *pub_node = create_spot_node (ctx, "spot-test");
    void *sub_node = create_spot_node (ctx, "spot-test");
    TEST_ASSERT_NOT_NULL (pub_node);
    TEST_ASSERT_NOT_NULL (sub_node);

    char endpoint[MAX_SOCKET_STRING];
    int endpoint_seed = 22624;
    TEST_ASSERT_SUCCESS_ERRNO (bind_spot_node_with_port_seed (
      pub_node, "tcp://127.0.0.1:", &endpoint_seed, endpoint,
      sizeof (endpoint)));

    void *pub = create_spot_pub_handle (pub_node);
    void *sub = create_spot_sub_handle (sub_node);
    TEST_ASSERT_NOT_NULL (pub);
    TEST_ASSERT_NOT_NULL (sub);

    spot_probe_t *sub_probe = new spot_probe_t;
    TEST_ASSERT_SUCCESS_ERRNO (attach_spot_probe (sub, sub_probe));
    service_monitor_probe_t sub_monitor_probe;
    service_monitor_probe_t pub_monitor_probe;
    g_service_monitor_probe_a = &sub_monitor_probe;
    g_service_monitor_probe_b = &pub_monitor_probe;
    void *sub_monitor =
      zlink_spot_monitor_open (sub, ZLINK_SPOT_ROLE_SUB,
                               ZLINK_SPOT_SUB_FILTER_APPLIED
                                 | ZLINK_SPOT_SUB_DELIVERY_READY_CHANGED
                                 | ZLINK_MONITOR_EVENT_ERROR,
                               queued_service_monitor_handler_a);
    void *pub_monitor =
      zlink_spot_monitor_open (pub, ZLINK_SPOT_ROLE_PUB,
                               ZLINK_SPOT_PUB_DELIVERY_READY_CHANGED
                                 | ZLINK_MONITOR_EVENT_ERROR,
                               queued_service_monitor_handler_b);
    TEST_ASSERT_NOT_NULL (sub_monitor);
    TEST_ASSERT_NOT_NULL (pub_monitor);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_subscribe (sub, "svc-late"));
    TEST_ASSERT_TRUE (wait_for_service_event_match (
      &sub_monitor_probe, ZLINK_SPOT_SUB_FILTER_APPLIED, NULL, "svc-late", -1,
      NULL, 3000));

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_connect_peer_pub (sub_node, endpoint));
    TEST_ASSERT_TRUE (wait_for_service_event_match (
      &sub_monitor_probe, ZLINK_SPOT_SUB_DELIVERY_READY_CHANGED, endpoint,
      "svc-late", 1, NULL, 5000));
    TEST_ASSERT_TRUE (wait_for_service_event_match (
      &pub_monitor_probe, ZLINK_SPOT_PUB_DELIVERY_READY_CHANGED, NULL,
      "svc-late", 1, NULL, 5000));

    const char *payload = "late-replay";
    TEST_ASSERT_SUCCESS_ERRNO (
      publish_text (&zlink_spot_publish, pub, "svc-late", payload, 0));
    TEST_ASSERT_TRUE_MESSAGE (
      wait_for_spot_message_bytes (sub_probe, "svc-late", payload,
                                   strlen (payload), 3000),
      "late-connect peer did not receive replayed subscription");

    g_service_monitor_probe_a = NULL;
    g_service_monitor_probe_b = NULL;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_service_monitor_close (&sub_monitor));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_service_monitor_close (&pub_monitor));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&sub));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&pub));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&sub_node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&pub_node));
    destroy_test_ctx (ctx);
}

static void test_spot_faulted_node_apis_fail_with_efsm ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *node_handle = create_spot_node (ctx, "spot-test");
    TEST_ASSERT_NOT_NULL (node_handle);

    zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (node_handle);
    node->debug_mark_fault (EIO);

    TEST_ASSERT_EQUAL_INT (
      -1, zlink_spot_node_bind (node_handle, "tcp://127.0.0.1:9"));
    TEST_ASSERT_EQUAL_INT (EFSM, zlink_errno ());
    TEST_ASSERT_EQUAL_PTR (
      NULL, zlink_spot_new (node_handle, &ignore_spot_handler));
    TEST_ASSERT_EQUAL_INT (EFSM, zlink_errno ());
    TEST_ASSERT_EQUAL_PTR (
      NULL, zlink_spot_new (node_handle, &spot_probe_handler));
    TEST_ASSERT_EQUAL_INT (EFSM, zlink_errno ());
    TEST_ASSERT_EQUAL_INT (
      -1, zlink_spot_node_connect_peer_pub (node_handle, "tcp://127.0.0.1:9"));
    TEST_ASSERT_EQUAL_INT (EFSM, zlink_errno ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node_handle));
    destroy_test_ctx (ctx);
}

int main (int, char **)
{
    setup_test_environment (300);

    UNITY_BEGIN ();
#define RUN_SPOT_INTROSPECTION_TEST(name)                                      \
    do {                                                                       \
        if (should_run_named_test (#name))                                     \
            RUN_TEST (name);                                                   \
    } while (0)
    RUN_SPOT_INTROSPECTION_TEST (test_spot_pub_sub_options_and_routing_ids);
    RUN_SPOT_INTROSPECTION_TEST (test_spot_monitors_and_monitor_poller);
    RUN_SPOT_INTROSPECTION_TEST (test_spot_node_direct_apis_and_explicit_handles_interop);
    RUN_SPOT_INTROSPECTION_TEST (test_spot_node_send_ready_handler_isolated_by_service);
    RUN_SPOT_INTROSPECTION_TEST (test_spot_topology_summary_lifecycle);
    RUN_SPOT_INTROSPECTION_TEST (test_spot_register_null_derivation_and_wildcard_rejection);
    RUN_SPOT_INTROSPECTION_TEST (test_spot_tls_settings_lock_after_bind_connect_and_register);
    RUN_SPOT_INTROSPECTION_TEST (test_spot_late_connect_replays_existing_subscription);
    RUN_SPOT_INTROSPECTION_TEST (test_spot_faulted_node_apis_fail_with_efsm);
#undef RUN_SPOT_INTROSPECTION_TEST
    return UNITY_END ();
}
