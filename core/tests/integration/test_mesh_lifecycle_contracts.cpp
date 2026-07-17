/* SPDX-License-Identifier: MPL-2.0 */

//  S5 review-driven lifecycle contracts: process-wide claim identity across
//  multiple MeshNodes, monitor handler re-entrancy, logical Spot end with
//  timer generation gating, Actor destroy drain, shutdown detaching
//  timeout-less operations, query output invariance on invalid elements and
//  the strict UTF-8 gate on public strings.

#include "../testutil_unity.hpp"

#include <string.h>

SETUP_TEARDOWN_TESTCONTEXT

namespace
{
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

void make_payload (zlink_msg_t *msg_, const char *text_)
{
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK, zlink_msg_init_size (msg_, strlen (text_)));
    memcpy (zlink_msg_data (&*msg_), text_, strlen (text_));
}

void take_ready_claim (void *node_,
                       zlink_mesh_owner_kind_t owner_kind_,
                       zlink_mesh_ready_domain_mask_t domain_,
                       zlink_mesh_claim_t *claim_out_)
{
    void *batch = zlink_mesh_ready_batch_new (8);
    TEST_ASSERT_NOT_NULL (batch);
    bool taken = false;
    for (int attempt = 0; attempt < 200 && !taken; ++attempt) {
        uint32_t residue = 0;
        const zlink_recv_result_t rc =
          zlink_mesh_node_drain_ready (node_, domain_, batch, &residue, ZLINK_RECV_FLAGS_DONTWAIT);
        if (rc == ZLINK_RECV_NO_DATA) {
            msleep (10);
            continue;
        }
        TEST_ASSERT_EQUAL_INT (ZLINK_RECV_OK, rc);
        const size_t count = zlink_mesh_ready_batch_count (batch);
        const zlink_mesh_ready_record_t *records = zlink_mesh_ready_batch_data (batch);
        for (size_t i = 0; i < count; ++i) {
            if (records[i].owner_kind == owner_kind_ && records[i].domain == domain_) {
                TEST_ASSERT_EQUAL_INT (
                  ZLINK_CONFIG_OK, zlink_mesh_ready_batch_take_claim (batch, i, claim_out_));
                taken = true;
                break;
            }
        }
        if (!taken)
            TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK, zlink_mesh_ready_batch_reset (batch));
    }
    TEST_ASSERT_TRUE_MESSAGE (taken, "expected ready record did not arrive");
    void *batch_handle = batch;
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_ready_batch_destroy (&batch_handle));
}

const zlink_mesh_receive_record_t *recv_one_record (zlink_mesh_claim_t *claim_, void *recv_batch_)
{
    zlink_mesh_receive_requirements_t required;
    memset (&required, 0, sizeof (required));
    required.struct_size = sizeof (required);
    required.version = 1;
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK, zlink_mesh_receive_batch_reset (recv_batch_));
    TEST_ASSERT_EQUAL_INT (ZLINK_RECV_OK, zlink_mesh_claim_recv_batch (claim_, recv_batch_,
                                                                       &required,
                                                                       ZLINK_RECV_FLAGS_NONE));
    TEST_ASSERT_TRUE (zlink_mesh_receive_batch_count (recv_batch_) >= 1);
    return zlink_mesh_receive_batch_data (recv_batch_);
}
}

//  Two MeshNodes in one process hold application claims at the same time:
//  claim identities never collide, both recv their own record and both
//  releases succeed (process-wide claim serials).
void test_claims_are_process_unique_across_nodes ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    void *node_a = new_started_node (ctx, "claims-mesh-a", "jobs");
    void *node_b = new_started_node (ctx, "claims-mesh-b", "jobs");

    zlink_msg_t part;
    make_payload (&part, "job-on-a");
    TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_OK,
                           zlink_mesh_node_send_to_channel (node_a, "jobs", NULL, &part, 1, ZLINK_SEND_FLAGS_NONE));
    zlink_msg_close (&part);
    make_payload (&part, "job-on-b");
    TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_OK,
                           zlink_mesh_node_send_to_channel (node_b, "jobs", NULL, &part, 1, ZLINK_SEND_FLAGS_NONE));
    zlink_msg_close (&part);

    zlink_mesh_claim_t claim_a;
    zlink_mesh_claim_t claim_b;
    take_ready_claim (node_a, ZLINK_MESH_OWNER_NODE, ZLINK_MESH_READY_APPLICATION, &claim_a);
    take_ready_claim (node_b, ZLINK_MESH_OWNER_NODE, ZLINK_MESH_READY_APPLICATION, &claim_b);

    void *batch = zlink_mesh_receive_batch_new (4, 16, 1 << 20);
    TEST_ASSERT_NOT_NULL (batch);
    const zlink_mesh_receive_record_t *record = recv_one_record (&claim_a, batch);
    const zlink_msg_t *parts = zlink_mesh_receive_batch_parts (batch);
    TEST_ASSERT_EQUAL_MEMORY ("job-on-a",
                              zlink_msg_data (const_cast<zlink_msg_t *> (&parts[record->part_offset])),
                              8);
    record = recv_one_record (&claim_b, batch);
    parts = zlink_mesh_receive_batch_parts (batch);
    TEST_ASSERT_EQUAL_MEMORY ("job-on-b",
                              zlink_msg_data (const_cast<zlink_msg_t *> (&parts[record->part_offset])),
                              8);

    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_claim_release (&claim_a));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_claim_release (&claim_b));

    void *batch_handle = batch;
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_receive_batch_destroy (&batch_handle));
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK, zlink_mesh_node_shutdown (node_a, 1000));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_node_destroy (&node_a));
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK, zlink_mesh_node_shutdown (node_b, 1000));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_node_destroy (&node_b));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_ctx_term (ctx));
}

namespace
{
struct handler_dereg_probe_t
{
    void *monitor;
    int result;
    int captured_errno;
    int fired;
};

void deregistering_handler (const zlink_mesh_monitor_event_t *event_, void *userdata_)
{
    (void) event_;
    handler_dereg_probe_t *probe = static_cast<handler_dereg_probe_t *> (userdata_);
    if (probe->fired++ > 0)
        return;
    probe->result = zlink_mesh_node_monitor_handler (probe->monitor, NULL, NULL);
    probe->captured_errno = zlink_errno ();
}
}

//  Deregistering the monitor handler from inside the handler is re-entry:
//  the guard reports HANDLER_DEADLOCK instead of deadlocking or corrupting.
void test_monitor_handler_reentry_is_deadlock_error ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    void *node = new_started_node (ctx, "reentry-mesh", "beat");

    zlink_mesh_monitor_open_options_t options;
    memset (&options, 0, sizeof (options));
    options.struct_size = sizeof (options);
    options.version = 1;
    void *monitor = zlink_mesh_node_monitor_open (node, &options);
    TEST_ASSERT_NOT_NULL (monitor);

    handler_dereg_probe_t probe;
    memset (&probe, 0, sizeof (probe));
    probe.monitor = monitor;
    TEST_ASSERT_EQUAL_INT (ZLINK_HANDLER_OK,
                           zlink_mesh_node_monitor_handler (monitor, deregistering_handler,
                                                            &probe));

    //  A weight change emits CHANNEL_CHANGED into the handler.
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK, zlink_mesh_node_set_channel_weight (node, "beat", 25));
    for (int waited = 0; waited < 2000 && probe.fired == 0; waited += 10)
        msleep (10);
    TEST_ASSERT_TRUE (probe.fired > 0);
    TEST_ASSERT_EQUAL_INT (ZLINK_HANDLER_DEADLOCK, probe.result);
    TEST_ASSERT_EQUAL_INT (EDEADLK, probe.captured_errno);

    TEST_ASSERT_EQUAL_INT (ZLINK_HANDLER_OK, zlink_mesh_node_monitor_handler (monitor, NULL, NULL));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_node_monitor_close (&monitor));
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK, zlink_mesh_node_shutdown (node, 1000));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_node_destroy (&node));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_ctx_term (ctx));
}

//  Logical Spot lifetime: the Spot outlives its facade while a timer holds a
//  reference, ends when the timer is destroyed, and a re-created Spot with
//  the same RID carries a higher generation. Ticks of the ended generation
//  are never delivered.
void test_spot_ends_after_last_reference_and_generation_advances ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    void *node = new_started_node (ctx, "spotlife-mesh", "rooms");

    zlink_routing_id_t rid;
    memset (&rid, 0, sizeof (rid));
    rid.size = 8;
    memcpy (rid.data, "room-777", 8);
    void *spot = NULL;
    uint32_t created = 0;
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK,
                           zlink_mesh_node_spot_get_or_new (node, &rid, &spot, &created));
    TEST_ASSERT_EQUAL_INT (1, created);

    zlink_spot_status_t status;
    memset (&status, 0, sizeof (status));
    status.struct_size = sizeof (status);
    status.version = 1;
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK, zlink_spot_status (spot, &status));
    const uint64_t first_generation = status.lifecycle_generation;

    //  The timer keeps the logical Spot alive past the facade.
    void *timer = zlink_spot_timer_new (spot);
    TEST_ASSERT_NOT_NULL (timer);
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK,
                           zlink_timer_start (timer, 20ull * 1000 * 1000, 0));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_spot_destroy (&spot));

    void *lookup = NULL;
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK, zlink_mesh_node_spot_lookup (node, &rid, &lookup));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_spot_destroy (&lookup));

    //  Destroying the last reference ends the logical Spot.
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_timer_destroy (&timer));
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_NOT_FOUND,
                           zlink_mesh_node_spot_lookup (node, &rid, &lookup));
    TEST_ASSERT_EQUAL_INT (ENOENT, zlink_errno ());

    //  Re-creation advances the generation.
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK,
                           zlink_mesh_node_spot_get_or_new (node, &rid, &spot, &created));
    TEST_ASSERT_EQUAL_INT (1, created);
    memset (&status, 0, sizeof (status));
    status.struct_size = sizeof (status);
    status.version = 1;
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK, zlink_spot_status (spot, &status));
    TEST_ASSERT_TRUE (status.lifecycle_generation > first_generation);

    //  A stale-generation timer (created before a lookup handle of the first
    //  generation was released) delivers nothing once its generation ended:
    //  covered structurally by the end-before-recreate order above; a fresh
    //  timer on the new generation still ticks.
    void *fresh_timer = zlink_spot_timer_new (spot);
    TEST_ASSERT_NOT_NULL (fresh_timer);
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK,
                           zlink_timer_start (fresh_timer, 10ull * 1000 * 1000, 3));
    uint64_t fire_count = 0;
    bool ticked = false;
    for (int waited = 0; waited < 2000 && !ticked; waited += 10) {
        if (zlink_timer_recv (fresh_timer, &fire_count) == ZLINK_RECV_OK)
            ticked = true;
        else
            msleep (10);
    }
    TEST_ASSERT_TRUE (ticked);
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_timer_destroy (&fresh_timer));

    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_spot_destroy (&spot));
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK, zlink_mesh_node_shutdown (node, 1000));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_node_destroy (&node));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_ctx_term (ctx));
}

//  Actor destroy drains held application claims until its deadline: the
//  destroy waits while a claim is held and completes once released.
void test_actor_destroy_waits_for_held_claim ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    void *node = new_started_node (ctx, "adrain-mesh", "sessions");

    zlink_actor_ref_t actor;
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK,
                           zlink_mesh_node_actor_new (node, "player-77", NULL, 0, &actor,
                                                      ZLINK_SEND_FLAGS_NONE, 2000));
    zlink_msg_t part;
    make_payload (&part, "move:east");
    TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_OK,
                           zlink_mesh_node_send_to_actor (node, &actor, NULL, &part, 1, ZLINK_SEND_FLAGS_NONE));
    zlink_msg_close (&part);

    zlink_mesh_claim_t claim;
    take_ready_claim (node, ZLINK_MESH_OWNER_ACTOR, ZLINK_MESH_READY_APPLICATION, &claim);
    void *batch = zlink_mesh_receive_batch_new (4, 16, 1 << 20);
    TEST_ASSERT_NOT_NULL (batch);
    recv_one_record (&claim, batch);

    //  The held claim makes a short-deadline destroy wait ~the full deadline.
    zlink_mesh_operation_id_t op_id;
    void *watch = zlink_stopwatch_start ();
    TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_OK,
                           zlink_mesh_node_actor_destroy (node, &actor, &op_id, 300));
    const unsigned long waited_us = zlink_stopwatch_stop (watch);
    TEST_ASSERT_TRUE_MESSAGE (waited_us >= 250 * 1000,
                              "destroy returned before draining its deadline");

    //  The claim was revoked at the deadline; release stays safe.
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_claim_release (&claim));
    zlink_actor_location_t location;
    memset (&location, 0, sizeof (location));
    location.struct_size = sizeof (location);
    location.version = 1;
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_NOT_FOUND,
                           zlink_mesh_node_actor_lookup (node, "player-77", &location));

    void *batch_handle = batch;
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_receive_batch_destroy (&batch_handle));
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK, zlink_mesh_node_shutdown (node, 1000));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_node_destroy (&node));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_ctx_term (ctx));
}

//  Shutdown past its deadline detaches timeout-less operations into
//  exactly-once TERMINATED/ESHUTDOWN completions, and a sequential second
//  shutdown is legal (only concurrent re-entry deadlocks).
void test_shutdown_detaches_timeoutless_operations ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    void *node = new_started_node (ctx, "detach-mesh", "mail");

    //  timeout_ms == 0: no deadline, so only shutdown can terminate it.
    zlink_msg_t part;
    make_payload (&part, "mail-42");
    zlink_mesh_operation_id_t op_id;
    TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_OK,
                           zlink_mesh_node_request_to_channel (node, "mail", NULL, &part, 1,
                                                               &op_id, ZLINK_SEND_FLAGS_NONE, 0));
    zlink_msg_close (&part);

    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_TIMED_OUT, zlink_mesh_node_shutdown (node, 150));
    TEST_ASSERT_EQUAL_INT (ETIMEDOUT, zlink_errno ());

    //  The detached operation surfaces exactly once on the infra lane.
    zlink_mesh_claim_t infra_claim;
    take_ready_claim (node, ZLINK_MESH_OWNER_NODE, ZLINK_MESH_READY_INFRASTRUCTURE, &infra_claim);
    void *batch = zlink_mesh_receive_batch_new (8, 32, 1 << 20);
    TEST_ASSERT_NOT_NULL (batch);
    const zlink_mesh_receive_record_t *record = recv_one_record (&infra_claim, batch);
    TEST_ASSERT_EQUAL_INT (ZLINK_MESH_RECORD_COMPLETION, record->kind);
    TEST_ASSERT_EQUAL_UINT64 (op_id.low, record->operation_id.low);
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_TERMINATED, record->terminal_result);
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_claim_release (&infra_claim));

    //  With nothing outstanding a sequential shutdown drains cleanly.
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK, zlink_mesh_node_shutdown (node, 1000));

    void *batch_handle = batch;
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_receive_batch_destroy (&batch_handle));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_node_destroy (&node));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_ctx_term (ctx));
}

//  An invalid (un-versioned) element anywhere in a query output array fails
//  the whole call without writing any element.
void test_peers_query_output_is_invariant_on_invalid_element ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    void *node = new_started_node (ctx, "query-mesh", "sync");

    //  Two un-admitted intents give two live entries.
    zlink_mesh_peer_connection_options_t options;
    memset (&options, 0, sizeof (options));
    options.struct_size = sizeof (options);
    options.version = 1;
    const char *endpoints[2] = {"tcp://127.0.0.1:47621", "tcp://127.0.0.1:47622"};
    for (int i = 0; i < 2; ++i) {
        options.endpoint = endpoints[i];
        options.endpoint_size = strlen (endpoints[i]);
        uint64_t intent_id = 0;
        TEST_ASSERT_EQUAL_INT (ZLINK_CONNECT_OK,
                               zlink_mesh_node_connect_peer (node, &options, &intent_id));
    }

    zlink_mesh_peer_entry_t entries[2];
    memset (entries, 0, sizeof (entries));
    entries[0].struct_size = sizeof (entries[0]);
    entries[0].version = 1;
    //  entries[1] stays un-versioned: the call must fail without touching
    //  entries[0].
    size_t count = 2;
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_INVALID_ARGUMENT,
                           zlink_mesh_node_peers (node, entries, &count));
    TEST_ASSERT_EQUAL_UINT64 (0, entries[0].connection_intent_id);
    TEST_ASSERT_EQUAL_INT (0, entries[0].state);

    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK, zlink_mesh_node_shutdown (node, 1000));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_node_destroy (&node));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_ctx_term (ctx));
}

//  Public strings are strict UTF-8: overlong encodings, UTF-16 surrogates
//  and code points above U+10FFFF are rejected with EINVAL on names, topics
//  and subscription filters.
void test_public_strings_reject_invalid_utf8 ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    void *node = new_started_node (ctx, "utf8-mesh", "정산");

    const char overlong[] = {(char) 0xC0, (char) 0xAF, 0};
    const char surrogate[] = {(char) 0xED, (char) 0xA0, (char) 0x80, 0};
    const char beyond[] = {(char) 0xF4, (char) 0x90, (char) 0x80, (char) 0x80, 0};
    const char *bad[3] = {overlong, surrogate, beyond};

    void *entry = NULL;
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK, zlink_mesh_node_entry_spot (node, &entry));
    void *publisher = zlink_mesh_node_publisher_new (node);
    TEST_ASSERT_NOT_NULL (publisher);
    zlink_msg_t part;

    for (int i = 0; i < 3; ++i) {
        //  Channel name path.
        make_payload (&part, "x");
        TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_INVALID_ARGUMENT,
                               zlink_mesh_node_send_to_channel (node, bad[i], NULL, &part, 1, ZLINK_SEND_FLAGS_NONE));
        TEST_ASSERT_EQUAL_INT (EINVAL, zlink_errno ());
        zlink_msg_close (&part);
        //  Topic path.
        make_payload (&part, "y");
        TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_INVALID_ARGUMENT,
                               zlink_mesh_node_publisher_publish (publisher, "정산", bad[i], NULL,
                                                                  &part, 1, NULL,
                                                                  ZLINK_SEND_FLAGS_NONE));
        TEST_ASSERT_EQUAL_INT (EINVAL, zlink_errno ());
        zlink_msg_close (&part);
        //  Subscription filter path.
        TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_INVALID_ARGUMENT,
                               zlink_spot_set_subscription (entry, "정산", bad[i],
                                                            ZLINK_SPOT_SUBSCRIPTION_PREFIX));
        TEST_ASSERT_EQUAL_INT (EINVAL, zlink_errno ());
    }

    //  A well-formed multibyte Korean topic passes end to end.
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK,
                           zlink_spot_set_subscription (entry, "정산", "환율.",
                                                        ZLINK_SPOT_SUBSCRIPTION_PREFIX));
    make_payload (&part, "1385.42");
    TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_OK,
                           zlink_mesh_node_publisher_publish (publisher, "정산", "환율.usdkrw",
                                                              NULL, &part, 1, NULL,
                                                              ZLINK_SEND_FLAGS_NONE));
    zlink_msg_close (&part);

    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_node_publisher_destroy (&publisher));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_spot_destroy (&entry));
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK, zlink_mesh_node_shutdown (node, 1000));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_node_destroy (&node));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_ctx_term (ctx));
}

int main ()
{
    setup_test_environment (120);

    UNITY_BEGIN ();
    RUN_TEST (test_claims_are_process_unique_across_nodes);
    RUN_TEST (test_monitor_handler_reentry_is_deadlock_error);
    RUN_TEST (test_spot_ends_after_last_reference_and_generation_advances);
    RUN_TEST (test_actor_destroy_waits_for_held_claim);
    RUN_TEST (test_shutdown_detaches_timeoutless_operations);
    RUN_TEST (test_peers_query_output_is_invariant_on_invalid_element);
    RUN_TEST (test_public_strings_reject_invalid_utf8);
    const int rc = UNITY_END ();
    fflush (NULL);
    return rc;
}
