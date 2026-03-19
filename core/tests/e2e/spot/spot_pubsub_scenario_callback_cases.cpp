/* SPDX-License-Identifier: MPL-2.0 */

#include "spot_pubsub_scenario_shared.hpp"

#include <atomic>
#include <chrono>
#include <string.h>

struct callback_probe_t
{
    callback_probe_t () :
        calls (0),
        payload_ok (0),
        topic_ok (0),
        saw_source_rid (0)
    {
    }

    std::mutex mutex;
    std::condition_variable cv;
    std::atomic<int> calls;
    std::atomic<int> payload_ok;
    std::atomic<int> topic_ok;
    std::atomic<int> saw_source_rid;
};

static void spot_sub_probe_handler (const zlink_routing_id_t *source_rid_,
                                    const char *topic_,
                                    size_t topic_len_,
                                    zlink_msg_t *parts_,
                                    size_t part_count_,
                                    void *userdata_)
{
    callback_probe_t *probe = static_cast<callback_probe_t *> (userdata_);
    if (!probe) {
        close_spot_parts (parts_, part_count_);
        return;
    }

    probe->calls.fetch_add (1);
    if (source_rid_ && source_rid_->size > 0)
        probe->saw_source_rid.store (1);
    if (topic_ && topic_len_ == 14 && memcmp (topic_, "zone:auto:test", 14) == 0)
        probe->topic_ok.store (1);
    if (parts_ && part_count_ == 1 && zlink_msg_size (&parts_[0]) == 4
        && memcmp (zlink_msg_data (&parts_[0]), "ping", 4) == 0) {
        probe->payload_ok.store (1);
    }

    close_spot_parts (parts_, part_count_);
    probe->cv.notify_all ();
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

    void *pub_node = create_spot_node (ctx, "spot-callback-basic");
    void *sub_node = zlink_spot_node_new (ctx, "spot-callback-basic");
    TEST_ASSERT_NOT_NULL (pub_node);
    TEST_ASSERT_NOT_NULL (sub_node);

    const int linger = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_option (sub_node, ZLINK_OPT_LINGER, &linger, sizeof (linger)));

    callback_probe_t probe;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_subscribe_handler (sub_node, &spot_sub_probe_handler, &probe));

    char endpoint[MAX_SOCKET_STRING];
    int port_seed = 33140;
    TEST_ASSERT_SUCCESS_ERRNO (bind_spot_node_with_port_seed (
      pub_node, "tcp://127.0.0.1:", &port_seed, endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_connect_peer (sub_node, endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_subscription (sub_node, "zone:auto:test"));
    msleep (100);

    zlink_msg_t part;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&part, 4));
    memcpy (zlink_msg_data (&part), "ping", 4);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_publish (pub_node, "zone:auto:test", &part, 1, 0));

    TEST_ASSERT_TRUE (wait_until_counter_at_least (&probe.calls, 1, 3000));
    TEST_ASSERT_EQUAL_INT (1, probe.payload_ok.load ());
    TEST_ASSERT_EQUAL_INT (1, probe.topic_ok.load ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&sub_node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&pub_node));
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

    void *pub_node = create_spot_node (ctx, "spot-callback-isolation");
    void *sub_a = zlink_spot_node_new (ctx, "spot-callback-isolation-a");
    void *sub_b = zlink_spot_node_new (ctx, "spot-callback-isolation-b");
    TEST_ASSERT_NOT_NULL (pub_node);
    TEST_ASSERT_NOT_NULL (sub_a);
    TEST_ASSERT_NOT_NULL (sub_b);

    const int linger = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_option (sub_a, ZLINK_OPT_LINGER, &linger, sizeof (linger)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_option (sub_b, ZLINK_OPT_LINGER, &linger, sizeof (linger)));

    callback_probe_t probe_a;
    callback_probe_t probe_b;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_subscribe_handler (sub_a, &spot_sub_probe_handler, &probe_a));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_subscribe_handler (sub_b, &spot_sub_probe_handler, &probe_b));

    char endpoint[MAX_SOCKET_STRING];
    int port_seed = 33180;
    TEST_ASSERT_SUCCESS_ERRNO (bind_spot_node_with_port_seed (
      pub_node, "tcp://127.0.0.1:", &port_seed, endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_connect_peer (sub_a, endpoint));

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_subscription (sub_a, "zone:auto:test"));
    msleep (100);

    TEST_ASSERT_SUCCESS_ERRNO (
      publish_text (&zlink_publish, pub_node, "zone:auto:test", "ping", 0));
    TEST_ASSERT_TRUE (wait_until_counter_at_least (&probe_a.calls, 1, 3000));
    TEST_ASSERT_EQUAL_INT (0, probe_b.calls.load ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&sub_b));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&sub_a));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&pub_node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_spot_facade_handler_receives_source_rid ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *pub_node = create_spot_node (ctx, "spot-callback-rid");
    void *sub_node = zlink_spot_node_new (ctx, "spot-callback-rid");
    TEST_ASSERT_NOT_NULL (pub_node);
    TEST_ASSERT_NOT_NULL (sub_node);

    const int linger = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_option (sub_node, ZLINK_OPT_LINGER, &linger, sizeof (linger)));

    const char rid[] = "pub-rid";
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_routing_id (pub_node, rid, sizeof (rid) - 1));

    callback_probe_t probe;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_subscribe_handler (sub_node, &spot_sub_probe_handler, &probe));

    char endpoint[MAX_SOCKET_STRING];
    int port_seed = 33220;
    TEST_ASSERT_SUCCESS_ERRNO (bind_spot_node_with_port_seed (
      pub_node, "tcp://127.0.0.1:", &port_seed, endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_connect_peer (sub_node, endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_subscription (sub_node, "zone:auto:test"));
    msleep (100);

    TEST_ASSERT_SUCCESS_ERRNO (
      publish_text (&zlink_publish, pub_node, "zone:auto:test", "ping", 0));
    TEST_ASSERT_TRUE (wait_until_counter_at_least (&probe.calls, 1, 3000));
    TEST_ASSERT_EQUAL_INT (1, probe.saw_source_rid.load ());

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

    int registry_seed = 33260;
    char registry_pub[MAX_SOCKET_STRING];
    char registry_router[MAX_SOCKET_STRING];
    void *registry = create_started_registry_with_port_seed (
      ctx, &registry_seed, registry_pub, sizeof (registry_pub), registry_router,
      sizeof (registry_router));
    TEST_ASSERT_NOT_NULL (registry);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_registry_set_broadcast_interval (registry, 50));

    void *discovery = zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_SPOT);
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_SUCCESS_ERRNO (
      connect_discovery_registry_with_retry (discovery, registry_router, 2000));

    void *pub_a = create_spot_node (ctx, "spot-svc-a");
    void *sub_a = zlink_spot_node_new (ctx, "spot-svc-a");
    void *pub_b = create_spot_node (ctx, "spot-svc-b");
    void *sub_b = zlink_spot_node_new (ctx, "spot-svc-b");
    TEST_ASSERT_NOT_NULL (pub_a);
    TEST_ASSERT_NOT_NULL (sub_a);
    TEST_ASSERT_NOT_NULL (pub_b);
    TEST_ASSERT_NOT_NULL (sub_b);

    const int linger = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_option (sub_a, ZLINK_OPT_LINGER, &linger, sizeof (linger)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_option (sub_b, ZLINK_OPT_LINGER, &linger, sizeof (linger)));

    callback_probe_t probe_a;
    callback_probe_t probe_b;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_subscribe_handler (sub_a, &spot_sub_probe_handler, &probe_a));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_subscribe_handler (sub_b, &spot_sub_probe_handler, &probe_b));

    char pub_endpoint_a[MAX_SOCKET_STRING];
    char pub_endpoint_b[MAX_SOCKET_STRING];
    int port_seed = 33300;
    TEST_ASSERT_SUCCESS_ERRNO (bind_spot_node_with_port_seed (
      pub_a, "tcp://127.0.0.1:", &port_seed, pub_endpoint_a));
    TEST_ASSERT_SUCCESS_ERRNO (bind_spot_node_with_port_seed (
      pub_b, "tcp://127.0.0.1:", &port_seed, pub_endpoint_b));

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_attach_discovery (pub_a, discovery));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_attach_discovery (sub_a, discovery));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_attach_discovery (pub_b, discovery));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_attach_discovery (sub_b, discovery));

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_subscription (sub_a, "zone:auto:test"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_subscription (sub_b, "zone:auto:test"));
    msleep (300);

    TEST_ASSERT_SUCCESS_ERRNO (
      publish_text (&zlink_publish, pub_a, "zone:auto:test", "ping", 0));
    TEST_ASSERT_TRUE (wait_until_counter_at_least (&probe_a.calls, 1, 3000));
    TEST_ASSERT_EQUAL_INT (0, probe_b.calls.load ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&sub_b));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&pub_b));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&sub_a));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&pub_a));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}
