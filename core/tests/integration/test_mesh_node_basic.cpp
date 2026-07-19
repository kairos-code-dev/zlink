/* SPDX-License-Identifier: MPL-2.0 */

//  Single-node MeshNode contract coverage: lifecycle gates, local channel
//  selection, the claim/batch receive path, one-shot replies, Logical
//  Multicast with local subscriptions and the Actor control lane.

#include "../testutil_unity.hpp"

#include <string.h>
#include <atomic>
#include <thread>

#if !defined(ZLINK_HAVE_WINDOWS)
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

SETUP_TEARDOWN_TESTCONTEXT

extern "C" void zlink_test_set_mesh_alloc_fault (int count_);
extern "C" void zlink_test_set_mesh_publish_pause_after_snapshot_ms (int pause_ms_);
extern "C" int zlink_test_mesh_publish_snapshot_paused ();
extern "C" void zlink_test_stream_session_pause_before_local_actor_admit (int pause_);
extern "C" int zlink_test_stream_session_local_actor_admit_paused ();
extern "C" void zlink_test_stream_session_fence_actor (
  void *service_, const zlink_actor_ref_t *actor_, uint64_t transfer_serial_);
extern "C" void zlink_test_stream_session_abort_actor (
  void *service_, const zlink_actor_ref_t *actor_, uint64_t transfer_serial_);

namespace
{
void *new_started_node (void *ctx_,
                        const char *name_,
                        const char *channel_,
                        uint64_t mailbox_message_budget_ = 0)
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
    if (mailbox_message_budget_ != 0) {
        TEST_ASSERT_EQUAL_INT (
          ZLINK_CONFIG_OK,
          zlink_set_mesh_node_option (node, ZLINK_MESH_NODE_OPT_MAILBOX_MESSAGE_BUDGET,
                                      &mailbox_message_budget_,
                                      sizeof (mailbox_message_budget_)));
    }
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

#if !defined(ZLINK_HAVE_WINDOWS)
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
#endif

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

void test_mesh_node_common_timeout_validation ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    zlink_mesh_node_options_t options;
    memset (&options, 0, sizeof (options));
    options.struct_size = sizeof (options);
    options.version = 1;
    options.mesh_name = "timeout-validation-mesh";
    options.mesh_name_size = strlen (options.mesh_name);
    void *node = zlink_mesh_node_new (ctx, &options);
    TEST_ASSERT_NOT_NULL (node);

    const int invalid_timeout = -2;
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_INVALID_ARGUMENT,
      zlink_set_option (node, ZLINK_OPT_SNDTIMEO, &invalid_timeout,
                        sizeof (invalid_timeout)));
    TEST_ASSERT_EQUAL_INT (EINVAL, zlink_errno ());
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_INVALID_ARGUMENT,
      zlink_set_option (node, ZLINK_OPT_RCVTIMEO, &invalid_timeout,
                        sizeof (invalid_timeout)));
    TEST_ASSERT_EQUAL_INT (EINVAL, zlink_errno ());

    int timeout = 0;
    size_t timeout_size = sizeof (timeout);
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK,
      zlink_get_option (node, ZLINK_OPT_SNDTIMEO, &timeout, &timeout_size));
    TEST_ASSERT_EQUAL_INT (1000, timeout);
    timeout_size = sizeof (timeout);
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK,
      zlink_get_option (node, ZLINK_OPT_RCVTIMEO, &timeout, &timeout_size));
    TEST_ASSERT_EQUAL_INT (1000, timeout);

    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_node_destroy (&node));
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
    //  Payload preparation failure and terminal-mailbox admission failure
    //  both leave the token and operation retryable.
    zlink_test_set_mesh_alloc_fault (1);
    TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_OUT_OF_MEMORY,
                           zlink_mesh_reply (&record->reply_token, &reply_part, 1,
                                             ZLINK_SEND_FLAGS_NONE));
    TEST_ASSERT_EQUAL_INT (ENOMEM, zlink_errno ());
    zlink_test_set_mesh_alloc_fault (2);
    TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_OUT_OF_MEMORY,
                           zlink_mesh_reply (&record->reply_token, &reply_part, 1,
                                             ZLINK_SEND_FLAGS_NONE));
    TEST_ASSERT_EQUAL_INT (ENOMEM, zlink_errno ());
    zlink_test_set_mesh_alloc_fault (0);
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

void test_mesh_metadata_ownership_and_timeout_contract ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    void *node = new_started_node (ctx, "metadata-mesh", "events");

    //  Canonical metadata contains one UTF-8 key/value entry. The submit call
    //  must retain both metadata and the borrowed payload before it returns.
    uint8_t metadata_bytes[] = {1, 1, 3, 'k', 'e', 'y', 0, 5, 'v', 'a', 'l', 'u', 'e'};
    zlink_mesh_metadata_view_t metadata = {metadata_bytes, sizeof (metadata_bytes)};
    zlink_msg_t part;
    make_payload (&part, "original-payload");
    TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_OK,
                           zlink_mesh_node_send_to_channel (
                             node, "events", &metadata, &part, 1, ZLINK_SEND_FLAGS_NONE));
    memset (metadata_bytes, 0xff, sizeof (metadata_bytes));
    memset (zlink_msg_data (&part), 'x', zlink_msg_size (&part));
    zlink_msg_close (&part);

    zlink_mesh_claim_t claim;
    take_ready_claim (node, ZLINK_MESH_OWNER_NODE, ZLINK_MESH_READY_APPLICATION, &claim);
    void *recv_batch = zlink_mesh_receive_batch_new (4, 8, 1 << 20);
    TEST_ASSERT_NOT_NULL (recv_batch);
    zlink_mesh_receive_requirements_t required;
    memset (&required, 0, sizeof (required));
    required.struct_size = sizeof (required);
    required.version = 1;
    TEST_ASSERT_EQUAL_INT (ZLINK_RECV_OK,
                           zlink_mesh_claim_recv_batch (&claim, recv_batch, &required,
                                                        ZLINK_RECV_FLAGS_NONE));
    TEST_ASSERT_EQUAL_UINT64 (1, zlink_mesh_receive_batch_count (recv_batch));
    const zlink_mesh_receive_record_t *record = zlink_mesh_receive_batch_data (recv_batch);
    const uint8_t expected_metadata[] = {1, 1, 3, 'k', 'e', 'y', 0, 5,
                                         'v', 'a', 'l', 'u', 'e'};
    TEST_ASSERT_EQUAL_UINT64 (sizeof (expected_metadata), record->application_metadata_size);
    TEST_ASSERT_EQUAL_MEMORY (expected_metadata, record->application_metadata,
                              sizeof (expected_metadata));
    const zlink_msg_t *received_parts = zlink_mesh_receive_batch_parts (recv_batch);
    TEST_ASSERT_EQUAL_MEMORY (
      "original-payload",
      zlink_msg_data (const_cast<zlink_msg_t *> (&received_parts[record->part_offset])), 16);

    //  Retain reports the required capacity without initializing output, then
    //  returns a reference that remains valid after the receive batch resets.
    size_t retained_count = 0;
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_BUFFER_TOO_SMALL,
      zlink_mesh_receive_batch_retain_message (recv_batch, 0, NULL, &retained_count));
    TEST_ASSERT_EQUAL_UINT64 (1, retained_count);
    TEST_ASSERT_EQUAL_INT (ENOBUFS, zlink_errno ());
    zlink_msg_t retained[1];
    retained_count = 1;
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK,
      zlink_mesh_receive_batch_retain_message (recv_batch, 0, retained, &retained_count));
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK, zlink_mesh_receive_batch_reset (recv_batch));
    TEST_ASSERT_EQUAL_MEMORY ("original-payload", zlink_msg_data (&retained[0]), 16);
    zlink_msg_close (&retained[0]);
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_claim_release (&claim));

    //  Every malformed outbound metadata shape is rejected before admission.
    make_payload (&part, "invalid-metadata");
    uint8_t wrong_version[] = {2, 0};
    uint8_t missing_entry[] = {1, 1};
    uint8_t empty_key[] = {1, 1, 0, 0, 0};
    uint8_t duplicate_key[] = {1, 2, 1, 'a', 0, 0, 1, 'a', 0, 0};
    uint8_t trailing_byte[] = {1, 0, 0};
    uint8_t invalid_utf8[] = {1, 1, 1, 0xff, 0, 0};
    uint8_t oversized[ZLINK_MESH_APPLICATION_METADATA_MAX + 1] = {1, 0};
    const zlink_mesh_metadata_view_t invalid_metadata[] = {
      {NULL, 2},
      {wrong_version, sizeof (wrong_version)},
      {missing_entry, sizeof (missing_entry)},
      {empty_key, sizeof (empty_key)},
      {duplicate_key, sizeof (duplicate_key)},
      {trailing_byte, sizeof (trailing_byte)},
      {invalid_utf8, sizeof (invalid_utf8)},
      {oversized, sizeof (oversized)}};
    for (size_t i = 0; i < sizeof (invalid_metadata) / sizeof (invalid_metadata[0]); ++i) {
        TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_INVALID_ARGUMENT,
                               zlink_mesh_node_send_to_channel (
                                 node, "events", &invalid_metadata[i], &part, 1,
                                 ZLINK_SEND_FLAGS_NONE));
        TEST_ASSERT_EQUAL_INT (EINVAL, zlink_errno ());
    }
    zlink_msg_close (&part);

    //  A request without a reply produces one timeout completion with the
    //  original operation ID and canonical errno.
    make_payload (&part, "will-time-out");
    zlink_mesh_operation_id_t operation_id;
    TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_OK,
                           zlink_mesh_node_request_to_channel (
                             node, "events", NULL, &part, 1, &operation_id,
                             ZLINK_SEND_FLAGS_NONE, 20));
    zlink_msg_close (&part);
    take_ready_claim (node, ZLINK_MESH_OWNER_NODE, ZLINK_MESH_READY_APPLICATION, &claim);
    TEST_ASSERT_EQUAL_INT (ZLINK_RECV_OK,
                           zlink_mesh_claim_recv_batch (&claim, recv_batch, &required,
                                                        ZLINK_RECV_FLAGS_NONE));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_claim_release (&claim));

    zlink_mesh_claim_t infra_claim;
    take_ready_claim (node, ZLINK_MESH_OWNER_NODE, ZLINK_MESH_READY_INFRASTRUCTURE,
                      &infra_claim);
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK, zlink_mesh_receive_batch_reset (recv_batch));
    TEST_ASSERT_EQUAL_INT (ZLINK_RECV_OK,
                           zlink_mesh_claim_recv_batch (&infra_claim, recv_batch, &required,
                                                        ZLINK_RECV_FLAGS_NONE));
    TEST_ASSERT_EQUAL_UINT64 (1, zlink_mesh_receive_batch_count (recv_batch));
    record = zlink_mesh_receive_batch_data (recv_batch);
    TEST_ASSERT_EQUAL_INT (ZLINK_MESH_RECORD_COMPLETION, record->kind);
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_TIMED_OUT, record->terminal_result);
    TEST_ASSERT_EQUAL_INT (ETIMEDOUT, record->failure_errno);
    TEST_ASSERT_EQUAL_UINT64 (operation_id.low, record->operation_id.low);
    TEST_ASSERT_EQUAL_UINT64 (operation_id.high, record->operation_id.high);
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
    //  exactly once with aggregate target accounting.
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

//  A full local mailbox drops only that target. Another subscribed Spot still
//  receives the same multicast without an all-target reservation or retry.
void test_mesh_local_multicast_target_specific_drop ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    void *node = new_started_node (ctx, "local-drop-mesh", "events", 1);

    const char *spot_names[2] = {"full-spot", "free-spot"};
    void *spots[2] = {NULL, NULL};
    zlink_routing_id_t spot_rids[2];
    for (size_t i = 0; i < 2; ++i) {
        memset (&spot_rids[i], 0, sizeof (spot_rids[i]));
        spot_rids[i].size = static_cast<uint8_t> (strlen (spot_names[i]));
        memcpy (spot_rids[i].data, spot_names[i], spot_rids[i].size);
        uint32_t created = 0;
        TEST_ASSERT_EQUAL_INT (
          ZLINK_CONFIG_OK,
          zlink_mesh_node_spot_get_or_new (node, &spot_rids[i], &spots[i], &created));
        TEST_ASSERT_EQUAL_UINT32 (1, created);
        TEST_ASSERT_EQUAL_INT (
          ZLINK_CONFIG_OK,
          zlink_spot_set_subscription (spots[i], "events", "broadcast.",
                                       ZLINK_SPOT_SUBSCRIPTION_PREFIX));
    }
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK,
      zlink_spot_set_subscription (spots[0], "events", "fill.",
                                   ZLINK_SPOT_SUBSCRIPTION_PREFIX));

    void *publisher = zlink_mesh_node_publisher_new (node);
    TEST_ASSERT_NOT_NULL (publisher);
    zlink_mesh_publish_detail_t detail;
    memset (&detail, 0, sizeof (detail));
    detail.struct_size = sizeof (detail);
    detail.version = 1;

    zlink_msg_t part;
    make_payload (&part, "first");
    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_OK,
      zlink_mesh_node_publisher_publish (publisher, "events", "fill.one", NULL, &part, 1,
                                         &detail, ZLINK_SEND_FLAGS_DONTWAIT));
    zlink_msg_close (&part);
    TEST_ASSERT_EQUAL_UINT32 (1, detail.admitted_local_spot_count);

    make_payload (&part, "second");
    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_OK,
      zlink_mesh_node_publisher_publish (publisher, "events", "broadcast.one", NULL, &part,
                                         1, &detail, ZLINK_SEND_FLAGS_DONTWAIT));
    zlink_msg_close (&part);
    TEST_ASSERT_EQUAL_UINT32 (2, detail.snapshot_local_spot_count);
    TEST_ASSERT_EQUAL_UINT32 (1, detail.admitted_local_spot_count);
    TEST_ASSERT_EQUAL_UINT32 (1, detail.dropped_local_spot_count);

    void *ready = zlink_mesh_ready_batch_new (4);
    TEST_ASSERT_NOT_NULL (ready);
    uint32_t residue = 0;
    TEST_ASSERT_EQUAL_INT (
      ZLINK_RECV_OK,
      zlink_mesh_node_drain_ready (node, ZLINK_MESH_READY_APPLICATION, ready, &residue,
                                   ZLINK_RECV_FLAGS_NONE));
    const zlink_mesh_ready_record_t *ready_records = zlink_mesh_ready_batch_data (ready);
    const size_t ready_count = zlink_mesh_ready_batch_count (ready);
    zlink_mesh_claim_t free_claim;
    bool free_claim_taken = false;
    for (size_t i = 0; i < ready_count; ++i) {
        if (ready_records[i].owner_kind == ZLINK_MESH_OWNER_SPOT
            && ready_records[i].spot_rid.size == spot_rids[1].size
            && memcmp (ready_records[i].spot_rid.data, spot_rids[1].data,
                       spot_rids[1].size)
                 == 0) {
            TEST_ASSERT_EQUAL_INT (
              ZLINK_CONFIG_OK,
              zlink_mesh_ready_batch_take_claim (ready, i, &free_claim));
            free_claim_taken = true;
            break;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE (free_claim_taken, "free Spot was not made ready");

    void *recv_batch = zlink_mesh_receive_batch_new (2, 4, 1024);
    TEST_ASSERT_NOT_NULL (recv_batch);
    zlink_mesh_receive_requirements_t required;
    memset (&required, 0, sizeof (required));
    required.struct_size = sizeof (required);
    required.version = 1;
    TEST_ASSERT_EQUAL_INT (
      ZLINK_RECV_OK,
      zlink_mesh_claim_recv_batch (&free_claim, recv_batch, &required,
                                   ZLINK_RECV_FLAGS_NONE));
    TEST_ASSERT_EQUAL_UINT64 (1, zlink_mesh_receive_batch_count (recv_batch));
    const zlink_mesh_receive_record_t *record = zlink_mesh_receive_batch_data (recv_batch);
    TEST_ASSERT_EQUAL_STRING_LEN ("broadcast.one", record->topic, record->topic_size);
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_claim_release (&free_claim));

    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_receive_batch_destroy (&recv_batch));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_ready_batch_destroy (&ready));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_node_publisher_destroy (&publisher));
    for (size_t i = 0; i < 2; ++i)
        TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_spot_destroy (&spots[i]));
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK, zlink_mesh_node_shutdown (node, 1000));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_node_destroy (&node));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_ctx_term (ctx));
}

//  A publish that selected its local targets before shutdown must not admit a
//  record after the node has transitioned to DRAINING/STOPPED.
void test_mesh_publish_snapshot_cannot_cross_shutdown ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    void *node = new_started_node (ctx, "publish-shutdown-mesh", "events");

    zlink_routing_id_t spot_rid;
    memset (&spot_rid, 0, sizeof (spot_rid));
    spot_rid.size = 9;
    memcpy (spot_rid.data, "collector", 9);
    void *spot = NULL;
    uint32_t created = 0;
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK,
      zlink_mesh_node_spot_get_or_new (node, &spot_rid, &spot, &created));
    TEST_ASSERT_EQUAL_UINT32 (1, created);
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK,
      zlink_spot_set_subscription (spot, "events", "shutdown.",
                                   ZLINK_SPOT_SUBSCRIPTION_PREFIX));

    void *publisher = zlink_mesh_node_publisher_new (node);
    TEST_ASSERT_NOT_NULL (publisher);
    zlink_msg_t part;
    make_payload (&part, "must-not-be-admitted");
    zlink_mesh_publish_detail_t detail;
    memset (&detail, 0, sizeof (detail));
    detail.struct_size = sizeof (detail);
    detail.version = 1;
    std::atomic<int> publish_rc (ZLINK_SUBMIT_INTERNAL_ERROR);
    std::atomic<int> publish_errno (0);

    zlink_test_set_mesh_publish_pause_after_snapshot_ms (500);
    std::thread publisher_thread ([&] {
        publish_rc.store (
          zlink_mesh_node_publisher_publish (publisher, "events", "shutdown.now", NULL,
                                             &part, 1, &detail,
                                             ZLINK_SEND_FLAGS_DONTWAIT),
          std::memory_order_release);
        publish_errno.store (errno, std::memory_order_release);
    });
    bool snapshot_paused = false;
    for (int attempt = 0; attempt < 100; ++attempt) {
        if (zlink_test_mesh_publish_snapshot_paused ()) {
            snapshot_paused = true;
            break;
        }
        msleep (5);
    }
    TEST_ASSERT_TRUE_MESSAGE (snapshot_paused, "publish did not reach the snapshot barrier");
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK, zlink_mesh_node_shutdown (node, 1000));
    publisher_thread.join ();

    TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_INVALID_STATE,
                           publish_rc.load (std::memory_order_acquire));
    TEST_ASSERT_EQUAL_INT (ESHUTDOWN, publish_errno.load (std::memory_order_acquire));
    TEST_ASSERT_EQUAL_UINT32 (1, detail.snapshot_local_spot_count);
    TEST_ASSERT_EQUAL_UINT32 (0, detail.admitted_local_spot_count);
    TEST_ASSERT_EQUAL_UINT32 (1, detail.dropped_local_spot_count);

    zlink_msg_close (&part);
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_node_publisher_destroy (&publisher));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_spot_destroy (&spot));
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

void test_stream_session_binding_reports_actor_membership_epoch ()
{
#if defined(ZLINK_HAVE_WINDOWS)
    TEST_IGNORE_MESSAGE ("raw TCP test helper is unavailable on Windows");
#else
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    void *node = new_started_node (ctx, "session-mesh", "sessions");
    void *stream = zlink_socket (ctx, ZLINK_SOCKET_STREAM);
    TEST_ASSERT_NOT_NULL (stream);
    void *service = zlink_stream_session_service_new (node, stream);
    TEST_ASSERT_NOT_NULL (service);
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_INVALID_STATE,
                           zlink_stream_session_service_start (service));
    TEST_ASSERT_EQUAL_INT (EINVAL, zlink_errno ());
    const int notify = 1;
    const int recv_timeout_ms = 5000;
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK,
      zlink_set_stream_option (stream, ZLINK_STREAM_OPT_NOTIFY, &notify, sizeof (notify)));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK,
      zlink_set_option (stream, ZLINK_OPT_RCVTIMEO, &recv_timeout_ms,
                        sizeof (recv_timeout_ms)));
    TEST_ASSERT_EQUAL_INT (ZLINK_BIND_OK, zlink_bind (stream, "tcp://127.0.0.1:0"));
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK, zlink_stream_session_service_start (service));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_BUSY, zlink_close (stream));
    TEST_ASSERT_EQUAL_INT (EBUSY, zlink_errno ());

    zlink_actor_ref_t actor;
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK,
                           zlink_mesh_node_actor_new (node, "player-session", NULL, 0, &actor,
                                                      ZLINK_SEND_FLAGS_NONE, 1000));

    zlink_routing_id_t absent_session;
    memset (&absent_session, 0, sizeof (absent_session));
    absent_session.size = 4;
    memcpy (absent_session.data, "none", absent_session.size);
    zlink_mesh_operation_id_t absent_operation;
    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_NOT_CONNECTED,
      zlink_stream_session_bind_actor (service, &absent_session, &actor, &absent_operation, 1000));
    TEST_ASSERT_EQUAL_INT (ENOTCONN, zlink_errno ());

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
    zlink_mesh_operation_id_t operation_id;
    TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_OK,
                           zlink_stream_session_bind_actor (service, &session_rid, &actor,
                                                            &operation_id, 1000));

    zlink_stream_session_binding_t binding;
    memset (&binding, 0, sizeof (binding));
    binding.struct_size = sizeof (binding);
    binding.version = ZLINK_STREAM_SESSION_ABI_VERSION;
    size_t count = 1;
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK,
                           zlink_stream_session_bindings (service, &session_rid, &binding,
                                                          &count));
    TEST_ASSERT_EQUAL_UINT64 (1, count);
    TEST_ASSERT_EQUAL_UINT64 (1, binding.membership_epoch);
    zlink_stream_session_status_t status;
    memset (&status, 0, sizeof (status));
    status.struct_size = sizeof (status);
    status.version = ZLINK_STREAM_SESSION_ABI_VERSION;
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK,
                           zlink_stream_session_service_status (service, &status));
    TEST_ASSERT_EQUAL_UINT64 (1, status.session_count);

    TEST_ASSERT_EQUAL_INT (0, shutdown (client_fd, SHUT_RDWR));
    close (client_fd);
    TEST_ASSERT_EQUAL_INT (ZLINK_CONNECT_OK, zlink_disconnect_rid (stream, &session_rid));
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK,
                           zlink_stream_session_service_status (service, &status));
    TEST_ASSERT_EQUAL_UINT64 (0, status.session_count);
    TEST_ASSERT_EQUAL_UINT64 (0, status.binding_count);

    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK,
                           zlink_stream_session_service_shutdown (service, 1000));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_stream_session_service_destroy (&service));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_close (stream));
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK, zlink_mesh_node_shutdown (node, 1000));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_node_destroy (&node));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_ctx_term (ctx));
#endif
}

void test_stream_session_actor_send_is_one_complete_byte_message ()
{
#if defined(ZLINK_HAVE_WINDOWS)
    TEST_IGNORE_MESSAGE ("raw TCP test helper is unavailable on Windows");
#else
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    void *node = new_started_node (ctx, "session-send-mesh", "sessions");
    void *stream = zlink_socket (ctx, ZLINK_SOCKET_STREAM);
    TEST_ASSERT_NOT_NULL (stream);
    const int notify = 1;
    const int recv_timeout_ms = 5000;
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK,
      zlink_set_stream_option (stream, ZLINK_STREAM_OPT_NOTIFY, &notify, sizeof (notify)));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK,
      zlink_set_option (stream, ZLINK_OPT_RCVTIMEO, &recv_timeout_ms,
                        sizeof (recv_timeout_ms)));
    TEST_ASSERT_EQUAL_INT (ZLINK_BIND_OK, zlink_bind (stream, "tcp://127.0.0.1:0"));
    char endpoint[ZLINK_MESH_ENDPOINT_MAX + 1];
    size_t endpoint_size = sizeof (endpoint);
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK,
      zlink_get_option (stream, ZLINK_OPT_LAST_ENDPOINT, endpoint, &endpoint_size));
    endpoint[sizeof (endpoint) - 1] = '\0';

    void *service = zlink_stream_session_service_new (node, stream);
    TEST_ASSERT_NOT_NULL (service);
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK, zlink_stream_session_service_start (service));
    const int client_fd = connect_raw_stream_client (endpoint);
    TEST_ASSERT_TRUE (client_fd >= 0);
    TEST_ASSERT_EQUAL_INT (1, send (client_fd, "!", 1, 0));

    zlink_msg_t connect_part;
    TEST_ASSERT_EQUAL_INT (0, zlink_msg_init (&connect_part));
    const zlink_routing_id_t *source_rid = NULL;
    zlink_part_flag_t has_more = ZLINK_PART_MORE;
    TEST_ASSERT_EQUAL_INT (ZLINK_RECV_OK,
                           zlink_recv_part (stream, &source_rid, &connect_part, &has_more,
                                            ZLINK_RECV_FLAGS_NONE));
    TEST_ASSERT_NOT_NULL (source_rid);
    TEST_ASSERT_TRUE (zlink_msg_size (&connect_part) <= 1);
    TEST_ASSERT_EQUAL_INT (ZLINK_PART_FINAL, has_more);
    zlink_routing_id_t session_rid;
    memset (&session_rid, 0, sizeof (session_rid));
    TEST_ASSERT_TRUE (source_rid->size > 0);
    TEST_ASSERT_TRUE (source_rid->size <= sizeof (session_rid.data));
    session_rid = *source_rid;
    zlink_msg_close (&connect_part);

    zlink_actor_ref_t actor;
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK,
                           zlink_mesh_node_actor_new (node, "stream-writer", NULL, 0, &actor,
                                                      ZLINK_SEND_FLAGS_NONE, 1000));
    zlink_mesh_operation_id_t operation_id;
    TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_OK,
                           zlink_stream_session_bind_actor (service, &session_rid, &actor,
                                                            &operation_id, 1000));

    //  The session binding check and local actor admission form one FIFO
    //  operation. A later submit must not enter the actor mailbox while an
    //  earlier submit is between those two steps.
    zlink_msg_t first_to_actor;
    zlink_msg_t second_to_actor;
    make_payload (&first_to_actor, "S1");
    make_payload (&second_to_actor, "S2");
    std::atomic<zlink_submit_result_t> first_submit (ZLINK_SUBMIT_INTERNAL_ERROR);
    std::atomic<zlink_submit_result_t> second_submit (ZLINK_SUBMIT_INTERNAL_ERROR);
    zlink_test_stream_session_pause_before_local_actor_admit (1);
    std::thread first_sender ([&] {
        first_submit.store (
          zlink_stream_session_send_to_actor (service, &session_rid, &actor, NULL,
                                              &first_to_actor, 1, ZLINK_SEND_FLAGS_NONE),
          std::memory_order_release);
    });
    bool first_paused = false;
    for (int attempt = 0; attempt < 100 && !first_paused; ++attempt) {
        first_paused = zlink_test_stream_session_local_actor_admit_paused () != 0;
        if (!first_paused)
            msleep (10);
    }
    TEST_ASSERT_TRUE_MESSAGE (first_paused,
                              "first session submit did not reach the admission barrier");
    std::thread second_sender ([&] {
        second_submit.store (
          zlink_stream_session_send_to_actor (service, &session_rid, &actor, NULL,
                                              &second_to_actor, 1, ZLINK_SEND_FLAGS_NONE),
          std::memory_order_release);
    });
    msleep (50);
    zlink_test_stream_session_pause_before_local_actor_admit (0);
    first_sender.join ();
    second_sender.join ();
    TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_OK,
                           first_submit.load (std::memory_order_acquire));
    TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_OK,
                           second_submit.load (std::memory_order_acquire));
    zlink_msg_close (&first_to_actor);
    zlink_msg_close (&second_to_actor);

    zlink_mesh_claim_t actor_claim;
    take_ready_claim (node, ZLINK_MESH_OWNER_ACTOR, ZLINK_MESH_READY_APPLICATION,
                      &actor_claim);
    void *actor_batch = zlink_mesh_receive_batch_new (2, 2, 16);
    TEST_ASSERT_NOT_NULL (actor_batch);
    zlink_mesh_receive_requirements_t actor_required;
    memset (&actor_required, 0, sizeof (actor_required));
    actor_required.struct_size = sizeof (actor_required);
    actor_required.version = 1;
    TEST_ASSERT_EQUAL_INT (
      ZLINK_RECV_OK,
      zlink_mesh_claim_recv_batch (&actor_claim, actor_batch, &actor_required,
                                   ZLINK_RECV_FLAGS_NONE));
    TEST_ASSERT_EQUAL_UINT64 (2, zlink_mesh_receive_batch_count (actor_batch));
    const zlink_msg_t *actor_parts = zlink_mesh_receive_batch_parts (actor_batch);
    TEST_ASSERT_EQUAL_MEMORY (
      "S1", zlink_msg_data (const_cast<zlink_msg_t *> (&actor_parts[0])), 2);
    TEST_ASSERT_EQUAL_MEMORY (
      "S2", zlink_msg_data (const_cast<zlink_msg_t *> (&actor_parts[1])), 2);
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_claim_release (&actor_claim));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK,
                           zlink_mesh_receive_batch_destroy (&actor_batch));

    //  Ordering is scoped to one session. While the first session is paused
    //  before mailbox admission, an unrelated session must still submit.
    const int second_client_fd = connect_raw_stream_client (endpoint);
    TEST_ASSERT_TRUE (second_client_fd >= 0);
    TEST_ASSERT_EQUAL_INT (1, send (second_client_fd, "?", 1, 0));
    zlink_msg_t second_connect_part;
    TEST_ASSERT_EQUAL_INT (0, zlink_msg_init (&second_connect_part));
    const zlink_routing_id_t *second_source_rid = NULL;
    has_more = ZLINK_PART_MORE;
    TEST_ASSERT_EQUAL_INT (
      ZLINK_RECV_OK,
      zlink_recv_part (stream, &second_source_rid, &second_connect_part, &has_more,
                       ZLINK_RECV_FLAGS_NONE));
    TEST_ASSERT_NOT_NULL (second_source_rid);
    zlink_routing_id_t second_session_rid = *second_source_rid;
    zlink_msg_close (&second_connect_part);

    zlink_actor_ref_t second_actor;
    TEST_ASSERT_EQUAL_INT (
      ZLINK_REQUEST_OK,
      zlink_mesh_node_actor_new (node, "stream-writer-2", NULL, 0, &second_actor,
                                 ZLINK_SEND_FLAGS_NONE, 1000));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_OK,
      zlink_stream_session_bind_actor (service, &second_session_rid,
                                       &second_actor, &operation_id, 1000));

    zlink_msg_t paused_part;
    zlink_msg_t independent_part;
    make_payload (&paused_part, "S3");
    make_payload (&independent_part, "X1");
    std::atomic<zlink_submit_result_t> paused_submit (
      ZLINK_SUBMIT_INTERNAL_ERROR);
    std::atomic<zlink_submit_result_t> independent_submit (
      ZLINK_SUBMIT_INTERNAL_ERROR);
    zlink_test_stream_session_pause_before_local_actor_admit (1);
    std::thread paused_sender ([&] {
        paused_submit.store (
          zlink_stream_session_send_to_actor (
            service, &session_rid, &actor, NULL, &paused_part, 1,
            ZLINK_SEND_FLAGS_NONE),
          std::memory_order_release);
    });
    first_paused = false;
    for (int attempt = 0; attempt < 100 && !first_paused; ++attempt) {
        first_paused =
          zlink_test_stream_session_local_actor_admit_paused () != 0;
        if (!first_paused)
            msleep (10);
    }
    TEST_ASSERT_TRUE_MESSAGE (
      first_paused, "first session did not reach the admission barrier");
    std::thread independent_sender ([&] {
        independent_submit.store (
          zlink_stream_session_send_to_actor (
            service, &second_session_rid, &second_actor, NULL,
            &independent_part, 1, ZLINK_SEND_FLAGS_NONE),
          std::memory_order_release);
    });
    bool independent_completed = false;
    for (int attempt = 0; attempt < 100 && !independent_completed; ++attempt) {
        independent_completed =
          independent_submit.load (std::memory_order_acquire)
          != ZLINK_SUBMIT_INTERNAL_ERROR;
        if (!independent_completed)
            msleep (10);
    }
    zlink_test_stream_session_pause_before_local_actor_admit (0);
    paused_sender.join ();
    independent_sender.join ();
    TEST_ASSERT_TRUE_MESSAGE (
      independent_completed,
      "a paused session blocked an unrelated session submission");
    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_OK, paused_submit.load (std::memory_order_acquire));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_OK, independent_submit.load (std::memory_order_acquire));
    zlink_msg_close (&paused_part);
    zlink_msg_close (&independent_part);

    //  A transfer fence shares the session ordering gate. It must wait for a
    //  submit that already passed binding validation, then make later submits
    //  use the transfer backpressure path.
    zlink_msg_t before_fence_part;
    make_payload (&before_fence_part, "F1");
    std::atomic<zlink_submit_result_t> before_fence_submit (
      ZLINK_SUBMIT_INTERNAL_ERROR);
    std::atomic<bool> fence_completed (false);
    zlink_test_stream_session_pause_before_local_actor_admit (1);
    std::thread before_fence_sender ([&] {
        before_fence_submit.store (
          zlink_stream_session_send_to_actor (
            service, &session_rid, &actor, NULL, &before_fence_part, 1,
            ZLINK_SEND_FLAGS_NONE),
          std::memory_order_release);
    });
    first_paused = false;
    for (int attempt = 0; attempt < 100 && !first_paused; ++attempt) {
        first_paused =
          zlink_test_stream_session_local_actor_admit_paused () != 0;
        if (!first_paused)
            msleep (10);
    }
    TEST_ASSERT_TRUE_MESSAGE (
      first_paused, "pre-fence submit did not reach the admission barrier");
    const uint64_t transfer_serial = 77;
    std::thread fence_thread ([&] {
        zlink_test_stream_session_fence_actor (
          service, &actor, transfer_serial);
        fence_completed.store (true, std::memory_order_release);
    });
    msleep (50);
    TEST_ASSERT_FALSE_MESSAGE (
      fence_completed.load (std::memory_order_acquire),
      "transfer fence bypassed the per-session submit gate");
    zlink_test_stream_session_pause_before_local_actor_admit (0);
    before_fence_sender.join ();
    fence_thread.join ();
    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_OK,
      before_fence_submit.load (std::memory_order_acquire));
    TEST_ASSERT_TRUE (fence_completed.load (std::memory_order_acquire));

    zlink_msg_t after_fence_part;
    make_payload (&after_fence_part, "F2");
    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_BACKPRESSURED,
      zlink_stream_session_send_to_actor (
        service, &session_rid, &actor, NULL, &after_fence_part, 1,
        ZLINK_SEND_FLAGS_NONE));
    TEST_ASSERT_EQUAL_INT (EAGAIN, zlink_errno ());
    zlink_test_stream_session_abort_actor (
      service, &actor, transfer_serial);
    zlink_msg_close (&before_fence_part);
    zlink_msg_close (&after_fence_part);

    //  Drain both records so node shutdown observes no pending application
    //  work; the ordering assertions above do not depend on claim order.
    for (int record = 0; record < 3; ++record) {
        zlink_mesh_claim_t claim;
        take_ready_claim (node, ZLINK_MESH_OWNER_ACTOR,
                          ZLINK_MESH_READY_APPLICATION, &claim);
        void *batch = zlink_mesh_receive_batch_new (1, 1, 8);
        TEST_ASSERT_NOT_NULL (batch);
        zlink_mesh_receive_requirements_t required;
        memset (&required, 0, sizeof (required));
        required.struct_size = sizeof (required);
        required.version = 1;
        TEST_ASSERT_EQUAL_INT (
          ZLINK_RECV_OK,
          zlink_mesh_claim_recv_batch (&claim, batch, &required,
                                       ZLINK_RECV_FLAGS_NONE));
        TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK,
                               zlink_mesh_claim_release (&claim));
        TEST_ASSERT_EQUAL_INT (
          ZLINK_CLOSE_OK, zlink_mesh_receive_batch_destroy (&batch));
    }

    zlink_msg_t parts[3];
    make_payload (&parts[0], "hello");
    make_payload (&parts[1], "-");
    make_payload (&parts[2], "world");
    TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_OK,
                           zlink_mesh_node_actor_send_bound_session (
                             node, &actor, parts, 3, ZLINK_SEND_FLAGS_NONE));
    for (size_t i = 0; i < 3; ++i)
        zlink_msg_close (&parts[i]);

    char received[11];
    size_t received_size = 0;
    while (received_size < sizeof (received)) {
        const ssize_t count =
          recv (client_fd, received + received_size, sizeof (received) - received_size, 0);
        TEST_ASSERT_TRUE (count > 0);
        received_size += static_cast<size_t> (count);
    }
    TEST_ASSERT_EQUAL_MEMORY ("hello-world", received, sizeof (received));

    zlink_stream_session_binding_t binding;
    memset (&binding, 0, sizeof (binding));
    binding.struct_size = sizeof (binding);
    binding.version = ZLINK_STREAM_SESSION_ABI_VERSION;
    size_t binding_count = 1;
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK,
      zlink_stream_session_bindings (service, &session_rid, &binding, &binding_count));
    zlink_mesh_operation_id_t close_operation;
    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_OK,
      zlink_mesh_node_actor_close_bound_session (node, &actor, binding.binding_generation,
                                                  &close_operation, 1000));
    char after_close = 0;
    TEST_ASSERT_EQUAL_INT (0, recv (client_fd, &after_close, sizeof (after_close), 0));

    close (second_client_fd);
    close (client_fd);
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK,
                           zlink_stream_session_service_shutdown (service, 1000));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_stream_session_service_destroy (&service));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_close (stream));
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK, zlink_mesh_node_shutdown (node, 1000));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_node_destroy (&node));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_ctx_term (ctx));
#endif
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
    RUN_TEST (test_mesh_node_common_timeout_validation);
    RUN_TEST (test_mesh_local_channel_request_reply);
    RUN_TEST (test_mesh_metadata_ownership_and_timeout_contract);
    RUN_TEST (test_mesh_local_multicast_and_subscription);
    RUN_TEST (test_mesh_local_multicast_target_specific_drop);
    RUN_TEST (test_mesh_publish_snapshot_cannot_cross_shutdown);
    RUN_TEST (test_mesh_actor_lifecycle_and_messaging);
    RUN_TEST (test_stream_session_binding_reports_actor_membership_epoch);
    RUN_TEST (test_stream_session_actor_send_is_one_complete_byte_message);
    RUN_TEST (test_mesh_node_poller_source);
    const int rc = UNITY_END ();
    fflush (NULL);
    return rc;
}
