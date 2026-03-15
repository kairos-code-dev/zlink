/* SPDX-License-Identifier: MPL-2.0 */

#include "spot_pubsub_scenario_shared.hpp"

#include <chrono>
#include <string.h>

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
    if (parts_ && part_count_ == 1 && zlink_msg_size (&parts_[0]) == 4
        && memcmp (zlink_msg_data (&parts_[0]), "ping", 4) == 0) {
        probe->payload_ok.store (1);
    }
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

void test_spot_sub_handler_basic ()
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
    register_callback_probe (sub, &probe);

    zlink_msg_t part;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&part, 4));
    memcpy (zlink_msg_data (&part), "ping", 4);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_publish (pub, "zone:auto:test", &part, 1, 0));

    TEST_ASSERT_TRUE (wait_until_counter_at_least (&probe.calls, 1, 1000));
    TEST_ASSERT_EQUAL_INT (1, probe.payload_ok.load ());
    TEST_ASSERT_EQUAL_INT (1, probe.topic_ok.load ());

    unregister_callback_probe (sub);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&sub));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&pub));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_spot_recv_callback_isolated_by_handle ()
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
    void *sub_a_monitor = open_spot_monitor_with_probe (
      sub_a, ZLINK_SPOT_ROLE_SUB,
      ZLINK_SPOT_SUB_FILTER_APPLIED | ZLINK_SPOT_SUB_SUBSCRIPTION_READY
        | ZLINK_MONITOR_EVENT_ERROR,
      &sub_a_monitor_probe);
    TEST_ASSERT_NOT_NULL (sub_a_monitor);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_subscribe (sub_a, "iso:handle"));
    TEST_ASSERT_TRUE (wait_for_service_event (
      &sub_a_monitor_probe, ZLINK_SPOT_SUB_FILTER_APPLIED, NULL, 3000));
    TEST_ASSERT_TRUE (wait_for_service_event (
      &sub_a_monitor_probe, ZLINK_SPOT_SUB_SUBSCRIPTION_READY, pub_endpoint,
      3000));
    TEST_ASSERT_SUCCESS_ERRNO (
      close_service_monitor_with_probe (&sub_a_monitor));

    service_monitor_probe_t sub_b_monitor_probe;
    void *sub_b_monitor = open_spot_monitor_with_probe (
      sub_b, ZLINK_SPOT_ROLE_SUB,
      ZLINK_SPOT_SUB_FILTER_APPLIED | ZLINK_SPOT_SUB_SUBSCRIPTION_READY
        | ZLINK_MONITOR_EVENT_ERROR,
      &sub_b_monitor_probe);
    TEST_ASSERT_NOT_NULL (sub_b_monitor);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_subscribe (sub_b, "iso:handle"));
    TEST_ASSERT_TRUE (wait_for_service_event (
      &sub_b_monitor_probe, ZLINK_SPOT_SUB_FILTER_APPLIED, NULL, 3000));
    TEST_ASSERT_SUCCESS_ERRNO (
      close_service_monitor_with_probe (&sub_b_monitor));

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

void test_spot_facade_handler_receives_source_rid ()
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

void test_spot_recv_callback_isolated_by_service_with_discovery ()
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
    void *sub_a_monitor = open_spot_monitor_with_probe (
      sub_a, ZLINK_SPOT_ROLE_SUB,
      ZLINK_SPOT_SUB_FILTER_APPLIED | ZLINK_SPOT_SUB_SUBSCRIPTION_READY
        | ZLINK_MONITOR_EVENT_ERROR,
      &sub_a_monitor_probe);
    TEST_ASSERT_NOT_NULL (sub_a_monitor);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_subscribe (sub_a, "iso:service"));
    TEST_ASSERT_TRUE (wait_for_service_event (
      &sub_a_monitor_probe, ZLINK_SPOT_SUB_FILTER_APPLIED, NULL, 3000));
    TEST_ASSERT_TRUE (wait_for_service_event (
      &sub_a_monitor_probe, ZLINK_SPOT_SUB_SUBSCRIPTION_READY, pub_endpoint_a,
      3000));
    TEST_ASSERT_SUCCESS_ERRNO (
      close_service_monitor_with_probe (&sub_a_monitor));

    service_monitor_probe_t sub_b_monitor_probe;
    void *sub_b_monitor = open_spot_monitor_with_probe (
      sub_b, ZLINK_SPOT_ROLE_SUB,
      ZLINK_SPOT_SUB_FILTER_APPLIED | ZLINK_SPOT_SUB_SUBSCRIPTION_READY
        | ZLINK_MONITOR_EVENT_ERROR,
      &sub_b_monitor_probe);
    TEST_ASSERT_NOT_NULL (sub_b_monitor);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_subscribe (sub_b, "iso:service"));
    TEST_ASSERT_TRUE (wait_for_service_event (
      &sub_b_monitor_probe, ZLINK_SPOT_SUB_FILTER_APPLIED, NULL, 3000));
    TEST_ASSERT_TRUE (wait_for_service_event (
      &sub_b_monitor_probe, ZLINK_SPOT_SUB_SUBSCRIPTION_READY, pub_endpoint_b,
      3000));
    TEST_ASSERT_SUCCESS_ERRNO (
      close_service_monitor_with_probe (&sub_b_monitor));

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

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_unsubscribe (sub_a, "iso:service"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_unsubscribe (sub_b, "iso:service"));
    TEST_ASSERT_SUCCESS_ERRNO (
      publish_text (&zlink_spot_publish, pub_a, "iso:service", "after-unsub-a", 0));
    TEST_ASSERT_FALSE (
      wait_for_spot_message (sub_a, "iso:service", "after-unsub-a", 13, 200));
    TEST_ASSERT_FALSE (
      wait_for_spot_message (sub_b, "iso:service", "after-unsub-a", 13, 200));
    TEST_ASSERT_SUCCESS_ERRNO (
      publish_text (&zlink_spot_publish, pub_b, "iso:service", "after-unsub-b", 0));
    TEST_ASSERT_FALSE (
      wait_for_spot_message (sub_a, "iso:service", "after-unsub-b", 13, 200));
    TEST_ASSERT_FALSE (
      wait_for_spot_message (sub_b, "iso:service", "after-unsub-b", 13, 200));

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
