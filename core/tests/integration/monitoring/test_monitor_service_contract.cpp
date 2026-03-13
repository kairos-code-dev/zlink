/* SPDX-License-Identifier: MPL-2.0 */

#include "testutil.hpp"
#include "testutil_unity.hpp"

#include "../../../src/core/monitor_dispatch_internal.hpp"
#include "../../../src/services/spot/spot_dispatch_internal.hpp"

#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <stdio.h>
#include <string>
#include <string.h>
#include <vector>

namespace
{
struct service_event_probe_t
{
    std::mutex mutex;
    std::condition_variable cv;
    std::vector<zlink_service_event_t> events;
    std::vector<uint64_t> event_times_ms;
};

void dump_service_events (const char *label_, service_event_probe_t *probe_)
{
    if (!label_ || !probe_)
        return;

    std::lock_guard<std::mutex> lock (probe_->mutex);
    fprintf (stderr, "[service-events] %s count=%zu\n", label_,
             probe_->events.size ());
    for (size_t i = 0; i < probe_->events.size (); ++i) {
        const zlink_service_event_t &event = probe_->events[i];
        const uint64_t recorded_ms =
          i < probe_->event_times_ms.size () ? probe_->event_times_ms[i] : 0;
        fprintf (stderr,
                 "[service-events] %s[%llu] t=%llu kind=%u type=%u flags=0x%x "
                 "endpoint=%s subject=%s value=%u status=%d\n",
                 label_, static_cast<unsigned long long> (i),
                 static_cast<unsigned long long> (recorded_ms),
                 static_cast<unsigned int> (event.service_kind),
                 static_cast<unsigned int> (event.event_type),
                 static_cast<unsigned int> (event.detail_flags),
                 (event.detail_flags & ZLINK_EVENT_DETAIL_ENDPOINT) != 0
                   ? event.endpoint
                   : "-",
                 (event.detail_flags & ZLINK_EVENT_DETAIL_SUBJECT) != 0
                   ? event.subject
                   : "-",
                 static_cast<unsigned int> (event.value),
                 static_cast<int> (event.status));
    }
}

struct gateway_delivery_probe_t
{
    gateway_delivery_probe_t () :
        server (NULL),
        request_calls (0),
        reply_calls (0),
        close_failures (0),
        send_failures (0)
    {
        memset (request_payload, 0, sizeof (request_payload));
        memset (reply_payload, 0, sizeof (reply_payload));
    }

    void *server;
    std::mutex mutex;
    std::condition_variable cv;
    int request_calls;
    int reply_calls;
    int close_failures;
    int send_failures;
    char request_payload[64];
    char reply_payload[64];
};

struct spot_delivery_probe_t
{
    spot_delivery_probe_t () : calls (0), close_failures (0)
    {
        memset (topic, 0, sizeof (topic));
        memset (payload, 0, sizeof (payload));
    }

    std::mutex mutex;
    std::condition_variable cv;
    int calls;
    int close_failures;
    char topic[256];
    char payload[256];
};

struct registered_spot_delivery_probe_t
{
    registered_spot_delivery_probe_t () : calls (0), close_failures (0)
    {
        memset (topic, 0, sizeof (topic));
        memset (payload, 0, sizeof (payload));
    }

    std::mutex mutex;
    std::condition_variable cv;
    int calls;
    int close_failures;
    char topic[256];
    char payload[256];
};

struct multi_spot_slot_t
{
    multi_spot_slot_t () : node (NULL), sub (NULL), monitor (NULL) {}

    void *node;
    void *sub;
    void *monitor;
    service_event_probe_t monitor_probe;
    registered_spot_delivery_probe_t delivery_probe;
};

service_event_probe_t *g_service_probe_a = NULL;
service_event_probe_t *g_service_probe_b = NULL;
gateway_delivery_probe_t *g_gateway_probe = NULL;
spot_delivery_probe_t *g_spot_probe = NULL;
std::mutex g_registered_service_monitor_probe_mutex;
std::map<void *, service_event_probe_t *> g_registered_service_monitor_probes;
std::mutex g_registered_spot_probe_mutex;
std::map<void *, registered_spot_delivery_probe_t *> g_registered_spot_probes;

void record_service_event (service_event_probe_t *probe_,
                           const zlink_service_event_t *event_)
{
    if (!probe_ || !event_)
        return;

    if (getenv ("ZLINK_DEBUG_SPOT_MONITOR_EVENTS")
        && (event_->service_kind == ZLINK_SERVICE_KIND_SPOT_SUB
            || event_->service_kind == ZLINK_SERVICE_KIND_SPOT_PUB)) {
        fprintf (stderr,
                 "[spot-monitor-event] kind=%u type=%u flags=0x%x endpoint=%s "
                 "subject=%s value=%u status=%d\n",
                 static_cast<unsigned int> (event_->service_kind),
                 static_cast<unsigned int> (event_->event_type),
                 static_cast<unsigned int> (event_->detail_flags),
                 (event_->detail_flags & ZLINK_EVENT_DETAIL_ENDPOINT) != 0
                   ? event_->endpoint
                   : "-",
                 (event_->detail_flags & ZLINK_EVENT_DETAIL_SUBJECT) != 0
                   ? event_->subject
                   : "-",
                 static_cast<unsigned int> (event_->value),
                 static_cast<int> (event_->status));
    }

    {
        std::lock_guard<std::mutex> lock (probe_->mutex);
        probe_->events.push_back (*event_);
        probe_->event_times_ms.push_back (
          static_cast<uint64_t> (
            std::chrono::duration_cast<std::chrono::milliseconds> (
              std::chrono::steady_clock::now ().time_since_epoch ())
              .count ()));
    }
    probe_->cv.notify_all ();
}

void service_monitor_handler_a (const zlink_service_event_t *event_)
{
    record_service_event (g_service_probe_a, event_);
}

void service_monitor_handler_b (const zlink_service_event_t *event_)
{
    record_service_event (g_service_probe_b, event_);
}

bool event_matches (const zlink_service_event_t &event_,
                    uint32_t event_type_,
                    const char *endpoint_,
                    const char *subject_,
                    int expected_value_)
{
    if (event_.event_type != event_type_)
        return false;

    if (endpoint_) {
        if ((event_.detail_flags & ZLINK_EVENT_DETAIL_ENDPOINT) == 0) {
            if (getenv ("ZLINK_DEBUG_SPOT_MONITOR_MATCH")) {
                fprintf (stderr,
                         "[spot-monitor-match] missing endpoint type=%u "
                         "subject=%s value=%u flags=0x%x\n",
                         static_cast<unsigned int> (event_.event_type),
                         (event_.detail_flags & ZLINK_EVENT_DETAIL_SUBJECT) != 0
                           ? event_.subject
                           : "-",
                         static_cast<unsigned int> (event_.value),
                         static_cast<unsigned int> (event_.detail_flags));
            }
            return false;
        }
        if (strcmp (event_.endpoint, endpoint_) != 0) {
            if (getenv ("ZLINK_DEBUG_SPOT_MONITOR_MATCH")) {
                fprintf (stderr,
                         "[spot-monitor-match] endpoint mismatch expected=%s "
                         "actual=%s type=%u subject=%s value=%u\n",
                         endpoint_, event_.endpoint,
                         static_cast<unsigned int> (event_.event_type),
                         (event_.detail_flags & ZLINK_EVENT_DETAIL_SUBJECT) != 0
                           ? event_.subject
                           : "-",
                         static_cast<unsigned int> (event_.value));
            }
            return false;
        }
    }

    if (subject_) {
        if ((event_.detail_flags & ZLINK_EVENT_DETAIL_SUBJECT) == 0) {
            if (getenv ("ZLINK_DEBUG_SPOT_MONITOR_MATCH")) {
                fprintf (stderr,
                         "[spot-monitor-match] missing subject type=%u "
                         "endpoint=%s value=%u flags=0x%x\n",
                         static_cast<unsigned int> (event_.event_type),
                         (event_.detail_flags & ZLINK_EVENT_DETAIL_ENDPOINT) != 0
                           ? event_.endpoint
                           : "-",
                         static_cast<unsigned int> (event_.value),
                         static_cast<unsigned int> (event_.detail_flags));
            }
            return false;
        }
        if (strcmp (event_.subject, subject_) != 0) {
            if (getenv ("ZLINK_DEBUG_SPOT_MONITOR_MATCH")) {
                fprintf (stderr,
                         "[spot-monitor-match] subject mismatch expected=%s "
                         "actual=%s type=%u endpoint=%s value=%u\n",
                         subject_, event_.subject,
                         static_cast<unsigned int> (event_.event_type),
                         (event_.detail_flags & ZLINK_EVENT_DETAIL_ENDPOINT) != 0
                           ? event_.endpoint
                           : "-",
                         static_cast<unsigned int> (event_.value));
            }
            return false;
        }
    }

    if (expected_value_ >= 0
        && event_.value != static_cast<uint32_t> (expected_value_)) {
        if (getenv ("ZLINK_DEBUG_SPOT_MONITOR_MATCH")) {
            fprintf (stderr,
                     "[spot-monitor-match] value mismatch expected=%d actual=%u "
                     "type=%u endpoint=%s subject=%s\n",
                     expected_value_, static_cast<unsigned int> (event_.value),
                     static_cast<unsigned int> (event_.event_type),
                     (event_.detail_flags & ZLINK_EVENT_DETAIL_ENDPOINT) != 0
                       ? event_.endpoint
                       : "-",
                     (event_.detail_flags & ZLINK_EVENT_DETAIL_SUBJECT) != 0
                       ? event_.subject
                       : "-");
        }
        return false;
    }

    return true;
}

bool wait_for_service_event_match (service_event_probe_t *probe_,
                                   size_t *cursor_,
                                   uint32_t event_type_,
                                   const char *endpoint_,
                                   const char *subject_,
                                   int expected_value_,
                                   zlink_service_event_t *out_,
                                   int timeout_ms_)
{
    if (!probe_ || !cursor_)
        return false;

    std::unique_lock<std::mutex> lock (probe_->mutex);
    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::milliseconds (timeout_ms_);

    while (true) {
        for (size_t i = *cursor_; i < probe_->events.size (); ++i) {
            if (!event_matches (probe_->events[i], event_type_, endpoint_,
                                subject_, expected_value_)) {
                continue;
            }
            if (out_)
                *out_ = probe_->events[i];
            *cursor_ = i + 1;
            return true;
        }

        if (probe_->cv.wait_until (lock, deadline) == std::cv_status::timeout) {
            for (size_t i = *cursor_; i < probe_->events.size (); ++i) {
                if (!event_matches (probe_->events[i], event_type_, endpoint_,
                                    subject_, expected_value_)) {
                    continue;
                }
                if (out_)
                    *out_ = probe_->events[i];
                *cursor_ = i + 1;
                return true;
            }
            return false;
        }
    }
}

bool wait_for_monitor_snapshot_state (
  void *monitor_,
  zlink_monitor_source_kind_t source_kind_,
  zlink_monitor_state_mask_t required_flags_,
  uint32_t min_ready_peer_count_,
  zlink_monitor_snapshot_t *out_,
  int timeout_ms_)
{
    const int step_ms = 10;
    const int attempts = timeout_ms_ / step_ms;
    for (int i = 0; i < attempts; ++i) {
        zlink_monitor_snapshot_t snapshot;
        memset (&snapshot, 0, sizeof (snapshot));
        if (zlink_monitor_snapshot (monitor_, &snapshot) == 0
            && snapshot.source_kind == source_kind_
            && (snapshot.state_flags & required_flags_) == required_flags_
            && snapshot.ready_peer_count >= min_ready_peer_count_) {
            if (out_)
                *out_ = snapshot;
            return true;
        }
        msleep (step_ms);
    }

    zlink_monitor_snapshot_t snapshot;
    memset (&snapshot, 0, sizeof (snapshot));
    if (zlink_monitor_snapshot (monitor_, &snapshot) == 0
        && snapshot.source_kind == source_kind_
        && (snapshot.state_flags & required_flags_) == required_flags_
        && snapshot.ready_peer_count >= min_ready_peer_count_) {
        if (out_)
            *out_ = snapshot;
        return true;
    }
    return false;
}

int remaining_timeout_ms (
  const std::chrono::steady_clock::time_point &deadline_)
{
    const std::chrono::steady_clock::time_point now =
      std::chrono::steady_clock::now ();
    if (now >= deadline_)
        return 0;
    return static_cast<int> (
      std::chrono::duration_cast<std::chrono::milliseconds> (deadline_ - now)
        .count ());
}

void register_service_monitor_probe (void *monitor_,
                                     service_event_probe_t *probe_)
{
    if (!monitor_ || !probe_)
        return;

    std::lock_guard<std::mutex> lock (g_registered_service_monitor_probe_mutex);
    g_registered_service_monitor_probes[monitor_] = probe_;
}

void unregister_service_monitor_probe (void *monitor_)
{
    if (!monitor_)
        return;

    std::lock_guard<std::mutex> lock (g_registered_service_monitor_probe_mutex);
    g_registered_service_monitor_probes.erase (monitor_);
}

service_event_probe_t *find_service_monitor_probe_for_current_dispatch ()
{
    void *monitor = zlink::current_monitor_dispatch_handle ();
    if (!monitor)
        return NULL;

    std::lock_guard<std::mutex> lock (g_registered_service_monitor_probe_mutex);
    std::map<void *, service_event_probe_t *>::iterator it =
      g_registered_service_monitor_probes.find (monitor);
    return it != g_registered_service_monitor_probes.end () ? it->second : NULL;
}

void service_monitor_handler_registered (const zlink_service_event_t *event_)
{
    record_service_event (find_service_monitor_probe_for_current_dispatch (),
                          event_);
}

bool wait_for_service_event_min_value (service_event_probe_t *probe_,
                                       size_t *cursor_,
                                       uint32_t event_type_,
                                       const char *endpoint_,
                                       const char *subject_,
                                       uint32_t min_value_,
                                       zlink_service_event_t *out_,
                                       int timeout_ms_)
{
    if (!probe_ || !cursor_)
        return false;

    std::unique_lock<std::mutex> lock (probe_->mutex);
    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::milliseconds (timeout_ms_);

    while (true) {
        for (size_t i = *cursor_; i < probe_->events.size (); ++i) {
            const zlink_service_event_t &event = probe_->events[i];
            if (event.event_type != event_type_)
                continue;
            if (endpoint_) {
                if ((event.detail_flags & ZLINK_EVENT_DETAIL_ENDPOINT) == 0)
                    continue;
                if (strcmp (event.endpoint, endpoint_) != 0)
                    continue;
            }
            if (subject_) {
                if ((event.detail_flags & ZLINK_EVENT_DETAIL_SUBJECT) == 0)
                    continue;
                if (strcmp (event.subject, subject_) != 0)
                    continue;
            }
            if (event.value < min_value_)
                continue;
            if (out_)
                *out_ = event;
            *cursor_ = i + 1;
            return true;
        }

        if (probe_->cv.wait_until (lock, deadline) == std::cv_status::timeout) {
            for (size_t i = *cursor_; i < probe_->events.size (); ++i) {
                const zlink_service_event_t &event = probe_->events[i];
                if (event.event_type != event_type_)
                    continue;
                if (endpoint_) {
                    if ((event.detail_flags & ZLINK_EVENT_DETAIL_ENDPOINT) == 0)
                        continue;
                    if (strcmp (event.endpoint, endpoint_) != 0)
                        continue;
                }
                if (subject_) {
                    if ((event.detail_flags & ZLINK_EVENT_DETAIL_SUBJECT) == 0)
                        continue;
                    if (strcmp (event.subject, subject_) != 0)
                        continue;
                }
                if (event.value < min_value_)
                    continue;
                if (out_)
                    *out_ = event;
                *cursor_ = i + 1;
                return true;
            }
            return false;
        }
    }
}

void close_parts (gateway_delivery_probe_t *probe_,
                  zlink_msg_t *parts_,
                  size_t part_count_)
{
    for (size_t i = 0; i < part_count_; ++i) {
        if (zlink_msg_close (&parts_[i]) != 0 && probe_) {
            std::lock_guard<std::mutex> lock (probe_->mutex);
            ++probe_->close_failures;
        }
    }
}

void gateway_server_handler (const zlink_routing_id_t *source_rid_,
                             zlink_msg_t *parts_,
                             size_t part_count_)
{
    gateway_delivery_probe_t *probe = g_gateway_probe;
    if (!probe || !source_rid_ || part_count_ == 0) {
        close_parts (probe, parts_, part_count_);
        return;
    }

    {
        std::lock_guard<std::mutex> lock (probe->mutex);
        const size_t size = zlink_msg_size (&parts_[0]);
        const size_t copy_size =
          size < sizeof (probe->request_payload) - 1
            ? size
            : sizeof (probe->request_payload) - 1;
        memcpy (probe->request_payload, zlink_msg_data (&parts_[0]), copy_size);
        probe->request_payload[copy_size] = '\0';
        ++probe->request_calls;
    }
    probe->cv.notify_all ();

    zlink_msg_t reply;
    static const char reply_text[] = "pong";
    if (zlink_msg_init_size (&reply, sizeof (reply_text) - 1) != 0) {
        std::lock_guard<std::mutex> lock (probe->mutex);
        ++probe->send_failures;
        close_parts (probe, parts_, part_count_);
        probe->cv.notify_all ();
        return;
    }
    memcpy (zlink_msg_data (&reply), reply_text, sizeof (reply_text) - 1);
    if (zlink_gateway_send_rid (probe->server, source_rid_, &reply, 1, 0)
        != 0) {
        (void) zlink_msg_close (&reply);
        std::lock_guard<std::mutex> lock (probe->mutex);
        ++probe->send_failures;
    }

    close_parts (probe, parts_, part_count_);
    probe->cv.notify_all ();
}

void gateway_client_handler (const zlink_routing_id_t *,
                             zlink_msg_t *parts_,
                             size_t part_count_)
{
    gateway_delivery_probe_t *probe = g_gateway_probe;
    if (!probe || part_count_ == 0) {
        close_parts (probe, parts_, part_count_);
        return;
    }

    {
        std::lock_guard<std::mutex> lock (probe->mutex);
        const size_t size = zlink_msg_size (&parts_[0]);
        const size_t copy_size =
          size < sizeof (probe->reply_payload) - 1
            ? size
            : sizeof (probe->reply_payload) - 1;
        memcpy (probe->reply_payload, zlink_msg_data (&parts_[0]), copy_size);
        probe->reply_payload[copy_size] = '\0';
        ++probe->reply_calls;
    }

    close_parts (probe, parts_, part_count_);
    probe->cv.notify_all ();
}

bool wait_for_gateway_delivery (gateway_delivery_probe_t *probe_,
                                int timeout_ms_)
{
    if (!probe_)
        return false;

    std::unique_lock<std::mutex> lock (probe_->mutex);
    return probe_->cv.wait_for (
      lock, std::chrono::milliseconds (timeout_ms_),
      [probe_]() { return probe_->request_calls >= 1 && probe_->reply_calls >= 1; });
}

void close_spot_parts (spot_delivery_probe_t *probe_,
                       zlink_msg_t *parts_,
                       size_t part_count_)
{
    for (size_t i = 0; i < part_count_; ++i) {
        if (zlink_msg_close (&parts_[i]) != 0 && probe_) {
            std::lock_guard<std::mutex> lock (probe_->mutex);
            ++probe_->close_failures;
        }
    }
}

void ignore_spot_handler (const zlink_routing_id_t *,
                          const char *,
                          size_t,
                          zlink_msg_t *parts_,
                          size_t part_count_)
{
    close_spot_parts (NULL, parts_, part_count_);
}

void capture_spot_delivery (const zlink_routing_id_t *,
                            const char *topic_,
                            size_t topic_len_,
                            zlink_msg_t *parts_,
                            size_t part_count_)
{
    spot_delivery_probe_t *probe = g_spot_probe;
    if (!probe || !topic_ || part_count_ == 0) {
        close_spot_parts (probe, parts_, part_count_);
        return;
    }

    {
        std::lock_guard<std::mutex> lock (probe->mutex);
        const size_t topic_copy =
          topic_len_ < sizeof (probe->topic) - 1 ? topic_len_
                                                 : sizeof (probe->topic) - 1;
        memcpy (probe->topic, topic_, topic_copy);
        probe->topic[topic_copy] = '\0';

        const size_t size = zlink_msg_size (&parts_[0]);
        const size_t payload_copy =
          size < sizeof (probe->payload) - 1 ? size
                                             : sizeof (probe->payload) - 1;
        memcpy (probe->payload, zlink_msg_data (&parts_[0]), payload_copy);
        probe->payload[payload_copy] = '\0';
        ++probe->calls;
        if (getenv ("ZLINK_DEBUG_SPOT_CAPTURE")) {
            fprintf (stderr,
                     "[spot-capture] calls=%d topic=%s payload=%s size=%zu\n",
                     probe->calls, probe->topic, probe->payload, size);
        }
    }

    close_spot_parts (probe, parts_, part_count_);
    probe->cv.notify_all ();
}

bool wait_for_spot_delivery (spot_delivery_probe_t *probe_,
                             const char *topic_,
                             const char *payload_,
                             int timeout_ms_)
{
    if (!probe_ || !topic_ || !payload_)
        return false;

    std::unique_lock<std::mutex> lock (probe_->mutex);
    return probe_->cv.wait_for (
      lock, std::chrono::milliseconds (timeout_ms_),
      [probe_, topic_, payload_]() {
          return probe_->calls >= 1 && strcmp (probe_->topic, topic_) == 0
                 && strcmp (probe_->payload, payload_) == 0;
      });
}

void register_spot_delivery_probe (void *spot_,
                                   registered_spot_delivery_probe_t *probe_)
{
    if (!spot_ || !probe_)
        return;

    std::lock_guard<std::mutex> lock (g_registered_spot_probe_mutex);
    g_registered_spot_probes[spot_] = probe_;
}

void unregister_spot_delivery_probe (void *spot_)
{
    if (!spot_)
        return;

    std::lock_guard<std::mutex> lock (g_registered_spot_probe_mutex);
    g_registered_spot_probes.erase (spot_);
}

registered_spot_delivery_probe_t *find_registered_spot_delivery_probe ()
{
    void *handle = zlink::current_spot_dispatch_handle ();
    if (!handle)
        return NULL;

    std::lock_guard<std::mutex> lock (g_registered_spot_probe_mutex);
    std::map<void *, registered_spot_delivery_probe_t *>::iterator it =
      g_registered_spot_probes.find (handle);
    return it != g_registered_spot_probes.end () ? it->second : NULL;
}

void capture_registered_spot_delivery (const zlink_routing_id_t *,
                                       const char *topic_,
                                       size_t topic_len_,
                                       zlink_msg_t *parts_,
                                       size_t part_count_)
{
    registered_spot_delivery_probe_t *probe =
      find_registered_spot_delivery_probe ();
    if (!probe || !topic_ || part_count_ == 0) {
        close_spot_parts (NULL, parts_, part_count_);
        return;
    }

    {
        std::lock_guard<std::mutex> lock (probe->mutex);
        const size_t topic_copy =
          topic_len_ < sizeof (probe->topic) - 1 ? topic_len_
                                                 : sizeof (probe->topic) - 1;
        memcpy (probe->topic, topic_, topic_copy);
        probe->topic[topic_copy] = '\0';

        const size_t size = zlink_msg_size (&parts_[0]);
        const size_t payload_copy =
          size < sizeof (probe->payload) - 1 ? size
                                             : sizeof (probe->payload) - 1;
        memcpy (probe->payload, zlink_msg_data (&parts_[0]), payload_copy);
        probe->payload[payload_copy] = '\0';
        ++probe->calls;
    }

    close_spot_parts (NULL, parts_, part_count_);
    probe->cv.notify_all ();
}

bool wait_for_registered_spot_delivery (
  registered_spot_delivery_probe_t *probe_,
  const char *topic_,
  const char *payload_,
  int timeout_ms_)
{
    if (!probe_ || !topic_ || !payload_)
        return false;

    std::unique_lock<std::mutex> lock (probe_->mutex);
    return probe_->cv.wait_for (
      lock, std::chrono::milliseconds (timeout_ms_),
      [probe_, topic_, payload_]() {
          return probe_->calls >= 1 && strcmp (probe_->topic, topic_) == 0
                 && strcmp (probe_->payload, payload_) == 0;
      });
}

int connect_discovery_registry_with_retry (void *discovery_,
                                           const char *endpoint_,
                                           int timeout_ms_)
{
    const int step_ms = 10;
    const int attempts = timeout_ms_ / step_ms;
    for (int i = 0; i < attempts; ++i) {
        if (zlink_discovery_connect_registry (discovery_, endpoint_) == 0)
            return 0;
        if (zlink_errno () != EAGAIN)
            return -1;
        msleep (step_ms);
    }
    errno = EAGAIN;
    return -1;
}

void bind_registry_with_port_seed (void *registry_,
                                   int *seed_,
                                   char *pub_endpoint_,
                                   size_t pub_size_,
                                   char *router_endpoint_,
                                   size_t router_size_)
{
    TEST_ASSERT_NOT_NULL (registry_);
    TEST_ASSERT_NOT_NULL (seed_);
    TEST_ASSERT_NOT_NULL (pub_endpoint_);
    TEST_ASSERT_NOT_NULL (router_endpoint_);

    for (int attempt = 0; attempt < 32; ++attempt) {
        snprintf (pub_endpoint_, pub_size_, "tcp://127.0.0.1:%d",
                  test_port (*seed_));
        snprintf (router_endpoint_, router_size_, "tcp://127.0.0.1:%d",
                  test_port (*seed_ + 1));
        if (zlink_registry_bind (registry_, pub_endpoint_, router_endpoint_)
            == 0) {
            *seed_ += 2;
            return;
        }
        if (errno != EADDRINUSE && errno != EAGAIN)
            break;
        *seed_ += 2;
    }

    TEST_FAIL_MESSAGE ("registry bind failed");
}

void bind_gateway_with_port_seed (void *gateway_,
                                  int *seed_,
                                  char *endpoint_out_,
                                  size_t endpoint_size_)
{
    TEST_ASSERT_NOT_NULL (gateway_);
    TEST_ASSERT_NOT_NULL (seed_);
    TEST_ASSERT_NOT_NULL (endpoint_out_);

    for (int attempt = 0; attempt < 32; ++attempt) {
        snprintf (endpoint_out_, endpoint_size_, "tcp://127.0.0.1:%d",
                  test_port (*seed_));
        if (zlink_gateway_bind (gateway_, endpoint_out_) == 0)
            return;
        if (errno != EADDRINUSE && errno != EAGAIN)
            break;
        ++(*seed_);
    }

    TEST_FAIL_MESSAGE ("gateway bind failed");
}

void bind_spot_node_with_port_seed (void *node_,
                                    int *seed_,
                                    char *endpoint_out_,
                                    size_t endpoint_size_)
{
    TEST_ASSERT_NOT_NULL (node_);
    TEST_ASSERT_NOT_NULL (seed_);
    TEST_ASSERT_NOT_NULL (endpoint_out_);

    for (int attempt = 0; attempt < 32; ++attempt) {
        snprintf (endpoint_out_, endpoint_size_, "tcp://127.0.0.1:%d",
                  test_port (*seed_));
        if (zlink_spot_node_bind (node_, endpoint_out_) == 0)
            return;
        if (errno != EADDRINUSE && errno != EAGAIN)
            break;
        ++(*seed_);
    }

    TEST_FAIL_MESSAGE ("spot node bind failed");
}

void *create_gateway_attached (void *ctx_,
                               void *discovery_,
                               const char *service_name_,
                               const char *routing_id_,
                               zlink_socket_msg_handler_fn handler_)
{
    void *gateway =
      zlink_gateway_new (ctx_, service_name_, routing_id_, handler_);
    TEST_ASSERT_NOT_NULL (gateway);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_gateway_attach_discovery (gateway, discovery_));
    return gateway;
}

void init_text_part (zlink_msg_t *part_, const char *text_)
{
    TEST_ASSERT_NOT_NULL (part_);
    TEST_ASSERT_NOT_NULL (text_);
    const size_t size = strlen (text_);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (part_, size));
    memcpy (zlink_msg_data (part_), text_, size);
}
} // namespace

void setUp ()
{
    setup_test_context ();
    g_service_probe_a = NULL;
    g_service_probe_b = NULL;
    g_gateway_probe = NULL;
    g_spot_probe = NULL;
    {
        std::lock_guard<std::mutex> lock (
          g_registered_service_monitor_probe_mutex);
        g_registered_service_monitor_probes.clear ();
    }
    {
        std::lock_guard<std::mutex> lock (g_registered_spot_probe_mutex);
        g_registered_spot_probes.clear ();
    }
}

void tearDown ()
{
    g_service_probe_a = NULL;
    g_service_probe_b = NULL;
    g_gateway_probe = NULL;
    g_spot_probe = NULL;
    {
        std::lock_guard<std::mutex> lock (
          g_registered_service_monitor_probe_mutex);
        g_registered_service_monitor_probes.clear ();
    }
    {
        std::lock_guard<std::mutex> lock (g_registered_spot_probe_mutex);
        g_registered_spot_probes.clear ();
    }
    teardown_test_context ();
}

void test_gateway_send_ready_changed_implies_first_request_reply ()
{
    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *registry = zlink_registry_new (ctx);
    TEST_ASSERT_NOT_NULL (registry);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_registry_set_broadcast_interval (registry, 50));

    char registry_pub[MAX_SOCKET_STRING];
    char registry_router[MAX_SOCKET_STRING];
    int registry_seed = 22910;
    bind_registry_with_port_seed (registry, &registry_seed, registry_pub,
                                  sizeof (registry_pub), registry_router,
                                  sizeof (registry_router));

    void *server_discovery =
      zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    void *client_discovery =
      zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (server_discovery);
    TEST_ASSERT_NOT_NULL (client_discovery);
    TEST_ASSERT_SUCCESS_ERRNO (connect_discovery_registry_with_retry (
      server_discovery, registry_router, 3000));
    TEST_ASSERT_SUCCESS_ERRNO (connect_discovery_registry_with_retry (
      client_discovery, registry_router, 3000));

    void *server = create_gateway_attached (
      ctx, server_discovery, "svc-monitor-contract", "gw-server-contract",
      &gateway_server_handler);
    void *client = create_gateway_attached (
      ctx, client_discovery, "svc-monitor-contract", "gw-client-contract",
      &gateway_client_handler);

    const int timeout_ms = 200;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_set_option (
      server, ZLINK_GATEWAY_OPT_SNDTIMEO, &timeout_ms, sizeof (timeout_ms)));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_set_option (
      client, ZLINK_GATEWAY_OPT_SNDTIMEO, &timeout_ms, sizeof (timeout_ms)));

    service_event_probe_t server_monitor_probe;
    service_event_probe_t client_monitor_probe;
    g_service_probe_a = &server_monitor_probe;
    g_service_probe_b = &client_monitor_probe;

    void *server_monitor = zlink_gateway_monitor_open (
      server, ZLINK_GATEWAY_SERVICE_READY | ZLINK_GATEWAY_ROUTE_UP
                | ZLINK_GATEWAY_MONITOR_EVENT_ERROR,
      &service_monitor_handler_a);
    void *client_monitor = zlink_gateway_monitor_open (
      client, ZLINK_GATEWAY_SEND_READY_CHANGED | ZLINK_GATEWAY_ROUTE_UP
                | ZLINK_GATEWAY_ROUTE_DOWN | ZLINK_GATEWAY_MONITOR_EVENT_ERROR,
      &service_monitor_handler_b);
    TEST_ASSERT_NOT_NULL (server_monitor);
    TEST_ASSERT_NOT_NULL (client_monitor);

    gateway_delivery_probe_t delivery_probe;
    delivery_probe.server = server;
    g_gateway_probe = &delivery_probe;

    char endpoint[MAX_SOCKET_STRING];
    int bind_seed = 22920;
    bind_gateway_with_port_seed (server, &bind_seed, endpoint,
                                 sizeof (endpoint));

    size_t server_cursor = 0;
    size_t client_cursor = 0;
    zlink_service_event_t service_ready_event;
    zlink_service_event_t send_ready_event;
    TEST_ASSERT_TRUE (wait_for_service_event_match (
      &server_monitor_probe, &server_cursor, ZLINK_GATEWAY_SERVICE_READY, NULL,
      NULL, -1, &service_ready_event, 3000));
    TEST_ASSERT_EQUAL_UINT32 (ZLINK_GATEWAY_SERVICE_READY,
                              service_ready_event.event_type);

    zlink_monitor_snapshot_t snapshot;
    TEST_ASSERT_TRUE (wait_for_monitor_snapshot_state (
      client_monitor, ZLINK_MONITOR_SOURCE_GATEWAY,
      ZLINK_MONITOR_STATE_READY | ZLINK_MONITOR_STATE_SEND_READY, 1, &snapshot,
      3000));
    TEST_ASSERT_TRUE (wait_for_service_event_match (
      &client_monitor_probe, &client_cursor, ZLINK_GATEWAY_SEND_READY_CHANGED,
      NULL, NULL, 1, &send_ready_event, 1000));
    TEST_ASSERT_EQUAL_UINT32 (1u, send_ready_event.value);

    TEST_ASSERT_EQUAL_UINT32 (ZLINK_MONITOR_SOURCE_GATEWAY,
                              snapshot.source_kind);
    TEST_ASSERT_TRUE ((snapshot.state_flags & ZLINK_MONITOR_STATE_READY) != 0);
    TEST_ASSERT_TRUE (
      (snapshot.state_flags & ZLINK_MONITOR_STATE_SEND_READY) != 0);
    TEST_ASSERT_TRUE (snapshot.ready_peer_count >= 1);

    zlink_msg_t request;
    init_text_part (&request, "ping");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_send (client, &request, 1, 0));

    TEST_ASSERT_TRUE (wait_for_gateway_delivery (&delivery_probe, 3000));
    TEST_ASSERT_EQUAL_STRING ("ping", delivery_probe.request_payload);
    TEST_ASSERT_EQUAL_STRING ("pong", delivery_probe.reply_payload);
    TEST_ASSERT_EQUAL_INT (0, delivery_probe.close_failures);
    TEST_ASSERT_EQUAL_INT (0, delivery_probe.send_failures);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_service_monitor_close (&client_monitor));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_service_monitor_close (&server_monitor));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&client));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&server));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&client_discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&server_discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
}

void test_spot_delivery_ready_changed_implies_first_publish_delivery ()
{
    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *pub_node =
      zlink_spot_node_new (ctx, "spot-monitor-contract", &ignore_spot_handler);
    void *sub_node =
      zlink_spot_node_new (ctx, "spot-monitor-contract", &ignore_spot_handler);
    TEST_ASSERT_NOT_NULL (pub_node);
    TEST_ASSERT_NOT_NULL (sub_node);

    void *pub = zlink_spot_new (pub_node, &ignore_spot_handler);
    void *sub = zlink_spot_new (sub_node, &capture_spot_delivery);
    TEST_ASSERT_NOT_NULL (pub);
    TEST_ASSERT_NOT_NULL (sub);

    service_event_probe_t sub_monitor_probe;
    service_event_probe_t pub_monitor_probe;
    g_service_probe_a = &sub_monitor_probe;
    g_service_probe_b = &pub_monitor_probe;

    void *sub_monitor = zlink_spot_monitor_open (
      sub, ZLINK_SPOT_ROLE_SUB,
      ZLINK_SPOT_SUB_FILTER_APPLIED | ZLINK_SPOT_SUB_SUBSCRIPTION_READY
        | ZLINK_SPOT_SUB_DELIVERY_READY_CHANGED | ZLINK_MONITOR_EVENT_ERROR,
      &service_monitor_handler_a);
    void *pub_monitor = zlink_spot_monitor_open (
      pub, ZLINK_SPOT_ROLE_PUB,
      ZLINK_SPOT_PUB_DELIVERY_READY_CHANGED
        | ZLINK_SPOT_PUB_FIRST_DELIVERY_READY_CHANGED
        | ZLINK_MONITOR_EVENT_ERROR,
      &service_monitor_handler_b);
    TEST_ASSERT_NOT_NULL (sub_monitor);
    TEST_ASSERT_NOT_NULL (pub_monitor);

    char endpoint[MAX_SOCKET_STRING];
    int bind_seed = 22930;
    bind_spot_node_with_port_seed (pub_node, &bind_seed, endpoint,
                                   sizeof (endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_connect_peer_pub (sub_node, endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_subscribe (sub, "svc-contract"));

    size_t sub_cursor = 0;
    size_t pub_cursor = 0;
    zlink_service_event_t filter_event;
    zlink_service_event_t sub_ready_event;
    zlink_service_event_t pub_ready_event;
    TEST_ASSERT_TRUE (wait_for_service_event_match (
      &sub_monitor_probe, &sub_cursor, ZLINK_SPOT_SUB_FILTER_APPLIED, NULL,
      "svc-contract", -1, &filter_event, 3000));
    TEST_ASSERT_TRUE ((filter_event.detail_flags & ZLINK_EVENT_DETAIL_SUBJECT)
                      != 0);

    const bool sub_ready_matched = wait_for_service_event_match (
      &sub_monitor_probe, &sub_cursor, ZLINK_SPOT_SUB_DELIVERY_READY_CHANGED,
      endpoint, "svc-contract", 1, &sub_ready_event, 5000);
    TEST_ASSERT_TRUE (sub_ready_matched);
    TEST_ASSERT_EQUAL_UINT32 (1u, sub_ready_event.value);

    const bool pub_first_ready = wait_for_service_event_match (
      &pub_monitor_probe, &pub_cursor,
      ZLINK_SPOT_PUB_FIRST_DELIVERY_READY_CHANGED,
      NULL, "svc-contract", 1, &pub_ready_event, 5000);
    TEST_ASSERT_TRUE (pub_first_ready);
    TEST_ASSERT_EQUAL_UINT32 (1u, pub_ready_event.value);

    zlink_monitor_snapshot_t sub_snapshot;
    TEST_ASSERT_TRUE (wait_for_monitor_snapshot_state (
      sub_monitor, ZLINK_MONITOR_SOURCE_SPOT_SUB, ZLINK_MONITOR_STATE_READY, 1,
      &sub_snapshot, 3000));
    TEST_ASSERT_EQUAL_UINT32 (ZLINK_MONITOR_SOURCE_SPOT_SUB,
                              sub_snapshot.source_kind);
    TEST_ASSERT_TRUE ((sub_snapshot.state_flags & ZLINK_MONITOR_STATE_READY)
                      != 0);
    TEST_ASSERT_TRUE (sub_snapshot.ready_peer_count >= 1);

    zlink_monitor_snapshot_t pub_snapshot;
    TEST_ASSERT_TRUE (wait_for_monitor_snapshot_state (
      pub_monitor, ZLINK_MONITOR_SOURCE_SPOT_PUB,
      ZLINK_MONITOR_STATE_READY | ZLINK_MONITOR_STATE_SEND_READY, 1,
      &pub_snapshot, 3000));
    TEST_ASSERT_EQUAL_UINT32 (ZLINK_MONITOR_SOURCE_SPOT_PUB,
                              pub_snapshot.source_kind);
    TEST_ASSERT_TRUE ((pub_snapshot.state_flags & ZLINK_MONITOR_STATE_READY)
                      != 0);
    TEST_ASSERT_TRUE (
      (pub_snapshot.state_flags & ZLINK_MONITOR_STATE_SEND_READY) != 0);
    TEST_ASSERT_TRUE (pub_snapshot.ready_peer_count >= 1);

    spot_delivery_probe_t delivery_probe;
    g_spot_probe = &delivery_probe;
    zlink_msg_t payload;
    init_text_part (&payload, "payload");
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_publish (pub, "svc-contract", &payload, 1, 0));
    TEST_ASSERT_TRUE (wait_for_spot_delivery (&delivery_probe, "svc-contract",
                                              "payload", 3000));
    TEST_ASSERT_EQUAL_INT (0, delivery_probe.close_failures);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_disconnect_peer_pub (sub_node, endpoint));
    TEST_ASSERT_TRUE (wait_for_service_event_match (
      &pub_monitor_probe, &pub_cursor,
      ZLINK_SPOT_PUB_FIRST_DELIVERY_READY_CHANGED,
      NULL, "svc-contract", 0, NULL, 3000));
    TEST_ASSERT_TRUE (wait_for_service_event_match (
      &sub_monitor_probe, &sub_cursor, ZLINK_SPOT_SUB_DELIVERY_READY_CHANGED,
      NULL, "svc-contract", 0, NULL, 3000));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_service_monitor_close (&pub_monitor));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_service_monitor_close (&sub_monitor));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&sub));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&pub));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&sub_node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&pub_node));
}

void test_spot_multi_delivery_ready_changed_implies_first_publish_delivery ()
{
    static const size_t client_count = 16;
    void *server_ctx = zlink_ctx_new ();
    void *client_ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (server_ctx);
    TEST_ASSERT_NOT_NULL (client_ctx);

    void *pub_node =
      zlink_spot_node_new (server_ctx, "spot-monitor-contract-pub",
                           &ignore_spot_handler);
    TEST_ASSERT_NOT_NULL (pub_node);

    void *pub = zlink_spot_new (pub_node, &ignore_spot_handler);
    TEST_ASSERT_NOT_NULL (pub);

    service_event_probe_t pub_monitor_probe;
    void *pub_monitor = zlink_spot_monitor_open (
      pub, ZLINK_SPOT_ROLE_PUB,
      ZLINK_SPOT_PUB_DELIVERY_READY_CHANGED
        | ZLINK_SPOT_PUB_FIRST_DELIVERY_READY_CHANGED
        | ZLINK_MONITOR_EVENT_ERROR,
      &service_monitor_handler_registered);
    TEST_ASSERT_NOT_NULL (pub_monitor);
    register_service_monitor_probe (pub_monitor, &pub_monitor_probe);

    char endpoint[MAX_SOCKET_STRING];
    int bind_seed = 22940;
    bind_spot_node_with_port_seed (pub_node, &bind_seed, endpoint,
                                   sizeof (endpoint));

    std::vector<multi_spot_slot_t> slots (client_count);
    for (size_t i = 0; i < client_count; ++i) {
        char name[64];
        snprintf (name, sizeof (name), "spot-monitor-contract-sub-%zu", i);
        slots[i].node =
          zlink_spot_node_new (client_ctx, name, &ignore_spot_handler);
        TEST_ASSERT_NOT_NULL (slots[i].node);

        slots[i].sub =
          zlink_spot_new (slots[i].node, &capture_registered_spot_delivery);
        TEST_ASSERT_NOT_NULL (slots[i].sub);
        register_spot_delivery_probe (slots[i].sub, &slots[i].delivery_probe);

        slots[i].monitor = zlink_spot_monitor_open (
          slots[i].sub, ZLINK_SPOT_ROLE_SUB,
          ZLINK_SPOT_SUB_FILTER_APPLIED | ZLINK_SPOT_SUB_DELIVERY_READY_CHANGED
            | ZLINK_MONITOR_EVENT_ERROR,
          &service_monitor_handler_registered);
        TEST_ASSERT_NOT_NULL (slots[i].monitor);
        register_service_monitor_probe (slots[i].monitor,
                                        &slots[i].monitor_probe);

        TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_subscribe (slots[i].sub,
                                                         "svc-contract"));
        TEST_ASSERT_SUCCESS_ERRNO (
          zlink_spot_node_connect_peer_pub (slots[i].node, endpoint));
    }

    const std::chrono::steady_clock::time_point ready_deadline =
      std::chrono::steady_clock::now () + std::chrono::seconds (10);
    for (size_t i = 0; i < client_count; ++i) {
        size_t cursor = 0;
        const int filter_timeout = remaining_timeout_ms (ready_deadline);
        TEST_ASSERT_TRUE (filter_timeout > 0);
        TEST_ASSERT_TRUE (wait_for_service_event_match (
          &slots[i].monitor_probe, &cursor, ZLINK_SPOT_SUB_FILTER_APPLIED,
          NULL, "svc-contract", -1, NULL, filter_timeout));
        const int ready_timeout = remaining_timeout_ms (ready_deadline);
        TEST_ASSERT_TRUE (ready_timeout > 0);
        const bool sub_ready = wait_for_service_event_match (
          &slots[i].monitor_probe, &cursor,
          ZLINK_SPOT_SUB_DELIVERY_READY_CHANGED, endpoint, "svc-contract", 1,
          NULL, ready_timeout);
        if (!sub_ready) {
            fprintf (stderr,
                     "[spot-multi-ready-debug] slot=%llu missing "
                     "SUB_DELIVERY_READY_CHANGED endpoint=%s\n",
                     static_cast<unsigned long long> (i), endpoint);
            dump_service_events ("spot-multi-slot", &slots[i].monitor_probe);
            dump_service_events ("spot-multi-pub", &pub_monitor_probe);
        }
        TEST_ASSERT_TRUE (sub_ready);
    }
    size_t pub_cursor = 0;
    zlink_service_event_t pub_ready_event;
    const std::chrono::steady_clock::time_point pub_ready_deadline =
      std::chrono::steady_clock::now () + std::chrono::seconds (10);
    const int pub_ready_timeout = remaining_timeout_ms (pub_ready_deadline);
    TEST_ASSERT_TRUE (pub_ready_timeout > 0);
    TEST_ASSERT_TRUE (wait_for_service_event_min_value (
      &pub_monitor_probe, &pub_cursor,
      ZLINK_SPOT_PUB_FIRST_DELIVERY_READY_CHANGED,
      NULL, "svc-contract", static_cast<uint32_t> (client_count),
      &pub_ready_event, pub_ready_timeout));
    TEST_ASSERT_TRUE (
      pub_ready_event.value >= static_cast<uint32_t> (client_count));

    zlink_msg_t payload;
    init_text_part (&payload, "payload");
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_publish (pub, "svc-contract", &payload, 1, 0));

    const std::chrono::steady_clock::time_point delivery_deadline =
      std::chrono::steady_clock::now () + std::chrono::seconds (10);
    for (size_t i = 0; i < client_count; ++i) {
        const int delivery_timeout = remaining_timeout_ms (delivery_deadline);
        TEST_ASSERT_TRUE (delivery_timeout > 0);
        const bool delivered = wait_for_registered_spot_delivery (
          &slots[i].delivery_probe, "svc-contract", "payload",
          delivery_timeout);
        if (!delivered) {
            fprintf (stderr,
                     "[spot-multi-debug] slot=%llu calls=%d topic=%s "
                     "payload=%s close_failures=%d\n",
                     static_cast<unsigned long long> (i),
                     slots[i].delivery_probe.calls, slots[i].delivery_probe.topic,
                     slots[i].delivery_probe.payload,
                     slots[i].delivery_probe.close_failures);
        }
        TEST_ASSERT_TRUE (delivered);
        TEST_ASSERT_EQUAL_INT (0, slots[i].delivery_probe.close_failures);
    }

    unregister_service_monitor_probe (pub_monitor);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_service_monitor_close (&pub_monitor));

    for (size_t i = 0; i < client_count; ++i) {
        unregister_service_monitor_probe (slots[i].monitor);
        unregister_spot_delivery_probe (slots[i].sub);
        TEST_ASSERT_SUCCESS_ERRNO (
          zlink_service_monitor_close (&slots[i].monitor));
    }

    // This regression runs as an isolated process case and only needs to
    // verify the delivery contract. Closing the monitors is enough to stop
    // further probe recording; the remaining service/context teardown is left
    // to process exit so the regression stays bounded in runtime.
    (void) pub;
    (void) pub_node;
    (void) server_ctx;
    (void) client_ctx;
}

int main (int, char **)
{
    setup_test_environment ();
    const char *selected = getenv ("ZLINK_TEST_CASE");

    UNITY_BEGIN ();
#define RUN_MONITOR_SERVICE_CONTRACT_TEST(name)                              \
    do {                                                                     \
        if (!selected || strcmp (selected, #name) == 0)                      \
            RUN_TEST (name);                                                 \
    } while (0)
    RUN_MONITOR_SERVICE_CONTRACT_TEST (
      test_gateway_send_ready_changed_implies_first_request_reply);
    RUN_MONITOR_SERVICE_CONTRACT_TEST (
      test_spot_delivery_ready_changed_implies_first_publish_delivery);
    RUN_MONITOR_SERVICE_CONTRACT_TEST (
      test_spot_multi_delivery_ready_changed_implies_first_publish_delivery);
#undef RUN_MONITOR_SERVICE_CONTRACT_TEST
    return UNITY_END ();
}
