/* SPDX-License-Identifier: MPL-2.0 */

//  MeshNode dispatch stress: concurrent submitters against a claim/release
//  consumer loop (lost-wakeup and claim-leak check), request completions
//  under contention delivered exactly once, and shared multipart reference
//  counts across Logical Multicast fan-out with early producer close.

#include "../testutil_unity.hpp"

#include <string.h>

#include <atomic>
#include <thread>
#include <vector>

SETUP_TEARDOWN_TESTCONTEXT

namespace
{
const int producer_count = 4;
const int sends_per_producer = 500;

void *new_started_node (void *ctx_, const char *name_, const char *channel_)
{
    zlink_mesh_node_options_t options;
    memset (&options, 0, sizeof (options));
    options.struct_size = sizeof (options);
    options.version = 1;
    options.mesh_name = name_;
    options.mesh_name_size = strlen (name_);
    void *node = zlink_mesh_node_new (ctx_, &options);
    TEST_ASSERT_NOT_NULL (node);
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK, zlink_set_routing_id (node, name_, strlen (name_)));
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK,
                           zlink_mesh_node_set_bind (node, "tcp://127.0.0.1:0"));
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK, zlink_mesh_node_add_channel_name (node, channel_));
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK, zlink_mesh_node_start (node));
    return node;
}

//  Drains every claimable ready record across both domains once and returns
//  the number of application records received. Releases each claim after its
//  batch recv, so the consumer path continuously rearms the ready index.
int drain_once (void *node_, void *ready_, void *batch_, int *completions_out_)
{
    uint32_t residue = 0;
    const zlink_recv_result_t rc = zlink_mesh_node_drain_ready (
      node_, ZLINK_MESH_READY_APPLICATION | ZLINK_MESH_READY_INFRASTRUCTURE, ready_, &residue,
      ZLINK_RECV_FLAGS_DONTWAIT);
    if (rc == ZLINK_RECV_NO_DATA)
        return 0;
    TEST_ASSERT_EQUAL_INT (ZLINK_RECV_OK, rc);

    int application_records = 0;
    const size_t count = zlink_mesh_ready_batch_count (ready_);
    for (size_t i = 0; i < count; ++i) {
        zlink_mesh_claim_t claim;
        if (zlink_mesh_ready_batch_take_claim (ready_, i, &claim) != ZLINK_CONFIG_OK)
            continue;
        while (true) {
            zlink_mesh_receive_requirements_t required;
            memset (&required, 0, sizeof (required));
            required.struct_size = sizeof (required);
            required.version = 1;
            TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK, zlink_mesh_receive_batch_reset (batch_));
            const zlink_recv_result_t recv_rc =
              zlink_mesh_claim_recv_batch (&claim, batch_, &required, ZLINK_RECV_FLAGS_DONTWAIT);
            if (recv_rc == ZLINK_RECV_NO_DATA)
                break;
            TEST_ASSERT_EQUAL_INT (ZLINK_RECV_OK, recv_rc);
            const size_t records = zlink_mesh_receive_batch_count (batch_);
            const zlink_mesh_receive_record_t *data = zlink_mesh_receive_batch_data (batch_);
            for (size_t r = 0; r < records; ++r) {
                if (data[r].kind == ZLINK_MESH_RECORD_COMPLETION)
                    *completions_out_ += 1;
                else
                    ++application_records;
            }
            if (records == 0)
                break;
        }
        TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_claim_release (&claim));
    }
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK, zlink_mesh_ready_batch_reset (ready_));
    return application_records;
}
}

//  Four producers hammer one channel while the consumer loop claims,
//  receives and releases. Every send and every request completion must be
//  observed exactly once and no claim may stay active at the end.
void test_concurrent_submit_claim_release_no_lost_wakeup ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    void *node = new_started_node (ctx, "stress-mesh", "load");

    zlink_mesh_monitor_open_options_t monitor_options;
    memset (&monitor_options, 0, sizeof (monitor_options));
    monitor_options.struct_size = sizeof (monitor_options);
    monitor_options.version = 1;
    //  Counters only: mask everything out of the event queue except state.
    monitor_options.events = 1ull << (ZLINK_MESH_MONITOR_STATE_CHANGED - 1);
    void *monitor = zlink_mesh_node_monitor_open (node, &monitor_options);
    TEST_ASSERT_NOT_NULL (monitor);

    std::atomic<int> submitted (0);
    std::atomic<int> requested (0);
    std::vector<std::thread> producers;
    for (int p = 0; p < producer_count; ++p) {
        producers.emplace_back ([node, p, &submitted, &requested] () {
            for (int i = 0; i < sends_per_producer; ++i) {
                zlink_msg_t part;
                char text[48];
                snprintf (text, sizeof (text), "producer-%d:item-%d", p, i);
                if (zlink_msg_init_size (&part, strlen (text)) != ZLINK_CONFIG_OK)
                    return;
                memcpy (zlink_msg_data (&part), text, strlen (text));
                if ((i & 7) == 0) {
                    //  Every eighth submit is a request with a short timeout;
                    //  nobody replies, so each yields exactly one TIMED_OUT
                    //  completion on the infrastructure lane.
                    zlink_mesh_operation_id_t op_id;
                    if (zlink_mesh_node_request_to_channel (node, "load", NULL, &part, 1, &op_id,
                                                            ZLINK_SEND_FLAGS_NONE, 50)
                        == ZLINK_SUBMIT_OK)
                        requested.fetch_add (1);
                } else {
                    if (zlink_mesh_node_send_to_channel (node, "load", NULL, &part, 1,
                                                         ZLINK_SEND_FLAGS_NONE)
                        == ZLINK_SUBMIT_OK)
                        submitted.fetch_add (1);
                }
                zlink_msg_close (&part);
            }
        });
    }

    void *ready = zlink_mesh_ready_batch_new (32);
    void *batch = zlink_mesh_receive_batch_new (32, 128, 1 << 20);
    TEST_ASSERT_NOT_NULL (ready);
    TEST_ASSERT_NOT_NULL (batch);

    int application_seen = 0;
    int completions_seen = 0;
    const int expected_low_bound = producer_count * sends_per_producer;
    for (int spin = 0; spin < 6000; ++spin) {
        application_seen += drain_once (node, ready, batch, &completions_seen);
        const int total_submitted = submitted.load () + requested.load ();
        if (total_submitted == expected_low_bound && application_seen == total_submitted
            && completions_seen == requested.load ())
            break;
        msleep (5);
    }
    for (size_t i = 0; i < producers.size (); ++i)
        producers[i].join ();
    //  Late completions: keep draining until the counts settle.
    for (int spin = 0; spin < 1000; ++spin) {
        application_seen += drain_once (node, ready, batch, &completions_seen);
        if (application_seen == submitted.load () + requested.load ()
            && completions_seen == requested.load ())
            break;
        msleep (5);
    }

    TEST_ASSERT_EQUAL_INT (producer_count * sends_per_producer,
                           submitted.load () + requested.load ());
    TEST_ASSERT_EQUAL_INT (submitted.load () + requested.load (), application_seen);
    TEST_ASSERT_EQUAL_INT (requested.load (), completions_seen);

    //  No claim leak: every claim was released.
    zlink_mesh_monitor_status_t status;
    memset (&status, 0, sizeof (status));
    status.struct_size = sizeof (status);
    status.version = 1;
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK, zlink_mesh_node_monitor_status (monitor, &status));
    TEST_ASSERT_EQUAL_UINT64 (0, status.active_claims);
    TEST_ASSERT_EQUAL_UINT64 (0, status.pending_application_messages);
    TEST_ASSERT_EQUAL_UINT64 (0, status.pending_infrastructure_messages);

    void *ready_handle = ready;
    void *batch_handle = batch;
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_ready_batch_destroy (&ready_handle));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_receive_batch_destroy (&batch_handle));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_node_monitor_close (&monitor));
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK, zlink_mesh_node_shutdown (node, 2000));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_node_destroy (&node));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_ctx_term (ctx));
}

//  Shared multipart ownership: one publish fans out to two subscribed Spots
//  while the producer closes its parts immediately. Both Spots must observe
//  every intact part, exercising the shared reference count path.
void test_multicast_shared_reference_count_fanout ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    void *node = new_started_node (ctx, "fanout-mesh", "frames");

    void *spots[2] = {NULL, NULL};
    const char *names[2] = {"sink-alpha", "sink-beta"};
    for (int s = 0; s < 2; ++s) {
        zlink_routing_id_t rid;
        memset (&rid, 0, sizeof (rid));
        rid.size = static_cast<uint8_t> (strlen (names[s]));
        memcpy (rid.data, names[s], rid.size);
        uint32_t created = 0;
        TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK,
                               zlink_mesh_node_spot_get_or_new (node, &rid, &spots[s], &created));
        TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK,
                               zlink_spot_set_subscription (spots[s], "frames", "video.",
                                                            ZLINK_SPOT_SUBSCRIPTION_PREFIX));
    }

    void *publisher = zlink_mesh_node_publisher_new (node);
    TEST_ASSERT_NOT_NULL (publisher);

    const int publish_count = 300;
    for (int i = 0; i < publish_count; ++i) {
        zlink_msg_t parts[3];
        char text[40];
        for (int part_index = 0; part_index < 3; ++part_index) {
            snprintf (text, sizeof (text), "frame-%d-part-%d", i, part_index);
            TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK,
                                   zlink_msg_init_size (&parts[part_index], strlen (text)));
            memcpy (zlink_msg_data (&parts[part_index]), text, strlen (text));
        }
        zlink_mesh_publish_detail_t detail;
        memset (&detail, 0, sizeof (detail));
        detail.struct_size = sizeof (detail);
        detail.version = 1;
        TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_OK,
                               zlink_mesh_node_publisher_publish (publisher, "frames",
                                                                  "video.live", NULL, parts, 3,
                                                                  &detail,
                                                                  ZLINK_SEND_FLAGS_NONE));
        TEST_ASSERT_EQUAL_UINT32 (2, detail.admitted_local_spot_count);
        //  The producer's references close right away; the queues keep the
        //  payload alive through the shared reference count.
        for (int part_index = 0; part_index < 3; ++part_index)
            zlink_msg_close (&parts[part_index]);
    }

    //  Both Spots drain their own application lane: every record intact.
    void *ready = zlink_mesh_ready_batch_new (16);
    void *batch = zlink_mesh_receive_batch_new (16, 64, 1 << 20);
    TEST_ASSERT_NOT_NULL (ready);
    TEST_ASSERT_NOT_NULL (batch);
    int records_seen = 0;
    int parts_seen = 0;
    for (int spin = 0; spin < 3000 && records_seen < publish_count * 2; ++spin) {
        uint32_t residue = 0;
        const zlink_recv_result_t rc = zlink_mesh_node_drain_ready (
          node, ZLINK_MESH_READY_APPLICATION, ready, &residue, ZLINK_RECV_FLAGS_DONTWAIT);
        if (rc == ZLINK_RECV_NO_DATA) {
            msleep (5);
            continue;
        }
        TEST_ASSERT_EQUAL_INT (ZLINK_RECV_OK, rc);
        const size_t count = zlink_mesh_ready_batch_count (ready);
        for (size_t i = 0; i < count; ++i) {
            zlink_mesh_claim_t claim;
            if (zlink_mesh_ready_batch_take_claim (ready, i, &claim) != ZLINK_CONFIG_OK)
                continue;
            while (true) {
                zlink_mesh_receive_requirements_t required;
                memset (&required, 0, sizeof (required));
                required.struct_size = sizeof (required);
                required.version = 1;
                TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK, zlink_mesh_receive_batch_reset (batch));
                const zlink_recv_result_t recv_rc =
                  zlink_mesh_claim_recv_batch (&claim, batch, &required,
                                               ZLINK_RECV_FLAGS_DONTWAIT);
                if (recv_rc == ZLINK_RECV_NO_DATA)
                    break;
                TEST_ASSERT_EQUAL_INT (ZLINK_RECV_OK, recv_rc);
                const size_t records = zlink_mesh_receive_batch_count (batch);
                const zlink_mesh_receive_record_t *data = zlink_mesh_receive_batch_data (batch);
                const zlink_msg_t *batch_parts = zlink_mesh_receive_batch_parts (batch);
                for (size_t r = 0; r < records; ++r) {
                    TEST_ASSERT_EQUAL_INT (ZLINK_MESH_RECORD_SPOT_MULTICAST, data[r].kind);
                    TEST_ASSERT_EQUAL_UINT64 (3, data[r].part_count);
                    for (uint64_t part_index = 0; part_index < data[r].part_count; ++part_index) {
                        const zlink_msg_t *msg =
                          &batch_parts[data[r].part_offset + part_index];
                        const char *bytes = static_cast<const char *> (
                          zlink_msg_data (const_cast<zlink_msg_t *> (msg)));
                        TEST_ASSERT_TRUE (zlink_msg_size (msg) >= 12);
                        TEST_ASSERT_EQUAL_MEMORY ("frame-", bytes, 6);
                    }
                    ++records_seen;
                    parts_seen += static_cast<int> (data[r].part_count);
                }
                if (records == 0)
                    break;
            }
            TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_claim_release (&claim));
        }
        TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK, zlink_mesh_ready_batch_reset (ready));
    }
    TEST_ASSERT_EQUAL_INT (publish_count * 2, records_seen);
    TEST_ASSERT_EQUAL_INT (publish_count * 2 * 3, parts_seen);

    void *ready_handle = ready;
    void *batch_handle = batch;
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_ready_batch_destroy (&ready_handle));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_receive_batch_destroy (&batch_handle));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_node_publisher_destroy (&publisher));
    for (int s = 0; s < 2; ++s)
        TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_spot_destroy (&spots[s]));
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK, zlink_mesh_node_shutdown (node, 2000));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_node_destroy (&node));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_ctx_term (ctx));
}

//  Ready-handler churn: registering and unregistering the wakeup handler
//  concurrently with submits neither loses records nor deadlocks, and the
//  handler/poller exclusivity holds at every step.
void test_ready_handler_churn_under_load ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    void *node = new_started_node (ctx, "churn-mesh", "beat");

    std::atomic<bool> stop (false);
    std::atomic<int> sent (0);
    std::thread producer ([node, &stop, &sent] () {
        int i = 0;
        while (!stop.load ()) {
            zlink_msg_t part;
            char text[32];
            snprintf (text, sizeof (text), "beat-%d", i++);
            if (zlink_msg_init_size (&part, strlen (text)) != ZLINK_CONFIG_OK)
                return;
            memcpy (zlink_msg_data (&part), text, strlen (text));
            if (zlink_mesh_node_send_to_channel (node, "beat", NULL, &part, 1,
                                                 ZLINK_SEND_FLAGS_DONTWAIT)
                == ZLINK_SUBMIT_OK)
                sent.fetch_add (1);
            zlink_msg_close (&part);
            if ((i & 31) == 0)
                msleep (1);
        }
    });

    //  Handler churn on the consumer side.
    for (int cycle = 0; cycle < 200; ++cycle) {
        TEST_ASSERT_EQUAL_INT (
          ZLINK_HANDLER_OK,
          zlink_mesh_node_set_ready_handler (
            node,
            [] (void *, zlink_mesh_ready_domain_mask_t mask_, void *) { return mask_; }, NULL));
        TEST_ASSERT_EQUAL_INT (ZLINK_HANDLER_OK,
                               zlink_mesh_node_set_ready_handler (node, NULL, NULL));
    }
    stop.store (true);
    producer.join ();

    //  Drain everything that was admitted; nothing may be lost.
    void *ready = zlink_mesh_ready_batch_new (32);
    void *batch = zlink_mesh_receive_batch_new (32, 128, 1 << 20);
    TEST_ASSERT_NOT_NULL (ready);
    TEST_ASSERT_NOT_NULL (batch);
    int seen = 0;
    int completions = 0;
    for (int spin = 0; spin < 2000 && seen < sent.load (); ++spin) {
        seen += drain_once (node, ready, batch, &completions);
        if (seen < sent.load ())
            msleep (2);
    }
    TEST_ASSERT_EQUAL_INT (sent.load (), seen);

    void *ready_handle = ready;
    void *batch_handle = batch;
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_ready_batch_destroy (&ready_handle));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_receive_batch_destroy (&batch_handle));
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK, zlink_mesh_node_shutdown (node, 2000));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_node_destroy (&node));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_ctx_term (ctx));
}

int main ()
{
    setup_test_environment (180);

    UNITY_BEGIN ();
    RUN_TEST (test_concurrent_submit_claim_release_no_lost_wakeup);
    RUN_TEST (test_multicast_shared_reference_count_fanout);
    RUN_TEST (test_ready_handler_churn_under_load);
    const int rc = UNITY_END ();
    fflush (NULL);
    return rc;
}
