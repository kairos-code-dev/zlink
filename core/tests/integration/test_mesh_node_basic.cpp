/* SPDX-License-Identifier: MPL-2.0 */

//  Single-node MeshNode contract coverage: lifecycle gates, local channel
//  selection, the claim/batch receive path, one-shot replies, Logical
//  Multicast with local subscriptions and the Actor control lane.

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

zlink_mesh_ready_domain_mask_t ignore_ready_handler (void *node_,
                                                     zlink_mesh_ready_domain_mask_t mask_,
                                                     void *userdata_)
{
    (void) node_;
    (void) userdata_;
    return mask_;
}

void make_payload (zlink_msg_t *msg_, const char *text_)
{
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK, zlink_msg_init_size (msg_, strlen (text_)));
    memcpy (zlink_msg_data (msg_), text_, strlen (text_));
}

//  Drains one ready record of the wanted owner kind and domain into
//  claim_out_. Fails the test if it does not arrive.
void take_ready_claim (void *node_,
                       zlink_mesh_owner_kind_t owner_kind_,
                       zlink_mesh_ready_domain_mask_t domain_,
                       zlink_mesh_claim_t *claim_out_)
{
    void *batch = zlink_mesh_ready_batch_new (8);
    TEST_ASSERT_NOT_NULL (batch);
    bool taken = false;
    for (int attempt = 0; attempt < 100 && !taken; ++attempt) {
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
}

void test_mesh_node_lifecycle_gates ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    //  Options must be versioned and carry a mesh name.
    zlink_mesh_node_options_t options;
    memset (&options, 0, sizeof (options));
    TEST_ASSERT_NULL (zlink_mesh_node_new (ctx, &options));
    TEST_ASSERT_EQUAL_INT (EINVAL, zlink_errno ());

    options.struct_size = sizeof (options);
    options.version = 1;
    options.mesh_name = "orders-mesh";
    options.mesh_name_size = strlen ("orders-mesh");
    void *node = zlink_mesh_node_new (ctx, &options);
    TEST_ASSERT_NOT_NULL (node);

    //  Same process, same MeshName: EEXIST.
    TEST_ASSERT_NULL (zlink_mesh_node_new (ctx, &options));
    TEST_ASSERT_EQUAL_INT (EEXIST, zlink_errno ());

    //  Start requires routing id, bind endpoint and one channel.
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_INVALID_STATE, zlink_mesh_node_start (node));
    TEST_ASSERT_EQUAL_INT (EINVAL, zlink_errno ());

    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK, zlink_set_routing_id (node, "node-a", 6));
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK, zlink_mesh_node_set_bind (node, "tcp://127.0.0.1:0"));
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK, zlink_mesh_node_add_channel_name (node, "billing"));
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_CONFLICT, zlink_mesh_node_add_channel_name (node, "billing"));
    TEST_ASSERT_EQUAL_INT (EEXIST, zlink_errno ());
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK, zlink_mesh_node_start (node));

    //  Membership is frozen after start.
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_INVALID_STATE,
                           zlink_mesh_node_add_channel_name (node, "audit"));
    TEST_ASSERT_EQUAL_INT (EBUSY, zlink_errno ());

    //  Weight changes stay legal at runtime and bump the descriptor revision.
    zlink_mesh_node_status_t status;
    memset (&status, 0, sizeof (status));
    status.struct_size = sizeof (status);
    status.version = 1;
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK, zlink_mesh_node_status (node, &status));
    TEST_ASSERT_EQUAL_INT (ZLINK_MESH_NODE_READY, status.state);
    const uint64_t revision_before = status.descriptor_revision;
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK, zlink_mesh_node_set_channel_weight (node, "billing", 0));
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK, zlink_mesh_node_status (node, &status));
    TEST_ASSERT_TRUE (status.descriptor_revision > revision_before);

    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK, zlink_mesh_node_shutdown (node, 1000));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_node_destroy (&node));
    TEST_ASSERT_NULL (node);
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_ctx_term (ctx));
}

void test_mesh_local_channel_request_reply ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    void *node = new_started_node (ctx, "trades-mesh", "fills");

    //  Local membership makes the single node a channel target.
    zlink_msg_t part;
    make_payload (&part, "fill-2381");
    zlink_mesh_operation_id_t op_id;
    TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_OK,
                           zlink_mesh_node_request_to_channel (node, "fills", NULL, &part, 1,
                                                               &op_id, ZLINK_SEND_FLAGS_NONE,
                                                               5000));
    TEST_ASSERT_TRUE (op_id.low != 0 || op_id.high != 0);
    zlink_msg_close (&part);

    //  The request arrives on the Node application claim.
    zlink_mesh_claim_t claim;
    take_ready_claim (node, ZLINK_MESH_OWNER_NODE, ZLINK_MESH_READY_APPLICATION, &claim);

    void *recv_batch = zlink_mesh_receive_batch_new (8, 32, 1 << 20);
    TEST_ASSERT_NOT_NULL (recv_batch);
    zlink_mesh_receive_requirements_t required;
    memset (&required, 0, sizeof (required));
    required.struct_size = sizeof (required);
    required.version = 1;
    TEST_ASSERT_EQUAL_INT (ZLINK_RECV_OK, zlink_mesh_claim_recv_batch (&claim, recv_batch,
                                                                       &required,
                                                                       ZLINK_RECV_FLAGS_NONE));
    TEST_ASSERT_EQUAL_UINT64 (1, zlink_mesh_receive_batch_count (recv_batch));
    const zlink_mesh_receive_record_t *record = zlink_mesh_receive_batch_data (recv_batch);
    TEST_ASSERT_EQUAL_INT (ZLINK_MESH_RECORD_CHANNEL_REQUEST, record->kind);
    TEST_ASSERT_EQUAL_UINT64 (1, record->part_count);
    const zlink_msg_t *parts = zlink_mesh_receive_batch_parts (recv_batch);
    TEST_ASSERT_EQUAL_MEMORY ("fill-2381",
                              zlink_msg_data (const_cast<zlink_msg_t *> (&parts[record->part_offset])),
                              9);

    //  One-shot reply through the sealed token.
    zlink_msg_t reply_part;
    make_payload (&reply_part, "ack-2381");
    TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_OK,
                           zlink_mesh_reply (&record->reply_token, &reply_part, 1,
                                             ZLINK_SEND_FLAGS_NONE));
    //  A second reply on the same token must fail with EALREADY.
    TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_INVALID_STATE,
                           zlink_mesh_reply (&record->reply_token, &reply_part, 1,
                                             ZLINK_SEND_FLAGS_NONE));
    TEST_ASSERT_EQUAL_INT (EALREADY, zlink_errno ());
    zlink_msg_close (&reply_part);

    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_claim_release (&claim));

    //  The requester's completion arrives on the Node infrastructure claim.
    zlink_mesh_claim_t infra_claim;
    take_ready_claim (node, ZLINK_MESH_OWNER_NODE, ZLINK_MESH_READY_INFRASTRUCTURE, &infra_claim);
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK, zlink_mesh_receive_batch_reset (recv_batch));
    TEST_ASSERT_EQUAL_INT (ZLINK_RECV_OK,
                           zlink_mesh_claim_recv_batch (&infra_claim, recv_batch, &required,
                                                        ZLINK_RECV_FLAGS_NONE));
    TEST_ASSERT_EQUAL_UINT64 (1, zlink_mesh_receive_batch_count (recv_batch));
    record = zlink_mesh_receive_batch_data (recv_batch);
    TEST_ASSERT_EQUAL_INT (ZLINK_MESH_RECORD_COMPLETION, record->kind);
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK, record->terminal_result);
    TEST_ASSERT_EQUAL_UINT64 (op_id.low, record->operation_id.low);
    TEST_ASSERT_EQUAL_UINT64 (1, record->part_count);
    parts = zlink_mesh_receive_batch_parts (recv_batch);
    TEST_ASSERT_EQUAL_MEMORY ("ack-2381",
                              zlink_msg_data (const_cast<zlink_msg_t *> (&parts[record->part_offset])),
                              8);
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_claim_release (&infra_claim));

    void *batch_handle = recv_batch;
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_receive_batch_destroy (&batch_handle));
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK, zlink_mesh_node_shutdown (node, 1000));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_node_destroy (&node));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_ctx_term (ctx));
}

void test_mesh_local_multicast_and_subscription ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    void *node = new_started_node (ctx, "metrics-mesh", "telemetry");

    //  A user Spot subscribes to a prefix on the channel.
    zlink_routing_id_t spot_rid;
    memset (&spot_rid, 0, sizeof (spot_rid));
    spot_rid.size = 9;
    memcpy (spot_rid.data, "collector", 9);
    void *spot = NULL;
    uint32_t created = 0;
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK,
                           zlink_mesh_node_spot_get_or_new (node, &spot_rid, &spot, &created));
    TEST_ASSERT_EQUAL_INT (1, created);
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK,
                           zlink_spot_set_subscription (spot, "telemetry", "cpu.",
                                                        ZLINK_SPOT_SUBSCRIPTION_PREFIX));

    //  Publish through the node publisher; the local match must deliver
    //  exactly once with NODROP accounting.
    void *publisher = zlink_mesh_node_publisher_new (node);
    TEST_ASSERT_NOT_NULL (publisher);
    zlink_msg_t part;
    make_payload (&part, "cpu-load=0.42");
    zlink_mesh_publish_detail_t detail;
    memset (&detail, 0, sizeof (detail));
    detail.struct_size = sizeof (detail);
    detail.version = 1;
    TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_OK,
                           zlink_mesh_node_publisher_publish (publisher, "telemetry", "cpu.load",
                                                              NULL, &part, 1, &detail,
                                                              ZLINK_SEND_FLAGS_NONE));
    zlink_msg_close (&part);
    TEST_ASSERT_EQUAL_UINT32 (1, detail.snapshot_local_spot_count);
    TEST_ASSERT_EQUAL_UINT32 (1, detail.admitted_local_spot_count);
    TEST_ASSERT_EQUAL_UINT32 (0, detail.dropped_local_spot_count);

    //  A topic outside the subscription reaches no target.
    make_payload (&part, "disk-free=91");
    TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_NOT_FOUND,
                           zlink_mesh_node_publisher_publish (publisher, "telemetry", "disk.free",
                                                              NULL, &part, 1, &detail,
                                                              ZLINK_SEND_FLAGS_NONE));
    zlink_msg_close (&part);

    //  The subscriber receives the multicast record with channel and topic.
    zlink_mesh_claim_t claim;
    take_ready_claim (node, ZLINK_MESH_OWNER_SPOT, ZLINK_MESH_READY_APPLICATION, &claim);
    void *recv_batch = zlink_mesh_receive_batch_new (4, 16, 1 << 20);
    zlink_mesh_receive_requirements_t required;
    memset (&required, 0, sizeof (required));
    required.struct_size = sizeof (required);
    required.version = 1;
    TEST_ASSERT_EQUAL_INT (ZLINK_RECV_OK, zlink_mesh_claim_recv_batch (&claim, recv_batch,
                                                                       &required,
                                                                       ZLINK_RECV_FLAGS_NONE));
    const zlink_mesh_receive_record_t *record = zlink_mesh_receive_batch_data (recv_batch);
    TEST_ASSERT_EQUAL_INT (ZLINK_MESH_RECORD_SPOT_MULTICAST, record->kind);
    TEST_ASSERT_EQUAL_STRING_LEN ("telemetry", record->channel_name, record->channel_name_size);
    TEST_ASSERT_EQUAL_STRING_LEN ("cpu.load", record->topic, record->topic_size);
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_claim_release (&claim));

    void *batch_handle = recv_batch;
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_receive_batch_destroy (&batch_handle));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_node_publisher_destroy (&publisher));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_spot_destroy (&spot));
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK, zlink_mesh_node_shutdown (node, 1000));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_node_destroy (&node));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_ctx_term (ctx));
}

void test_mesh_actor_lifecycle_and_messaging ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    void *node = new_started_node (ctx, "game-mesh", "sessions");

    //  Creation commits the actor and the entry-Spot CREATED control record.
    zlink_actor_ref_t actor;
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK,
                           zlink_mesh_node_actor_new (node, "player-981", NULL, 0, &actor,
                                                      ZLINK_SEND_FLAGS_NONE, 1000));
    TEST_ASSERT_TRUE (actor.generation != 0);
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_CONFLICT,
                           zlink_mesh_node_actor_new (node, "player-981", NULL, 0, NULL,
                                                      ZLINK_SEND_FLAGS_NONE, 1000));
    TEST_ASSERT_EQUAL_INT (EEXIST, zlink_errno ());

    zlink_actor_location_t location;
    memset (&location, 0, sizeof (location));
    location.struct_size = sizeof (location);
    location.version = 1;
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK,
                           zlink_mesh_node_actor_lookup (node, "player-981", &location));
    TEST_ASSERT_EQUAL_UINT64 (actor.generation, location.actor.generation);
    TEST_ASSERT_EQUAL_UINT64 (1, location.membership_epoch);

    //  Direct actor messaging lands on the actor application claim.
    zlink_msg_t part;
    make_payload (&part, "move:north");
    TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_OK,
                           zlink_mesh_node_send_to_actor (node, &actor, NULL, &part, 1,
                                                          ZLINK_SEND_FLAGS_NONE));
    zlink_msg_close (&part);

    zlink_mesh_claim_t claim;
    take_ready_claim (node, ZLINK_MESH_OWNER_ACTOR, ZLINK_MESH_READY_APPLICATION, &claim);
    void *recv_batch = zlink_mesh_receive_batch_new (4, 16, 1 << 20);
    zlink_mesh_receive_requirements_t required;
    memset (&required, 0, sizeof (required));
    required.struct_size = sizeof (required);
    required.version = 1;
    TEST_ASSERT_EQUAL_INT (ZLINK_RECV_OK, zlink_mesh_claim_recv_batch (&claim, recv_batch,
                                                                       &required,
                                                                       ZLINK_RECV_FLAGS_NONE));
    const zlink_mesh_receive_record_t *record = zlink_mesh_receive_batch_data (recv_batch);
    TEST_ASSERT_EQUAL_INT (ZLINK_MESH_RECORD_ACTOR_SEND, record->kind);
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_claim_release (&claim));

    //  Destroy completes with a terminal operation result.
    zlink_mesh_operation_id_t op_id;
    TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_OK,
                           zlink_mesh_node_actor_destroy (node, &actor, &op_id, 1000));
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_NOT_FOUND,
                           zlink_mesh_node_actor_lookup (node, "player-981", &location));
    TEST_ASSERT_EQUAL_INT (ENOENT, zlink_errno ());

    void *batch_handle = recv_batch;
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_receive_batch_destroy (&batch_handle));
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK, zlink_mesh_node_shutdown (node, 1000));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_node_destroy (&node));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_ctx_term (ctx));
}

//  MeshNode poller source: POLLIN reflects the ready index, events name the
//  node handle with the MESH_NODE source kind, and the poller stays the
//  single ready consumer while registered.
void test_mesh_node_poller_source ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    void *node = new_started_node (ctx, "mesh-poll", "orders");
    void *poller = zlink_poller_new ();
    TEST_ASSERT_NOT_NULL (poller);

    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK,
                           zlink_poller_add (poller, node, (void *) 0x77, ZLINK_POLLIN));
    //  receive-mode exclusivity: a ready handler cannot coexist.
    TEST_ASSERT_EQUAL_INT (ZLINK_HANDLER_BUSY,
                           zlink_mesh_node_set_ready_handler (node, ignore_ready_handler, NULL));

    //  no work yet: the wait times out without an event.
    zlink_poller_event_t event;
    memset (&event, 0, sizeof (event));
    TEST_ASSERT_EQUAL_INT (0, zlink_poller_wait (poller, &event, 1, 100, NULL));

    //  queued local work raises POLLIN with the MESH_NODE source.
    zlink_msg_t part;
    make_payload (&part, "poll-ping");
    TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_OK,
                           zlink_mesh_node_send_to_channel (node, "orders", NULL, &part, 1,
                                                            ZLINK_SEND_FLAGS_NONE));
    zlink_msg_close (&part);

    const int got = zlink_poller_wait (poller, &event, 1, 2000, NULL);
    TEST_ASSERT_EQUAL_INT (1, got);
    TEST_ASSERT_EQUAL_INT (ZLINK_POLLER_SOURCE_MESH_NODE, event.source_kind);
    TEST_ASSERT_EQUAL_PTR (node, event.socket);
    TEST_ASSERT_EQUAL_PTR ((void *) 0x77, event.user_data);
    TEST_ASSERT_TRUE ((event.events & ZLINK_POLLIN) != 0);

    //  drain and claim: the level-triggered source goes quiet afterwards.
    zlink_mesh_claim_t claim;
    take_ready_claim (node, ZLINK_MESH_OWNER_NODE, ZLINK_MESH_READY_APPLICATION, &claim);
    void *batch = zlink_mesh_receive_batch_new (8, 32, 64 * 1024);
    TEST_ASSERT_NOT_NULL (batch);
    zlink_mesh_receive_requirements_t requirements;
    memset (&requirements, 0, sizeof (requirements));
    TEST_ASSERT_EQUAL_INT (ZLINK_RECV_OK,
                           zlink_mesh_claim_recv_batch (&claim, batch, &requirements,
                                                        ZLINK_RECV_FLAGS_NONE));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_claim_release (&claim));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_receive_batch_destroy (&batch));
    memset (&event, 0, sizeof (event));
    TEST_ASSERT_EQUAL_INT (0, zlink_poller_wait (poller, &event, 1, 100, NULL));

    //  remove re-opens the handler registration path.
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK, zlink_poller_remove (poller, node));
    TEST_ASSERT_EQUAL_INT (ZLINK_HANDLER_OK,
                           zlink_mesh_node_set_ready_handler (node, ignore_ready_handler, NULL));
    TEST_ASSERT_EQUAL_INT (ZLINK_HANDLER_OK,
                           zlink_mesh_node_set_ready_handler (node, NULL, NULL));

    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_poller_destroy (&poller));
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK, zlink_mesh_node_shutdown (node, 1000));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_node_destroy (&node));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_ctx_term (ctx));
}

int main ()
{
    setup_test_environment (120);

    UNITY_BEGIN ();
    RUN_TEST (test_mesh_node_lifecycle_gates);
    RUN_TEST (test_mesh_local_channel_request_reply);
    RUN_TEST (test_mesh_local_multicast_and_subscription);
    RUN_TEST (test_mesh_actor_lifecycle_and_messaging);
    RUN_TEST (test_mesh_node_poller_source);
    const int rc = UNITY_END ();
    fflush (NULL);
    return rc;
}
