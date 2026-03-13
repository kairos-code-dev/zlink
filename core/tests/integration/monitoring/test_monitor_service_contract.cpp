/* SPDX-License-Identifier: MPL-2.0 */

#include "testutil.hpp"
#include "testutil_unity.hpp"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string.h>
#include <vector>

namespace
{
struct service_event_probe_t
{
    std::mutex mutex;
    std::condition_variable cv;
    std::vector<zlink_service_event_t> events;
};

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

service_event_probe_t *g_service_probe_a = NULL;
service_event_probe_t *g_service_probe_b = NULL;
gateway_delivery_probe_t *g_gateway_probe = NULL;
spot_delivery_probe_t *g_spot_probe = NULL;

void record_service_event (service_event_probe_t *probe_,
                           const zlink_service_event_t *event_)
{
    if (!probe_ || !event_)
        return;

    {
        std::lock_guard<std::mutex> lock (probe_->mutex);
        probe_->events.push_back (*event_);
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
        if ((event_.detail_flags & ZLINK_EVENT_DETAIL_ENDPOINT) == 0)
            return false;
        if (strcmp (event_.endpoint, endpoint_) != 0)
            return false;
    }

    if (subject_) {
        if ((event_.detail_flags & ZLINK_EVENT_DETAIL_SUBJECT) == 0)
            return false;
        if (strcmp (event_.subject, subject_) != 0)
            return false;
    }

    if (expected_value_ >= 0
        && event_.value != static_cast<uint32_t> (expected_value_)) {
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

        const std::chrono::steady_clock::time_point now =
          std::chrono::steady_clock::now ();
        if (now >= deadline)
            return false;

        probe_->cv.wait_for (lock, deadline - now);
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
}

void tearDown ()
{
    g_service_probe_a = NULL;
    g_service_probe_b = NULL;
    g_gateway_probe = NULL;
    g_spot_probe = NULL;
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
      ZLINK_SPOT_PUB_DELIVERY_READY_CHANGED | ZLINK_MONITOR_EVENT_ERROR,
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

    TEST_ASSERT_TRUE (wait_for_service_event_match (
      &sub_monitor_probe, &sub_cursor, ZLINK_SPOT_SUB_DELIVERY_READY_CHANGED,
      endpoint, "svc-contract", 1, &sub_ready_event, 3000));
    TEST_ASSERT_EQUAL_UINT32 (1u, sub_ready_event.value);

    TEST_ASSERT_TRUE (wait_for_service_event_match (
      &pub_monitor_probe, &pub_cursor, ZLINK_SPOT_PUB_DELIVERY_READY_CHANGED,
      NULL, "svc-contract", 1, &pub_ready_event, 3000));
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
      &pub_monitor_probe, &pub_cursor, ZLINK_SPOT_PUB_DELIVERY_READY_CHANGED,
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

int main (int, char **)
{
    setup_test_environment ();

    UNITY_BEGIN ();
    RUN_TEST (test_gateway_send_ready_changed_implies_first_request_reply);
    RUN_TEST (test_spot_delivery_ready_changed_implies_first_publish_delivery);
    return UNITY_END ();
}
