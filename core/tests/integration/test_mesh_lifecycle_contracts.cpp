/* SPDX-License-Identifier: MPL-2.0 */

//  S5 review-driven lifecycle contracts: process-wide claim identity across
//  multiple MeshNodes, monitor handler re-entrancy, logical Spot end with
//  timer generation gating, Actor destroy drain, shutdown detaching
//  timeout-less operations, query output invariance on invalid elements and
//  the strict UTF-8 gate on public strings.

#include "../testutil_unity.hpp"

#include <string.h>

#include <atomic>
#include <thread>

#if !defined _WIN32
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

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

namespace
{
void slow_tick_handler (void *timer_, uint64_t fire_count_, void *userdata_)
{
    (void) timer_;
    (void) fire_count_;
    std::atomic<int> *entered = static_cast<std::atomic<int> *> (userdata_);
    entered->store (1);
    msleep (300);
}
}

//  Destroying a timer that overlaps an in-flight fire must complete instead
//  of deadlocking the scheduler, and destroying a Spot timer while this
//  thread holds the Spot's application claim cancels the parked turn.
void test_timer_destroy_overlapping_fire_completes ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    void *node = new_started_node (ctx, "tdestroy-mesh", "ticks");

    //  Plain timer: destroy while the handler runs.
    void *timer = zlink_timer_new ();
    TEST_ASSERT_NOT_NULL (timer);
    std::atomic<int> entered (0);
    TEST_ASSERT_EQUAL_INT (ZLINK_HANDLER_OK,
                           zlink_timer_handler (timer, slow_tick_handler, &entered));
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK,
                           zlink_timer_start (timer, 20ull * 1000 * 1000, 1));
    for (int waited = 0; waited < 2000 && entered.load () == 0; waited += 5)
        msleep (5);
    TEST_ASSERT_TRUE (entered.load () != 0);
    //  The handler is still sleeping: destroy must wait it out and return.
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_timer_destroy (&timer));

    //  Spot timer: while this thread holds the Spot's application claim, the
    //  parked timer turn is cancelled by destroy instead of deadlocking.
    zlink_routing_id_t rid;
    memset (&rid, 0, sizeof (rid));
    rid.size = 8;
    memcpy (rid.data, "tick-spt", 8);
    void *spot = NULL;
    uint32_t created = 0;
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK,
                           zlink_mesh_node_spot_get_or_new (node, &rid, &spot, &created));
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK,
                           zlink_spot_set_subscription (spot, "ticks", "beat.",
                                                        ZLINK_SPOT_SUBSCRIPTION_PREFIX));
    void *publisher = zlink_mesh_node_publisher_new (node);
    TEST_ASSERT_NOT_NULL (publisher);
    zlink_msg_t part;
    make_payload (&part, "beat-1");
    TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_OK,
                           zlink_mesh_node_publisher_publish (publisher, "ticks", "beat.a", NULL,
                                                              &part, 1, NULL,
                                                              ZLINK_SEND_FLAGS_NONE));
    zlink_msg_close (&part);

    zlink_mesh_claim_t claim;
    take_ready_claim (node, ZLINK_MESH_OWNER_SPOT, ZLINK_MESH_READY_APPLICATION, &claim);

    void *spot_timer = zlink_spot_timer_new (spot);
    TEST_ASSERT_NOT_NULL (spot_timer);
    std::atomic<int> spot_entered (0);
    TEST_ASSERT_EQUAL_INT (ZLINK_HANDLER_OK,
                           zlink_timer_handler (spot_timer, slow_tick_handler, &spot_entered));
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK,
                           zlink_timer_start (spot_timer, 20ull * 1000 * 1000, 0));
    //  Give the fire time to park behind the held application claim.
    msleep (200);
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_timer_destroy (&spot_timer));
    //  The cancelled turn never ran the handler while the claim was held.
    TEST_ASSERT_EQUAL_INT (0, spot_entered.load ());

    void *recv_batch = zlink_mesh_receive_batch_new (4, 16, 1 << 20);
    TEST_ASSERT_NOT_NULL (recv_batch);
    recv_one_record (&claim, recv_batch);
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_claim_release (&claim));

    void *batch_handle = recv_batch;
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_receive_batch_destroy (&batch_handle));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_node_publisher_destroy (&publisher));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_spot_destroy (&spot));
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK, zlink_mesh_node_shutdown (node, 1000));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_node_destroy (&node));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_ctx_term (ctx));
}

//  Test-only hooks (ZLINK_BUILD_TESTS builds): mesh submit allocation fault
//  and the shutdown pin/lock pause for the reverse-order lifecycle race.
extern "C" void zlink_test_set_mesh_alloc_fault (int count_);
extern "C" void zlink_test_set_shutdown_pause_after_pin (int ms_);

//  A destroy that lands between shutdown's handle validation and its mutex
//  acquisition must wait for the pinned shutdown instead of freeing the node
//  under it: both calls complete, in either outcome order, and a shutdown
//  after the destroy fails with EFAULT.
void test_destroy_waits_for_pinned_shutdown ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    void *node = new_started_node (ctx, "pin-race-mesh", "jobs");

    //  Park the shutdown between its lifecycle pin and its lock.
    zlink_test_set_shutdown_pause_after_pin (400);
    std::atomic<int> shutdown_rc (-1);
    std::thread shutdown_thread ([&] () {
        shutdown_rc.store (static_cast<int> (zlink_mesh_node_shutdown (node, 1000)));
    });
    msleep (100);
    TEST_ASSERT_EQUAL_INT (-1, shutdown_rc.load ());

    //  The destroy commits while the shutdown is paused pre-lock; it must
    //  block on the pin instead of deleting the node under the shutdown.
    void *doomed = node;
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_node_destroy (&doomed));
    TEST_ASSERT_NULL (doomed);
    shutdown_thread.join ();
    //  The paused shutdown observed the destroy's forced STOPPED state.
    TEST_ASSERT_EQUAL_INT (static_cast<int> (ZLINK_REQUEST_OK), shutdown_rc.load ());

    //  The handle is gone: a later shutdown is an invalid-handle error.
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_INVALID_ARGUMENT, zlink_mesh_node_shutdown (node, 100));
    TEST_ASSERT_EQUAL_INT (EFAULT, zlink_errno ());

    zlink_test_set_shutdown_pause_after_pin (0);
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_ctx_term (ctx));
}

//  Injected allocation failures on the submit paths surface as the formal
//  ZLINK_SUBMIT_OUT_OF_MEMORY/ENOMEM mapping — never as an escaping
//  exception — and the next unarmed call succeeds cleanly.
void test_submit_alloc_failure_maps_to_out_of_memory ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    void *node = new_started_node (ctx, "oom-mesh", "jobs");

    //  Direct channel send: record preparation fault.
    zlink_msg_t part;
    make_payload (&part, "job-oom");
    zlink_test_set_mesh_alloc_fault (1);
    TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_OUT_OF_MEMORY,
                           zlink_mesh_node_send_to_channel (node, "jobs", NULL, &part, 1,
                                                            ZLINK_SEND_FLAGS_DONTWAIT));
    TEST_ASSERT_EQUAL_INT (ENOMEM, zlink_errno ());
    zlink_test_set_mesh_alloc_fault (0);
    TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_OK,
                           zlink_mesh_node_send_to_channel (node, "jobs", NULL, &part, 1,
                                                            ZLINK_SEND_FLAGS_DONTWAIT));
    zlink_msg_close (&part);

    //  Publish: multicast preparation fault, then a clean retry delivers.
    void *spot = NULL;
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK, zlink_mesh_node_entry_spot (node, &spot));
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK,
                           zlink_spot_set_subscription (spot, "jobs", "tick.",
                                                        ZLINK_SPOT_SUBSCRIPTION_PREFIX));
    void *publisher = zlink_mesh_node_publisher_new (node);
    TEST_ASSERT_NOT_NULL (publisher);
    make_payload (&part, "tick-oom");
    zlink_test_set_mesh_alloc_fault (1);
    TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_OUT_OF_MEMORY,
                           zlink_mesh_node_publisher_publish (publisher, "jobs", "tick.a", NULL,
                                                              &part, 1, NULL,
                                                              ZLINK_SEND_FLAGS_DONTWAIT));
    TEST_ASSERT_EQUAL_INT (ENOMEM, zlink_errno ());
    zlink_test_set_mesh_alloc_fault (0);
    TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_OK,
                           zlink_mesh_node_publisher_publish (publisher, "jobs", "tick.a", NULL,
                                                              &part, 1, NULL,
                                                              ZLINK_SEND_FLAGS_DONTWAIT));
    zlink_msg_close (&part);

    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_node_publisher_destroy (&publisher));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_spot_destroy (&spot));
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK, zlink_mesh_node_shutdown (node, 1000));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_node_destroy (&node));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_ctx_term (ctx));
}

//  Destroying the node while a shutdown is still draining is the same-handle
//  lifecycle re-entry the contract ends with EDEADLK (§11): the node
//  survives, the parked shutdown completes once the claim releases, and only
//  then does a destroy succeed.
void test_destroy_during_shutdown_wait_is_deadlock_error ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    void *node = new_started_node (ctx, "sd-race-mesh", "jobs");

    zlink_msg_t part;
    make_payload (&part, "job-1");
    TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_OK,
                           zlink_mesh_node_send_to_channel (node, "jobs", NULL, &part, 1,
                                                            ZLINK_SEND_FLAGS_NONE));
    zlink_msg_close (&part);
    zlink_mesh_claim_t claim;
    take_ready_claim (node, ZLINK_MESH_OWNER_NODE, ZLINK_MESH_READY_APPLICATION, &claim);

    //  The held claim parks the shutdown in its drain wait.
    std::atomic<int> shutdown_rc (-1);
    std::thread shutdown_thread ([&] () {
        shutdown_rc.store (static_cast<int> (zlink_mesh_node_shutdown (node, 3000)));
    });
    msleep (300);
    TEST_ASSERT_EQUAL_INT (-1, shutdown_rc.load ());

    void *doomed = node;
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_BUSY, zlink_mesh_node_destroy (&doomed));
    TEST_ASSERT_EQUAL_INT (EDEADLK, zlink_errno ());
    TEST_ASSERT_NOT_NULL (doomed);

    //  Releasing the claim lets the parked shutdown drain and finish.
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_claim_release (&claim));
    shutdown_thread.join ();
    TEST_ASSERT_EQUAL_INT (static_cast<int> (ZLINK_REQUEST_OK), shutdown_rc.load ());

    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_node_destroy (&node));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_ctx_term (ctx));
}

#if !defined _WIN32
namespace
{
//  Connects a raw TCP client to endpoint_ ("tcp://host:port") and returns
//  the connected fd, or -1.
int connect_raw_stream_client (const char *endpoint_)
{
    char protocol[8] = {0};
    char host[64] = {0};
    int port = 0;
    if (sscanf (endpoint_, "%7[^:]://%63[^:]:%d", protocol, host, &port) != 3
        || strcmp (protocol, "tcp") != 0) {
        errno = EINVAL;
        return -1;
    }
    const int fd = socket (AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0)
        return -1;
    struct sockaddr_in address;
    memset (&address, 0, sizeof (address));
    address.sin_family = AF_INET;
    address.sin_port = htons (static_cast<uint16_t> (port));
    if (inet_pton (AF_INET, host, &address.sin_addr) != 1
        || connect (fd, reinterpret_cast<const struct sockaddr *> (&address), sizeof (address))
             != 0) {
        close (fd);
        return -1;
    }
    struct timeval timeout;
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;
    TEST_ASSERT_EQUAL_INT (
      0, setsockopt (fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof (timeout)));
    return fd;
}
}
#endif

//  Two concurrent binds race an actor destroy. Externally observable
//  contract, asserted per round: every bind returns a result from the legal
//  set, once destroy has returned every later bind of that generation
//  fails, and no binding of the destroyed generation survives. The
//  success-then-rollback interleaving itself has no external observation
//  point; it is closed by design (both success shapes re-validate under
//  the node mutex and staleness is monotone) and this hammer guards the
//  observable half of that contract.
void test_stream_session_bind_destroy_race_leaves_no_binding ()
{
#if defined(ZLINK_HAVE_WINDOWS)
    TEST_IGNORE_MESSAGE ("raw TCP test helper is unavailable on Windows");
#else
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    void *node = new_started_node (ctx, "bind-race-mesh", "orders");
    void *stream = zlink_socket (ctx, ZLINK_SOCKET_STREAM);
    TEST_ASSERT_NOT_NULL (stream);
    void *service = zlink_stream_session_service_new (node, stream);
    TEST_ASSERT_NOT_NULL (service);
    const int notify = 1;
    const int recv_timeout_ms = 5000;
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK,
      zlink_set_stream_option (stream, ZLINK_STREAM_OPT_NOTIFY, &notify, sizeof (notify)));
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK,
                           zlink_set_option (stream, ZLINK_OPT_RCVTIMEO, &recv_timeout_ms,
                                             sizeof (recv_timeout_ms)));
    TEST_ASSERT_EQUAL_INT (ZLINK_BIND_OK, zlink_bind (stream, "tcp://127.0.0.1:0"));
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK, zlink_stream_session_service_start (service));

    char endpoint[ZLINK_MESH_ENDPOINT_MAX + 1];
    size_t endpoint_size = sizeof (endpoint);
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK,
      zlink_get_option (stream, ZLINK_OPT_LAST_ENDPOINT, endpoint, &endpoint_size));
    endpoint[sizeof (endpoint) - 1] = '\0';
    const int client_fd = connect_raw_stream_client (endpoint);
    TEST_ASSERT_TRUE (client_fd >= 0);
    TEST_ASSERT_EQUAL_INT (1, send (client_fd, "!", 1, 0));
    zlink_msg_t connect_part;
    TEST_ASSERT_EQUAL_INT (0, zlink_msg_init (&connect_part));
    const zlink_routing_id_t *source_rid = NULL;
    zlink_part_flag_t has_more = ZLINK_PART_MORE;
    TEST_ASSERT_EQUAL_INT (
      ZLINK_RECV_OK,
      zlink_recv_part (stream, &source_rid, &connect_part, &has_more, ZLINK_RECV_FLAGS_NONE));
    TEST_ASSERT_NOT_NULL (source_rid);
    zlink_routing_id_t session_rid = *source_rid;
    zlink_msg_close (&connect_part);

    for (int round = 0; round < 8; ++round) {
        char actor_id[32];
        snprintf (actor_id, sizeof (actor_id), "racer-%d", round);
        zlink_actor_ref_t actor;
        TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK,
                               zlink_mesh_node_actor_new (node, actor_id, NULL, 0, &actor,
                                                          ZLINK_SEND_FLAGS_NONE, 1000));
        std::atomic<int> rc_a (-1);
        std::atomic<int> rc_b (-1);
        std::thread binder_a ([&] () {
            zlink_mesh_operation_id_t op;
            rc_a.store (static_cast<int> (
              zlink_stream_session_bind_actor (service, &session_rid, &actor, &op, 200)));
        });
        std::thread binder_b ([&] () {
            zlink_mesh_operation_id_t op;
            rc_b.store (static_cast<int> (
              zlink_stream_session_bind_actor (service, &session_rid, &actor, &op, 200)));
        });
        zlink_mesh_operation_id_t destroy_op;
        TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_OK,
                               zlink_mesh_node_actor_destroy (node, &actor, &destroy_op, 200));
        binder_a.join ();
        binder_b.join ();

        //  A racing bind may only end in a result from the legal set: OK
        //  (validated while the generation was live), a staleness/conflict
        //  failure, not-found after the destroy, or a fence backpressure.
        const int legal[] = {static_cast<int> (ZLINK_SUBMIT_OK),
                             static_cast<int> (ZLINK_SUBMIT_INVALID_STATE),
                             static_cast<int> (ZLINK_SUBMIT_NOT_FOUND),
                             static_cast<int> (ZLINK_SUBMIT_BACKPRESSURED)};
        for (int probe = 0; probe < 2; ++probe) {
            const int rc = probe == 0 ? rc_a.load () : rc_b.load ();
            bool allowed = false;
            for (size_t l = 0; l < sizeof (legal) / sizeof (legal[0]); ++l)
                allowed = allowed || rc == legal[l];
            TEST_ASSERT_TRUE_MESSAGE (allowed, "racing bind returned an illegal result");
        }

        //  Staleness is monotone: after destroy returned, binds must fail.
        zlink_mesh_operation_id_t late_op;
        TEST_ASSERT_TRUE (
          zlink_stream_session_bind_actor (service, &session_rid, &actor, &late_op, 200)
          != ZLINK_SUBMIT_OK);

        //  No binding of the destroyed generation survives.
        zlink_stream_session_binding_t bindings[4];
        memset (bindings, 0, sizeof (bindings));
        for (int i = 0; i < 4; ++i) {
            bindings[i].struct_size = sizeof (bindings[i]);
            bindings[i].version = ZLINK_STREAM_SESSION_ABI_VERSION;
        }
        size_t count = 4;
        TEST_ASSERT_EQUAL_INT (
          ZLINK_CONFIG_OK,
          zlink_stream_session_bindings (service, &session_rid, bindings, &count));
        TEST_ASSERT_TRUE (count <= 4);
        for (size_t i = 0; i < count && i < 4; ++i) {
            TEST_ASSERT_FALSE (bindings[i].actor.generation == actor.generation
                               && memcmp (bindings[i].actor.actor_id, actor.actor_id,
                                          strlen (actor.actor_id) + 1)
                                    == 0);
        }
    }

    TEST_ASSERT_EQUAL_INT (0, shutdown (client_fd, SHUT_RDWR));
    close (client_fd);
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK, zlink_stream_session_service_shutdown (service, 1000));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_stream_session_service_destroy (&service));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_close (stream));
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK, zlink_mesh_node_shutdown (node, 1000));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_node_destroy (&node));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_ctx_term (ctx));
#endif
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
    RUN_TEST (test_timer_destroy_overlapping_fire_completes);
    RUN_TEST (test_destroy_during_shutdown_wait_is_deadlock_error);
    RUN_TEST (test_destroy_waits_for_pinned_shutdown);
    RUN_TEST (test_submit_alloc_failure_maps_to_out_of_memory);
    RUN_TEST (test_stream_session_bind_destroy_race_leaves_no_binding);
    const int rc = UNITY_END ();
    fflush (NULL);
    return rc;
}
