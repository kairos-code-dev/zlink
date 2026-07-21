/* SPDX-License-Identifier: MPL-2.0 */

//  MeshNode monitor contract coverage: the event-kind matrix (state, channel,
//  submit, backpressure, multicast, completion, claim revoke), event mask
//  filtering, monitor child references, shutdown claim revocation and the
//  reply/recv error grid after shutdown, plus in-turn infrastructure progress
//  while an application claim is held.

#include "../testutil_unity.hpp"

#include <string.h>

#if !defined(ZLINK_HAVE_WINDOWS)
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

SETUP_TEARDOWN_TESTCONTEXT

namespace
{
void *new_configured_node (void *ctx_,
                           const char *name_,
                           const char *channel_,
                           uint64_t message_budget_)
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
    if (message_budget_ != 0) {
        TEST_ASSERT_EQUAL_INT (
          ZLINK_CONFIG_OK,
          zlink_set_mesh_node_option (node, ZLINK_MESH_NODE_OPT_MAILBOX_MESSAGE_BUDGET,
                                      &message_budget_, sizeof (message_budget_)));
    }
    return node;
}

void *open_monitor (void *node_, zlink_mesh_monitor_event_mask_t events_)
{
    zlink_mesh_monitor_open_options_t options;
    memset (&options, 0, sizeof (options));
    options.struct_size = sizeof (options);
    options.version = 1;
    options.events = events_;
    void *monitor = zlink_mesh_node_monitor_open (node_, &options);
    TEST_ASSERT_NOT_NULL (monitor);
    return monitor;
}

void make_payload (zlink_msg_t *msg_, const char *text_)
{
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK, zlink_msg_init_size (msg_, strlen (text_)));
    memcpy (zlink_msg_data (msg_), text_, strlen (text_));
}

#if !defined(ZLINK_HAVE_WINDOWS)
int connect_raw_tcp (const char *endpoint_)
{
    char protocol[8] = {0};
    char host[64] = {0};
    int port = 0;
    if (sscanf (endpoint_, "%7[^:]://%63[^:]:%d", protocol, host, &port) != 3
        || strcmp (protocol, "tcp") != 0)
        return -1;
    const int fd = socket (AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0)
        return -1;
    struct sockaddr_in address;
    memset (&address, 0, sizeof (address));
    address.sin_family = AF_INET;
    address.sin_port = htons (static_cast<uint16_t> (port));
    if (inet_pton (AF_INET, host, &address.sin_addr) != 1
        || connect (fd, reinterpret_cast<const struct sockaddr *> (&address),
                    sizeof (address))
             != 0) {
        close (fd);
        return -1;
    }
    return fd;
}
#endif

//  Waits until an event of kind_ arrives, skipping earlier events. Fails the
//  test when the deadline passes first.
void wait_monitor_event (void *monitor_,
                         zlink_mesh_monitor_event_kind_t kind_,
                         zlink_mesh_monitor_event_t *event_out_)
{
    for (int attempt = 0; attempt < 500; ++attempt) {
        memset (event_out_, 0, sizeof (*event_out_));
        event_out_->struct_size = sizeof (*event_out_);
        event_out_->version = 1;
        const zlink_recv_result_t rc =
          zlink_mesh_node_monitor_recv (monitor_, event_out_, ZLINK_RECV_FLAGS_DONTWAIT);
        if (rc == ZLINK_RECV_NO_DATA) {
            msleep (10);
            continue;
        }
        TEST_ASSERT_EQUAL_INT (ZLINK_RECV_OK, rc);
        if (event_out_->kind == kind_)
            return;
    }
    TEST_FAIL_MESSAGE ("expected monitor event kind did not arrive");
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

//  Receives the single record of the claim into recv_batch_ and returns the
//  record pointer (owned by the batch).
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

struct inline_retry_state_t
{
    inline_retry_state_t () :
        node (NULL), callback_count (0), fill_result (ZLINK_SUBMIT_INTERNAL_ERROR),
        retry_result (ZLINK_SUBMIT_INTERNAL_ERROR), retry_errno (0)
    {
    }

    void *node;
    int callback_count;
    zlink_submit_result_t fill_result;
    zlink_submit_result_t retry_result;
    int retry_errno;
};

//  Reproduces the strongest ready-handler re-entry allowed by the public
//  contract: consume the wakeup synchronously, let another send take the
//  recovered slot, then retry the original destination before Core returns
//  from committing the SEND_READY record.
zlink_mesh_ready_domain_mask_t inline_retry_ready_handler (
  void *, zlink_mesh_ready_domain_mask_t domains_, void *userdata_)
{
    inline_retry_state_t *state = static_cast<inline_retry_state_t *> (userdata_);
    if (!state || (domains_ & ZLINK_MESH_READY_INFRASTRUCTURE) == 0
        || state->callback_count != 0)
        return 0;

    ++state->callback_count;
    zlink_msg_t part;
    if (zlink_msg_init_size (&part, 1) != ZLINK_CONFIG_OK)
        return ZLINK_MESH_READY_INFRASTRUCTURE;
    *static_cast<unsigned char *> (zlink_msg_data (&part)) = 0x41;
    state->fill_result = zlink_mesh_node_send_to_channel (
      state->node, "orders", NULL, &part, 1, ZLINK_SEND_FLAGS_DONTWAIT);
    zlink_msg_close (&part);

    if (zlink_msg_init_size (&part, 1) != ZLINK_CONFIG_OK)
        return ZLINK_MESH_READY_INFRASTRUCTURE;
    *static_cast<unsigned char *> (zlink_msg_data (&part)) = 0x42;
    state->retry_result = zlink_mesh_node_send_to_channel (
      state->node, "orders", NULL, &part, 1, ZLINK_SEND_FLAGS_DONTWAIT);
    state->retry_errno = zlink_errno ();
    zlink_msg_close (&part);
    return ZLINK_MESH_READY_INFRASTRUCTURE;
}
}

//  A TCP client that cannot complete the socket protocol has no peer RID, but
//  its rejection still belongs to the MeshNode monitor stream. This is the
//  only observable surface for pre-admission handshake failures.
void test_raw_handshake_failure_emits_peer_rejected ()
{
#if defined(ZLINK_HAVE_WINDOWS)
    TEST_IGNORE_MESSAGE ("raw TCP test helper is unavailable on Windows");
#else
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    void *node = new_configured_node (ctx, "handshake-monitor", "audit", 0);
    void *monitor =
      open_monitor (node, 1ull << (ZLINK_MESH_MONITOR_PEER_REJECTED - 1));
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK, zlink_mesh_node_start (node));

    zlink_mesh_node_status_t status;
    memset (&status, 0, sizeof (status));
    status.struct_size = sizeof (status);
    status.version = 1;
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK, zlink_mesh_node_status (node, &status));
    const int fd = connect_raw_tcp (status.local_endpoint);
    TEST_ASSERT_TRUE (fd >= 0);
    unsigned char invalid_greeting[64];
    memset (invalid_greeting, 0, sizeof (invalid_greeting));
    TEST_ASSERT_EQUAL_INT (
      static_cast<int> (sizeof (invalid_greeting)),
      send (fd, invalid_greeting, sizeof (invalid_greeting), 0));
    shutdown (fd, SHUT_RDWR);
    close (fd);

    zlink_mesh_monitor_event_t event;
    wait_monitor_event (monitor, ZLINK_MESH_MONITOR_PEER_REJECTED, &event);
    TEST_ASSERT_EQUAL_UINT8 (0, event.peer_rid.size);
    TEST_ASSERT_EQUAL_INT (ZLINK_CONNECT_INTERNAL_ERROR, event.result_code);
    TEST_ASSERT_EQUAL_INT (EPROTO, event.failure_errno);

    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_node_monitor_close (&monitor));
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK, zlink_mesh_node_shutdown (node, 1000));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_node_destroy (&node));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_ctx_term (ctx));
#endif
}

//  Event matrix: STATE_CHANGED, CHANNEL_CHANGED, MESSAGE_SUBMITTED,
//  OPERATION_COMPLETED and MULTICAST_COMMITTED arrive with their contract
//  payload, counters reflect them, and the monitor keeps a strong child
//  reference until closed.
void test_monitor_event_matrix_and_child_reference ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    void *node = new_configured_node (ctx, "monitor-mesh", "audit", 1);
    void *monitor = open_monitor (node, 0);

    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK, zlink_mesh_node_start (node));
    zlink_mesh_monitor_event_t event;
    wait_monitor_event (monitor, ZLINK_MESH_MONITOR_STATE_CHANGED, &event);
    TEST_ASSERT_TRUE (event.mesh_lifecycle_generation != 0);

    //  Channel weight change bumps the descriptor revision.
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK, zlink_mesh_node_set_channel_weight (node, "audit", 40));
    wait_monitor_event (monitor, ZLINK_MESH_MONITOR_CHANNEL_CHANGED, &event);
    TEST_ASSERT_EQUAL_STRING ("audit", event.channel_name);
    TEST_ASSERT_TRUE (event.mesh_descriptor_revision > 1);

    //  A local channel request produces MESSAGE_SUBMITTED, then its reply
    //  produces OPERATION_COMPLETED with the operation id and terminal result.
    zlink_msg_t part;
    make_payload (&part, "ledger-entry-77");
    zlink_mesh_operation_id_t op_id;
    TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_OK,
                           zlink_mesh_node_request_to_channel (node, "audit", NULL, &part, 1,
                                                               &op_id, ZLINK_SEND_FLAGS_NONE,
                                                               5000));
    zlink_msg_close (&part);
    wait_monitor_event (monitor, ZLINK_MESH_MONITOR_MESSAGE_SUBMITTED, &event);
    TEST_ASSERT_EQUAL_INT (ZLINK_MESH_OWNER_NODE, event.owner_kind);

    zlink_mesh_claim_t claim;
    take_ready_claim (node, ZLINK_MESH_OWNER_NODE, ZLINK_MESH_READY_APPLICATION, &claim);
    void *recv_batch = zlink_mesh_receive_batch_new (8, 32, 1 << 20);
    TEST_ASSERT_NOT_NULL (recv_batch);
    const zlink_mesh_receive_record_t *record = recv_one_record (&claim, recv_batch);
    TEST_ASSERT_EQUAL_INT (ZLINK_MESH_RECORD_CHANNEL_REQUEST, record->kind);
    zlink_msg_t reply_part;
    make_payload (&reply_part, "ack-77");
    TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_OK,
                           zlink_mesh_reply (&record->reply_token, &reply_part, 1,
                                             ZLINK_SEND_FLAGS_NONE));
    zlink_msg_close (&reply_part);
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_claim_release (&claim));

    wait_monitor_event (monitor, ZLINK_MESH_MONITOR_OPERATION_COMPLETED, &event);
    TEST_ASSERT_EQUAL_UINT64 (op_id.low, event.operation_id_low);
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK, event.result_code);

    //  Drain the requester completion so the mailbox is empty again.
    zlink_mesh_claim_t infra_claim;
    take_ready_claim (node, ZLINK_MESH_OWNER_NODE, ZLINK_MESH_READY_INFRASTRUCTURE, &infra_claim);
    recv_one_record (&infra_claim, recv_batch);
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_claim_release (&infra_claim));

    //  Local Logical Multicast commits with the local/remote count grid.
    zlink_routing_id_t spot_rid;
    memset (&spot_rid, 0, sizeof (spot_rid));
    spot_rid.size = 7;
    memcpy (spot_rid.data, "watcher", 7);
    void *spot = NULL;
    uint32_t created = 0;
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK,
                           zlink_mesh_node_spot_get_or_new (node, &spot_rid, &spot, &created));
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK,
                           zlink_spot_set_subscription (spot, "audit", "trades.",
                                                        ZLINK_SPOT_SUBSCRIPTION_PREFIX));
    void *publisher = zlink_mesh_node_publisher_new (node);
    TEST_ASSERT_NOT_NULL (publisher);
    make_payload (&part, "fill:9911");
    TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_OK,
                           zlink_mesh_node_publisher_publish (publisher, "audit", "trades.us",
                                                              NULL, &part, 1, NULL,
                                                              ZLINK_SEND_FLAGS_NONE));
    zlink_msg_close (&part);
    wait_monitor_event (monitor, ZLINK_MESH_MONITOR_MULTICAST_COMMITTED, &event);
    TEST_ASSERT_EQUAL_STRING ("audit", event.channel_name);
    TEST_ASSERT_EQUAL_UINT32 (1, event.snapshot_local_spot_count);
    TEST_ASSERT_EQUAL_UINT32 (1, event.admitted_local_spot_count);
    TEST_ASSERT_EQUAL_UINT32 (0, event.dropped_local_spot_count);
    TEST_ASSERT_EQUAL_UINT32 (0, event.dropped_remote_target_count);

    //  The first multicast still occupies the one-record Spot mailbox.
    //  A second publish reports one aggregate drop and must not emit the
    //  ordinary target-level BACKPRESSURED event.
    make_payload (&part, "fill:9912");
    TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_OK,
                           zlink_mesh_node_publisher_publish (publisher, "audit", "trades.eu",
                                                              NULL, &part, 1, NULL,
                                                              ZLINK_SEND_FLAGS_DONTWAIT));
    zlink_msg_close (&part);
    bool aggregate_drop_seen = false;
    for (int attempt = 0; attempt < 500 && !aggregate_drop_seen; ++attempt) {
        memset (&event, 0, sizeof (event));
        event.struct_size = sizeof (event);
        event.version = 1;
        const zlink_recv_result_t rc =
          zlink_mesh_node_monitor_recv (monitor, &event, ZLINK_RECV_FLAGS_DONTWAIT);
        if (rc == ZLINK_RECV_NO_DATA) {
            msleep (10);
            continue;
        }
        TEST_ASSERT_EQUAL_INT (ZLINK_RECV_OK, rc);
        TEST_ASSERT_NOT_EQUAL (ZLINK_MESH_MONITOR_BACKPRESSURED, event.kind);
        if (event.kind == ZLINK_MESH_MONITOR_MULTICAST_DROPPED) {
            TEST_ASSERT_EQUAL_UINT32 (1, event.snapshot_local_spot_count);
            TEST_ASSERT_EQUAL_UINT32 (0, event.admitted_local_spot_count);
            TEST_ASSERT_EQUAL_UINT32 (1, event.dropped_local_spot_count);
            aggregate_drop_seen = true;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE (aggregate_drop_seen, "aggregate multicast drop event missing");

    //  The status snapshot reflects the counters without consuming events.
    zlink_mesh_monitor_status_t status;
    memset (&status, 0, sizeof (status));
    status.struct_size = sizeof (status);
    status.version = 1;
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK, zlink_mesh_node_monitor_status (monitor, &status));
    TEST_ASSERT_TRUE (status.submitted_messages >= 1);
    TEST_ASSERT_TRUE (status.completed_operations >= 1);
    TEST_ASSERT_TRUE (status.multicast_messages >= 1);

    //  The open monitor is a strong child reference: destroy must refuse.
    void *node_alias = node;
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_BUSY, zlink_mesh_node_destroy (&node_alias));
    TEST_ASSERT_EQUAL_INT (EBUSY, zlink_errno ());

    void *batch_handle = recv_batch;
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_receive_batch_destroy (&batch_handle));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_node_publisher_destroy (&publisher));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_spot_destroy (&spot));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_node_monitor_close (&monitor));
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK, zlink_mesh_node_shutdown (node, 1000));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_node_destroy (&node));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_ctx_term (ctx));
}

//  The event mask selects kinds by bit (1 << (kind - 1)); unselected kinds
//  never reach the queue while counters keep counting.
void test_monitor_event_mask_filters_kinds ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    void *node = new_configured_node (ctx, "filter-mesh", "billing", 0);
    void *monitor =
      open_monitor (node, 1ull << (ZLINK_MESH_MONITOR_CHANNEL_CHANGED - 1));

    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK, zlink_mesh_node_start (node));
    zlink_msg_t part;
    make_payload (&part, "invoice-3");
    TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_OK,
                           zlink_mesh_node_send_to_channel (node, "billing", NULL, &part, 1,
                                                            ZLINK_SEND_FLAGS_NONE));
    zlink_msg_close (&part);
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK, zlink_mesh_node_set_channel_weight (node, "billing", 10));

    //  Only the selected CHANNEL_CHANGED event may surface.
    zlink_mesh_monitor_event_t event;
    wait_monitor_event (monitor, ZLINK_MESH_MONITOR_CHANNEL_CHANGED, &event);
    TEST_ASSERT_EQUAL_STRING ("billing", event.channel_name);
    memset (&event, 0, sizeof (event));
    event.struct_size = sizeof (event);
    event.version = 1;
    TEST_ASSERT_EQUAL_INT (ZLINK_RECV_NO_DATA,
                           zlink_mesh_node_monitor_recv (monitor, &event,
                                                         ZLINK_RECV_FLAGS_DONTWAIT));

    //  Counters are mask-independent.
    zlink_mesh_monitor_status_t status;
    memset (&status, 0, sizeof (status));
    status.struct_size = sizeof (status);
    status.version = 1;
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK, zlink_mesh_node_monitor_status (monitor, &status));
    TEST_ASSERT_TRUE (status.submitted_messages >= 1);

    //  Drain the delivered record so shutdown drains cleanly.
    zlink_mesh_claim_t claim;
    take_ready_claim (node, ZLINK_MESH_OWNER_NODE, ZLINK_MESH_READY_APPLICATION, &claim);
    void *recv_batch = zlink_mesh_receive_batch_new (4, 16, 1 << 20);
    TEST_ASSERT_NOT_NULL (recv_batch);
    recv_one_record (&claim, recv_batch);
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_claim_release (&claim));
    void *batch_handle = recv_batch;
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_receive_batch_destroy (&batch_handle));

    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_node_monitor_close (&monitor));
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK, zlink_mesh_node_shutdown (node, 1000));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_node_destroy (&node));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_ctx_term (ctx));
}

//  Monitor status counters belong to the MeshNode lifecycle, not to one
//  monitor handle. Work completed before a monitor is opened, and work
//  observed by a monitor that is later replaced, must remain visible.
void test_monitor_counters_cover_lifecycle_before_open_and_after_reopen ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    void *node = new_configured_node (ctx, "late-monitor", "orders", 1);
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK, zlink_mesh_node_start (node));

    zlink_msg_t part;
    make_payload (&part, "order-1");
    TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_OK,
                           zlink_mesh_node_send_to_channel (node, "orders", NULL, &part, 1,
                                                            ZLINK_SEND_FLAGS_DONTWAIT));
    zlink_msg_close (&part);
    make_payload (&part, "order-2");
    TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_BACKPRESSURED,
                           zlink_mesh_node_send_to_channel (node, "orders", NULL, &part, 1,
                                                            ZLINK_SEND_FLAGS_DONTWAIT));
    zlink_msg_close (&part);

    void *monitor = open_monitor (node, 0);
    zlink_mesh_monitor_status_t status;
    memset (&status, 0, sizeof (status));
    status.struct_size = sizeof (status);
    status.version = 1;
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK, zlink_mesh_node_monitor_status (monitor, &status));
    TEST_ASSERT_EQUAL_UINT64 (1, status.submitted_messages);
    TEST_ASSERT_EQUAL_UINT64 (1, status.backpressured_submits);
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_node_monitor_close (&monitor));

    monitor = open_monitor (node, 0);
    memset (&status, 0, sizeof (status));
    status.struct_size = sizeof (status);
    status.version = 1;
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK, zlink_mesh_node_monitor_status (monitor, &status));
    TEST_ASSERT_EQUAL_UINT64 (1, status.submitted_messages);
    TEST_ASSERT_EQUAL_UINT64 (1, status.backpressured_submits);

    zlink_mesh_claim_t claim;
    take_ready_claim (node, ZLINK_MESH_OWNER_NODE, ZLINK_MESH_READY_APPLICATION, &claim);
    void *recv_batch = zlink_mesh_receive_batch_new (4, 16, 1 << 20);
    TEST_ASSERT_NOT_NULL (recv_batch);
    recv_one_record (&claim, recv_batch);
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_claim_release (&claim));

    void *batch_handle = recv_batch;
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_receive_batch_destroy (&batch_handle));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_node_monitor_close (&monitor));
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK, zlink_mesh_node_shutdown (node, 1000));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_node_destroy (&node));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_ctx_term (ctx));
}

//  Mailbox budget rejection surfaces BACKPRESSURED with the owner kind, and
//  the infrastructure domain keeps progressing while an application claim is
//  held: a request submitted during the turn completes (times out) on the
//  infrastructure lane before the application claim is released.
void test_backpressure_event_and_in_turn_infrastructure_progress ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    void *node = new_configured_node (ctx, "pressure-mesh", "orders", 1);
    void *monitor = open_monitor (node, 0);
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK, zlink_mesh_node_start (node));

    //  Budget 1: the first send fills the mailbox, the second is rejected.
    zlink_msg_t part;
    make_payload (&part, "order-1");
    TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_OK,
                           zlink_mesh_node_send_to_channel (node, "orders", NULL, &part, 1,
                                                            ZLINK_SEND_FLAGS_DONTWAIT));
    zlink_msg_close (&part);
    make_payload (&part, "order-2");
    TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_BACKPRESSURED,
                           zlink_mesh_node_send_to_channel (node, "orders", NULL, &part, 1,
                                                            ZLINK_SEND_FLAGS_DONTWAIT));
    TEST_ASSERT_EQUAL_INT (EAGAIN, zlink_errno ());
    zlink_msg_close (&part);

    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK,
                           zlink_msg_init_size (&part, 4096));
    memset (zlink_msg_data (&part), 0x5A, zlink_msg_size (&part));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_BACKPRESSURED,
      zlink_mesh_node_send_to_channel (
        node, "orders", NULL, &part, 1, ZLINK_SEND_FLAGS_DONTWAIT));
    TEST_ASSERT_EQUAL_INT (EAGAIN, zlink_errno ());
    zlink_msg_close (&part);

    zlink_mesh_monitor_event_t event;
    wait_monitor_event (monitor, ZLINK_MESH_MONITOR_BACKPRESSURED, &event);
    TEST_ASSERT_EQUAL_INT (ZLINK_MESH_OWNER_NODE, event.owner_kind);
    TEST_ASSERT_EQUAL_INT (EAGAIN, event.failure_errno);

    //  Take the application claim; the mailbox budget frees up once the
    //  record moves into the receive batch during the turn.
    zlink_mesh_claim_t claim;
    take_ready_claim (node, ZLINK_MESH_OWNER_NODE, ZLINK_MESH_READY_APPLICATION, &claim);
    void *recv_batch = zlink_mesh_receive_batch_new (8, 32, 1 << 20);
    TEST_ASSERT_NOT_NULL (recv_batch);
    recv_one_record (&claim, recv_batch);

    //  In-turn await: submit a request during the turn and wait for its
    //  terminal completion on the infrastructure lane while the application
    //  claim stays held. Nobody answers, so it times out.
    make_payload (&part, "order-3");
    zlink_mesh_operation_id_t op_id;
    TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_OK,
                           zlink_mesh_node_request_to_channel (node, "orders", NULL, &part, 1,
                                                               &op_id, ZLINK_SEND_FLAGS_NONE,
                                                               200));
    zlink_msg_close (&part);

    zlink_mesh_claim_t infra_claim;
    take_ready_claim (node, ZLINK_MESH_OWNER_NODE, ZLINK_MESH_READY_INFRASTRUCTURE, &infra_claim);
    void *infra_batch = zlink_mesh_receive_batch_new (8, 32, 1 << 20);
    TEST_ASSERT_NOT_NULL (infra_batch);
    const zlink_mesh_receive_record_t *record =
      recv_one_record (&infra_claim, infra_batch);
    if (record->kind == ZLINK_MESH_RECORD_SEND_READY)
        record = recv_one_record (&infra_claim, infra_batch);
    TEST_ASSERT_EQUAL_INT (ZLINK_MESH_RECORD_COMPLETION, record->kind);
    TEST_ASSERT_EQUAL_UINT64 (op_id.low, record->operation_id.low);
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_TIMED_OUT, record->terminal_result);
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_claim_release (&infra_claim));
    void *infra_batch_handle = infra_batch;
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_receive_batch_destroy (&infra_batch_handle));

    //  Only now finish the application turn.
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_claim_release (&claim));

    //  Drain the pending order-3 request record left in the mailbox.
    zlink_mesh_claim_t tail_claim;
    take_ready_claim (node, ZLINK_MESH_OWNER_NODE, ZLINK_MESH_READY_APPLICATION, &tail_claim);
    recv_one_record (&tail_claim, recv_batch);
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_claim_release (&tail_claim));

    void *batch_handle = recv_batch;
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_receive_batch_destroy (&batch_handle));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_node_monitor_close (&monitor));
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK, zlink_mesh_node_shutdown (node, 1000));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_node_destroy (&node));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_ctx_term (ctx));
}

//  A rejected local Channel send registers one destination-specific waiter.
//  Moving the head record into a receive batch returns mailbox capacity and
//  emits one SEND_READY record on the source Node infrastructure claim.
void test_local_mailbox_capacity_emits_one_channel_send_ready ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    void *node = new_configured_node (ctx, "local-ready-mesh", "orders", 1);
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK, zlink_mesh_node_start (node));

    zlink_msg_t part;
    make_payload (&part, "order-1");
    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_OK,
      zlink_mesh_node_send_to_channel (
        node, "orders", NULL, &part, 1, ZLINK_SEND_FLAGS_DONTWAIT));
    zlink_msg_close (&part);

    make_payload (&part, "order-2");
    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_BACKPRESSURED,
      zlink_mesh_node_send_to_channel (
        node, "orders", NULL, &part, 1, ZLINK_SEND_FLAGS_DONTWAIT));
    TEST_ASSERT_EQUAL_INT (EAGAIN, zlink_errno ());
    zlink_msg_close (&part);

    zlink_mesh_claim_t application_claim;
    take_ready_claim (
      node, ZLINK_MESH_OWNER_NODE, ZLINK_MESH_READY_APPLICATION,
      &application_claim);
    void *application_batch = zlink_mesh_receive_batch_new (4, 8, 1 << 20);
    TEST_ASSERT_NOT_NULL (application_batch);
    const zlink_mesh_receive_record_t *application_record =
      recv_one_record (&application_claim, application_batch);
    TEST_ASSERT_EQUAL_INT (
      ZLINK_MESH_RECORD_CHANNEL_SEND, application_record->kind);

    zlink_mesh_claim_t infrastructure_claim;
    take_ready_claim (
      node, ZLINK_MESH_OWNER_NODE, ZLINK_MESH_READY_INFRASTRUCTURE,
      &infrastructure_claim);
    void *infrastructure_batch = zlink_mesh_receive_batch_new (4, 8, 1 << 20);
    TEST_ASSERT_NOT_NULL (infrastructure_batch);
    const zlink_mesh_receive_record_t *ready_record =
      recv_one_record (&infrastructure_claim, infrastructure_batch);
    TEST_ASSERT_EQUAL_INT (ZLINK_MESH_RECORD_SEND_READY, ready_record->kind);
    TEST_ASSERT_EQUAL_UINT (sizeof (zlink_mesh_send_ready_data_t),
                            ready_record->kind_data_size);
    const zlink_mesh_send_ready_data_t *ready =
      static_cast<const zlink_mesh_send_ready_data_t *> (ready_record->kind_data);
    TEST_ASSERT_EQUAL_INT (ZLINK_MESH_DESTINATION_CHANNEL,
                           ready->destination_kind);
    TEST_ASSERT_EQUAL_UINT (strlen ("orders"), ready->channel_name_size);
    TEST_ASSERT_EQUAL_MEMORY ("orders", ready->channel_name,
                              ready->channel_name_size);

    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK,
                           zlink_mesh_claim_release (&infrastructure_claim));

    void *duplicate_batch = zlink_mesh_ready_batch_new (4);
    TEST_ASSERT_NOT_NULL (duplicate_batch);
    uint32_t residue = 0;
    TEST_ASSERT_EQUAL_INT (
      ZLINK_RECV_NO_DATA,
      zlink_mesh_node_drain_ready (
        node, ZLINK_MESH_READY_INFRASTRUCTURE, duplicate_batch, &residue,
        ZLINK_RECV_FLAGS_DONTWAIT));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK,
                           zlink_mesh_ready_batch_destroy (&duplicate_batch));

    //  A successful retry may fill the mailbox again. A following EAGAIN
    //  registers a fresh interest, and the next dequeue emits exactly one new
    //  SEND_READY record.
    make_payload (&part, "order-2");
    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_OK,
      zlink_mesh_node_send_to_channel (
        node, "orders", NULL, &part, 1, ZLINK_SEND_FLAGS_DONTWAIT));
    zlink_msg_close (&part);
    make_payload (&part, "order-3");
    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_BACKPRESSURED,
      zlink_mesh_node_send_to_channel (
        node, "orders", NULL, &part, 1, ZLINK_SEND_FLAGS_DONTWAIT));
    TEST_ASSERT_EQUAL_INT (EAGAIN, zlink_errno ());
    zlink_msg_close (&part);

    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK,
                           zlink_mesh_claim_release (&application_claim));

    take_ready_claim (
      node, ZLINK_MESH_OWNER_NODE, ZLINK_MESH_READY_APPLICATION,
      &application_claim);
    application_record = recv_one_record (&application_claim, application_batch);
    TEST_ASSERT_EQUAL_INT (
      ZLINK_MESH_RECORD_CHANNEL_SEND, application_record->kind);
    take_ready_claim (
      node, ZLINK_MESH_OWNER_NODE, ZLINK_MESH_READY_INFRASTRUCTURE,
      &infrastructure_claim);
    ready_record = recv_one_record (&infrastructure_claim, infrastructure_batch);
    TEST_ASSERT_EQUAL_INT (ZLINK_MESH_RECORD_SEND_READY, ready_record->kind);
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK,
                           zlink_mesh_claim_release (&infrastructure_claim));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK,
                           zlink_mesh_claim_release (&application_claim));

    void *batch_handle = infrastructure_batch;
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CLOSE_OK, zlink_mesh_receive_batch_destroy (&batch_handle));
    batch_handle = application_batch;
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CLOSE_OK, zlink_mesh_receive_batch_destroy (&batch_handle));
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK,
                           zlink_mesh_node_shutdown (node, 1000));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_node_destroy (&node));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_ctx_term (ctx));
}

//  A SEND_READY record can invoke the registered ready handler before Core
//  returns from record admission. An EAGAIN retry made in that callback is a
//  new interest and must survive completion of the notification in progress.
void test_inline_ready_handler_retry_preserves_new_interest ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    void *node = new_configured_node (
      ctx, "inline-retry-mesh", "orders", 1);
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK, zlink_mesh_node_start (node));

    zlink_msg_t part;
    make_payload (&part, "A");
    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_OK,
      zlink_mesh_node_send_to_channel (
        node, "orders", NULL, &part, 1, ZLINK_SEND_FLAGS_DONTWAIT));
    zlink_msg_close (&part);
    make_payload (&part, "B");
    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_BACKPRESSURED,
      zlink_mesh_node_send_to_channel (
        node, "orders", NULL, &part, 1, ZLINK_SEND_FLAGS_DONTWAIT));
    TEST_ASSERT_EQUAL_INT (EAGAIN, zlink_errno ());
    zlink_msg_close (&part);

    zlink_mesh_claim_t application_claim;
    take_ready_claim (
      node, ZLINK_MESH_OWNER_NODE, ZLINK_MESH_READY_APPLICATION,
      &application_claim);

    inline_retry_state_t state;
    state.node = node;
    TEST_ASSERT_EQUAL_INT (
      ZLINK_HANDLER_OK,
      zlink_mesh_node_set_ready_handler (
        node, inline_retry_ready_handler, &state));

    void *application_batch = zlink_mesh_receive_batch_new (4, 8, 1024);
    TEST_ASSERT_NOT_NULL (application_batch);
    recv_one_record (&application_claim, application_batch);
    TEST_ASSERT_EQUAL_INT (1, state.callback_count);
    TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_OK, state.fill_result);
    TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_BACKPRESSURED, state.retry_result);
    TEST_ASSERT_EQUAL_INT (EAGAIN, state.retry_errno);
    TEST_ASSERT_EQUAL_INT (
      ZLINK_HANDLER_OK, zlink_mesh_node_set_ready_handler (node, NULL, NULL));

    //  Consume the first wakeup that caused the inline callback.
    zlink_mesh_claim_t infrastructure_claim;
    take_ready_claim (
      node, ZLINK_MESH_OWNER_NODE, ZLINK_MESH_READY_INFRASTRUCTURE,
      &infrastructure_claim);
    void *infrastructure_batch =
      zlink_mesh_receive_batch_new (4, 8, 64 * 1024);
    TEST_ASSERT_NOT_NULL (infrastructure_batch);
    const zlink_mesh_receive_record_t *ready_record =
      recv_one_record (&infrastructure_claim, infrastructure_batch);
    TEST_ASSERT_EQUAL_INT (ZLINK_MESH_RECORD_SEND_READY, ready_record->kind);
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CLOSE_OK, zlink_mesh_claim_release (&infrastructure_claim));

    //  The callback's filler still occupies the mailbox. Removing it is the
    //  next real recovery edge and must expose the retry interest exactly once.
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CLOSE_OK, zlink_mesh_claim_release (&application_claim));
    take_ready_claim (
      node, ZLINK_MESH_OWNER_NODE, ZLINK_MESH_READY_APPLICATION,
      &application_claim);
    recv_one_record (&application_claim, application_batch);
    take_ready_claim (
      node, ZLINK_MESH_OWNER_NODE, ZLINK_MESH_READY_INFRASTRUCTURE,
      &infrastructure_claim);
    ready_record = recv_one_record (&infrastructure_claim, infrastructure_batch);
    TEST_ASSERT_EQUAL_INT (ZLINK_MESH_RECORD_SEND_READY, ready_record->kind);
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CLOSE_OK, zlink_mesh_claim_release (&infrastructure_claim));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CLOSE_OK, zlink_mesh_claim_release (&application_claim));

    TEST_ASSERT_EQUAL_INT (
      ZLINK_CLOSE_OK,
      zlink_mesh_receive_batch_destroy (&infrastructure_batch));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CLOSE_OK, zlink_mesh_receive_batch_destroy (&application_batch));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_REQUEST_OK, zlink_mesh_node_shutdown (node, 1000));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_node_destroy (&node));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_ctx_term (ctx));
}

//  Byte budget recovery is independent of message-count recovery. With spare
//  message slots but no remaining bytes, dequeueing the head record must emit
//  one destination-specific SEND_READY record.
void test_local_byte_capacity_recovery_emits_send_ready ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    void *node = new_configured_node (
      ctx, "byte-ready-mesh", "orders", 8);
    const uint64_t byte_budget = 8;
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK,
      zlink_set_mesh_node_option (
        node, ZLINK_MESH_NODE_OPT_MAILBOX_BYTE_BUDGET, &byte_budget,
        sizeof (byte_budget)));
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK, zlink_mesh_node_start (node));

    zlink_msg_t part;
    make_payload (&part, "12345678");
    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_OK,
      zlink_mesh_node_send_to_channel (
        node, "orders", NULL, &part, 1, ZLINK_SEND_FLAGS_DONTWAIT));
    zlink_msg_close (&part);
    make_payload (&part, "x");
    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_BACKPRESSURED,
      zlink_mesh_node_send_to_channel (
        node, "orders", NULL, &part, 1, ZLINK_SEND_FLAGS_DONTWAIT));
    TEST_ASSERT_EQUAL_INT (EAGAIN, zlink_errno ());
    zlink_msg_close (&part);

    zlink_mesh_claim_t application_claim;
    take_ready_claim (
      node, ZLINK_MESH_OWNER_NODE, ZLINK_MESH_READY_APPLICATION,
      &application_claim);
    void *application_batch = zlink_mesh_receive_batch_new (4, 8, 1024);
    TEST_ASSERT_NOT_NULL (application_batch);
    recv_one_record (&application_claim, application_batch);

    zlink_mesh_claim_t infrastructure_claim;
    take_ready_claim (
      node, ZLINK_MESH_OWNER_NODE, ZLINK_MESH_READY_INFRASTRUCTURE,
      &infrastructure_claim);
    void *infrastructure_batch =
      zlink_mesh_receive_batch_new (4, 8, 64 * 1024);
    TEST_ASSERT_NOT_NULL (infrastructure_batch);
    const zlink_mesh_receive_record_t *ready_record =
      recv_one_record (&infrastructure_claim, infrastructure_batch);
    TEST_ASSERT_EQUAL_INT (ZLINK_MESH_RECORD_SEND_READY, ready_record->kind);
    TEST_ASSERT_EQUAL_UINT (sizeof (zlink_mesh_send_ready_data_t),
                            ready_record->kind_data_size);
    const zlink_mesh_send_ready_data_t *ready =
      static_cast<const zlink_mesh_send_ready_data_t *> (
        ready_record->kind_data);
    TEST_ASSERT_EQUAL_INT (
      ZLINK_MESH_DESTINATION_CHANNEL, ready->destination_kind);
    TEST_ASSERT_EQUAL_UINT (strlen ("orders"), ready->channel_name_size);
    TEST_ASSERT_EQUAL_MEMORY (
      "orders", ready->channel_name, ready->channel_name_size);

    TEST_ASSERT_EQUAL_INT (
      ZLINK_CLOSE_OK, zlink_mesh_claim_release (&infrastructure_claim));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CLOSE_OK, zlink_mesh_claim_release (&application_claim));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CLOSE_OK,
      zlink_mesh_receive_batch_destroy (&infrastructure_batch));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CLOSE_OK, zlink_mesh_receive_batch_destroy (&application_batch));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_REQUEST_OK, zlink_mesh_node_shutdown (node, 1000));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_node_destroy (&node));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_ctx_term (ctx));
}

//  Shutdown past its deadline revokes outstanding claims: the monitor
//  reports CLAIM_REVOKED, a revoked claim rejects recv with ESHUTDOWN while
//  release stays legal.
void test_shutdown_deadline_revokes_outstanding_claims ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    void *node = new_configured_node (ctx, "revoke-mesh", "jobs", 0);
    void *monitor = open_monitor (node, 0);
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK, zlink_mesh_node_start (node));

    zlink_msg_t part;
    make_payload (&part, "job-14");
    TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_OK,
                           zlink_mesh_node_send_to_channel (node, "jobs", NULL, &part, 1,
                                                            ZLINK_SEND_FLAGS_NONE));
    zlink_msg_close (&part);

    zlink_mesh_claim_t claim;
    take_ready_claim (node, ZLINK_MESH_OWNER_NODE, ZLINK_MESH_READY_APPLICATION, &claim);

    //  The held claim outlives the deadline: shutdown times out and revokes.
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_TIMED_OUT, zlink_mesh_node_shutdown (node, 150));
    TEST_ASSERT_EQUAL_INT (ETIMEDOUT, zlink_errno ());

    zlink_mesh_monitor_event_t event;
    wait_monitor_event (monitor, ZLINK_MESH_MONITOR_CLAIM_REVOKED, &event);
    TEST_ASSERT_EQUAL_INT (ZLINK_MESH_OWNER_NODE, event.owner_kind);

    //  recv on the revoked claim fails; release stays safe.
    void *recv_batch = zlink_mesh_receive_batch_new (4, 16, 1 << 20);
    TEST_ASSERT_NOT_NULL (recv_batch);
    zlink_mesh_receive_requirements_t required;
    memset (&required, 0, sizeof (required));
    required.struct_size = sizeof (required);
    required.version = 1;
    TEST_ASSERT_EQUAL_INT (ZLINK_RECV_INVALID_STATE,
                           zlink_mesh_claim_recv_batch (&claim, recv_batch, &required,
                                                        ZLINK_RECV_FLAGS_DONTWAIT));
    TEST_ASSERT_EQUAL_INT (ESHUTDOWN, zlink_errno ());
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_claim_release (&claim));

    void *batch_handle = recv_batch;
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_receive_batch_destroy (&batch_handle));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_node_monitor_close (&monitor));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_node_destroy (&node));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_ctx_term (ctx));
}

//  A reply token that survives into the stopped lifecycle fails with
//  ESHUTDOWN: after the requester timed out and the node shut down cleanly,
//  the one-shot token has no usable source route left.
void test_reply_after_stop_is_eshutdown ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    void *node = new_configured_node (ctx, "stop-mesh", "mail", 0);
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK, zlink_mesh_node_start (node));

    zlink_msg_t part;
    make_payload (&part, "mail-8");
    zlink_mesh_operation_id_t op_id;
    TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_OK,
                           zlink_mesh_node_request_to_channel (node, "mail", NULL, &part, 1,
                                                               &op_id, ZLINK_SEND_FLAGS_NONE,
                                                               100));
    zlink_msg_close (&part);

    //  Wait out the requester timeout, then drain its terminal completion.
    zlink_mesh_claim_t infra_claim;
    take_ready_claim (node, ZLINK_MESH_OWNER_NODE, ZLINK_MESH_READY_INFRASTRUCTURE, &infra_claim);
    void *recv_batch = zlink_mesh_receive_batch_new (8, 32, 1 << 20);
    TEST_ASSERT_NOT_NULL (recv_batch);
    const zlink_mesh_receive_record_t *record = recv_one_record (&infra_claim, recv_batch);
    TEST_ASSERT_EQUAL_INT (ZLINK_MESH_RECORD_COMPLETION, record->kind);
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_TIMED_OUT, record->terminal_result);
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_claim_release (&infra_claim));

    //  Take the request record and keep its token past the claim turn.
    zlink_mesh_claim_t claim;
    take_ready_claim (node, ZLINK_MESH_OWNER_NODE, ZLINK_MESH_READY_APPLICATION, &claim);
    record = recv_one_record (&claim, recv_batch);
    TEST_ASSERT_EQUAL_INT (ZLINK_MESH_RECORD_CHANNEL_REQUEST, record->kind);
    zlink_mesh_reply_token_t token = record->reply_token;
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_claim_release (&claim));

    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK, zlink_mesh_node_shutdown (node, 2000));

    zlink_msg_t reply_part;
    make_payload (&reply_part, "late-ack");
    TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_INVALID_STATE,
                           zlink_mesh_reply (&token, &reply_part, 1, ZLINK_SEND_FLAGS_NONE));
    TEST_ASSERT_EQUAL_INT (ESHUTDOWN, zlink_errno ());
    zlink_msg_close (&reply_part);

    void *batch_handle = recv_batch;
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_receive_batch_destroy (&batch_handle));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_node_destroy (&node));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_ctx_term (ctx));
}

//  Duplicate manual intents for one endpoint merge into a single intent, an
//  un-admitted intent is removed by id, and unknown ids / wrong generations
//  report their contract errors.
void test_peer_intent_merge_remove_and_disconnect_errors ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    void *node = new_configured_node (ctx, "intent-mesh", "sync", 0);
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK, zlink_mesh_node_start (node));

    //  Nothing listens on the endpoint: the intent stays un-admitted.
    zlink_mesh_peer_connection_options_t options;
    memset (&options, 0, sizeof (options));
    options.struct_size = sizeof (options);
    options.version = 1;
    const char endpoint[] = "tcp://127.0.0.1:47611";
    options.endpoint = endpoint;
    options.endpoint_size = strlen (endpoint);
    uint64_t intent_a = 0;
    TEST_ASSERT_EQUAL_INT (ZLINK_CONNECT_OK,
                           zlink_mesh_node_connect_peer (node, &options, &intent_a));
    TEST_ASSERT_TRUE (intent_a != 0);

    //  The same endpoint merges: one intent, same id.
    uint64_t intent_b = 0;
    TEST_ASSERT_EQUAL_INT (ZLINK_CONNECT_OK,
                           zlink_mesh_node_connect_peer (node, &options, &intent_b));
    TEST_ASSERT_EQUAL_UINT64 (intent_a, intent_b);
    zlink_mesh_node_status_t status;
    memset (&status, 0, sizeof (status));
    status.struct_size = sizeof (status);
    status.version = 1;
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK, zlink_mesh_node_status (node, &status));
    TEST_ASSERT_EQUAL_UINT32 (1, status.configured_peer_count);

    //  The un-admitted intent is removed by id; a second removal misses.
    TEST_ASSERT_EQUAL_INT (ZLINK_CONNECT_OK,
                           zlink_mesh_node_remove_peer_connection (node, intent_a));
    TEST_ASSERT_EQUAL_INT (ZLINK_CONNECT_NOT_FOUND,
                           zlink_mesh_node_remove_peer_connection (node, intent_a));
    TEST_ASSERT_EQUAL_INT (ENOENT, zlink_errno ());

    //  disconnect_peer targets admitted peers only: unknown RID reports
    //  NOT_FOUND, an empty RID is invalid.
    zlink_routing_id_t rid;
    memset (&rid, 0, sizeof (rid));
    TEST_ASSERT_EQUAL_INT (ZLINK_CONNECT_INVALID_ARGUMENT,
                           zlink_mesh_node_disconnect_peer (node, &rid, 1));
    TEST_ASSERT_EQUAL_INT (EINVAL, zlink_errno ());
    rid.size = 6;
    memcpy (rid.data, "ghost1", 6);
    TEST_ASSERT_EQUAL_INT (ZLINK_CONNECT_NOT_FOUND,
                           zlink_mesh_node_disconnect_peer (node, &rid, 1));
    TEST_ASSERT_EQUAL_INT (ENOENT, zlink_errno ());

    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK, zlink_mesh_node_shutdown (node, 1000));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_node_destroy (&node));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_ctx_term (ctx));
}

int main ()
{
    setup_test_environment (120);

    UNITY_BEGIN ();
    RUN_TEST (test_raw_handshake_failure_emits_peer_rejected);
    RUN_TEST (test_monitor_event_matrix_and_child_reference);
    RUN_TEST (test_monitor_event_mask_filters_kinds);
    RUN_TEST (test_monitor_counters_cover_lifecycle_before_open_and_after_reopen);
    RUN_TEST (test_backpressure_event_and_in_turn_infrastructure_progress);
    RUN_TEST (test_local_mailbox_capacity_emits_one_channel_send_ready);
    RUN_TEST (test_inline_ready_handler_retry_preserves_new_interest);
    RUN_TEST (test_local_byte_capacity_recovery_emits_send_ready);
    RUN_TEST (test_shutdown_deadline_revokes_outstanding_claims);
    RUN_TEST (test_peer_intent_merge_remove_and_disconnect_errors);
    RUN_TEST (test_reply_after_stop_is_eshutdown);
    const int rc = UNITY_END ();
    fflush (NULL);
    return rc;
}
