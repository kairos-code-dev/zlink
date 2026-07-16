/* SPDX-License-Identifier: MPL-2.0 */

//  Two-process MeshNode peer contract coverage: HELLO admission, readiness
//  transitions, weight update propagation, MeshName conflict rejection and a
//  remote node request/reply round trip. Each case forks before any context
//  exists and hands the parent's resolved endpoint to the child over a pipe.

#include "../testutil_unity.hpp"

#include <string.h>

#if !defined _WIN32
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

SETUP_TEARDOWN_TESTCONTEXT

#if !defined _WIN32
namespace
{
const char mesh_name[] = "mesh-admission";
const char channel_name[] = "orders";
const int poll_deadline_ms = 15000;
const int poll_step_ms = 20;

void *new_started_node (void *ctx_, const char *mesh_name_, const char *rid_)
{
    zlink_mesh_node_options_t options;
    memset (&options, 0, sizeof (options));
    options.struct_size = sizeof (options);
    options.version = 1;
    options.mesh_name = mesh_name_;
    options.mesh_name_size = strlen (mesh_name_);
    void *node = zlink_mesh_node_new (ctx_, &options);
    TEST_ASSERT_NOT_NULL (node);

    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK, zlink_set_routing_id (node, rid_, strlen (rid_)));
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK, zlink_mesh_node_set_bind (node, "tcp://127.0.0.1:0"));
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK, zlink_mesh_node_add_channel_name (node, channel_name));
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK, zlink_mesh_node_start (node));
    return node;
}

void node_status (void *node_, zlink_mesh_node_status_t *status_out_)
{
    memset (status_out_, 0, sizeof (*status_out_));
    status_out_->struct_size = sizeof (*status_out_);
    status_out_->version = 1;
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK, zlink_mesh_node_status (node_, status_out_));
}

//  Writes the node's resolved endpoint (newline-terminated) into fd_.
void publish_endpoint (void *node_, int fd_)
{
    zlink_mesh_node_status_t status;
    node_status (node_, &status);
    TEST_ASSERT_TRUE (status.local_endpoint[0] != '\0');
    const size_t len = strlen (status.local_endpoint);
    TEST_ASSERT_EQUAL_INT (static_cast<int> (len), (int) write (fd_, status.local_endpoint, len));
    TEST_ASSERT_EQUAL_INT (1, (int) write (fd_, "\n", 1));
}

//  Blocking read of one newline-terminated endpoint from fd_.
void read_endpoint (int fd_, char *endpoint_out_, size_t capacity_)
{
    size_t used = 0;
    while (used + 1 < capacity_) {
        char ch = 0;
        const ssize_t n = read (fd_, &ch, 1);
        if (n <= 0)
            break;
        if (ch == '\n')
            break;
        endpoint_out_[used++] = ch;
    }
    endpoint_out_[used] = '\0';
}

uint64_t submit_peer_intent (void *node_, const char *endpoint_)
{
    zlink_mesh_peer_connection_options_t options;
    memset (&options, 0, sizeof (options));
    options.struct_size = sizeof (options);
    options.version = 1;
    options.endpoint = endpoint_;
    options.endpoint_size = strlen (endpoint_);
    uint64_t intent_id = 0;
    const zlink_connect_result_t rc = zlink_mesh_node_connect_peer (node_, &options, &intent_id);
    if (rc != ZLINK_CONNECT_OK)
        return 0;
    return intent_id;
}

//  Polls the peers query until the single expected peer reaches state_ or the
//  deadline passes; returns the observed entry.
bool wait_peer_state (void *node_,
                      zlink_mesh_peer_state_t state_,
                      zlink_mesh_peer_entry_t *entry_out_)
{
    for (int waited = 0; waited < poll_deadline_ms; waited += poll_step_ms) {
        zlink_mesh_peer_entry_t entries[4];
        memset (entries, 0, sizeof (entries));
        for (size_t i = 0; i < 4; ++i) {
            entries[i].struct_size = sizeof (entries[i]);
            entries[i].version = 1;
        }
        size_t count = 4;
        if (zlink_mesh_node_peers (node_, entries, &count) == ZLINK_CONFIG_OK && count >= 1
            && entries[0].state == state_) {
            *entry_out_ = entries[0];
            return true;
        }
        msleep (poll_step_ms);
    }
    return false;
}

bool wait_node_state (void *node_, zlink_mesh_node_state_t state_)
{
    for (int waited = 0; waited < poll_deadline_ms; waited += poll_step_ms) {
        zlink_mesh_node_status_t status;
        node_status (node_, &status);
        if (status.state == state_)
            return true;
        msleep (poll_step_ms);
    }
    return false;
}

bool wait_admitted_count (void *node_, uint32_t count_)
{
    for (int waited = 0; waited < poll_deadline_ms; waited += poll_step_ms) {
        zlink_mesh_node_status_t status;
        node_status (node_, &status);
        if (status.admitted_peer_count >= count_)
            return true;
        msleep (poll_step_ms);
    }
    return false;
}

void make_payload (zlink_msg_t *msg_, const char *text_)
{
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK, zlink_msg_init_size (msg_, strlen (text_)));
    memcpy (zlink_msg_data (msg_), text_, strlen (text_));
}

//  Drains one ready record for the wanted owner kind and domain, takes its
//  claim and returns it. Fails if nothing arrives before the deadline.
void take_ready_claim (void *node_,
                       zlink_mesh_owner_kind_t owner_kind_,
                       zlink_mesh_ready_domain_mask_t domain_,
                       zlink_mesh_claim_t *claim_out_)
{
    void *batch = zlink_mesh_ready_batch_new (8);
    TEST_ASSERT_NOT_NULL (batch);
    bool taken = false;
    for (int waited = 0; waited < poll_deadline_ms && !taken; waited += poll_step_ms) {
        uint32_t residue = 0;
        const zlink_recv_result_t rc =
          zlink_mesh_node_drain_ready (node_, domain_, batch, &residue, ZLINK_RECV_FLAGS_DONTWAIT);
        if (rc == ZLINK_RECV_NO_DATA) {
            msleep (poll_step_ms);
            continue;
        }
        TEST_ASSERT_EQUAL_INT (ZLINK_RECV_OK, rc);
        const size_t count = zlink_mesh_ready_batch_count (batch);
        const zlink_mesh_ready_record_t *records = zlink_mesh_ready_batch_data (batch);
        for (size_t i = 0; i < count && !taken; ++i) {
            if (records[i].owner_kind != owner_kind_ || (records[i].domain & domain_) == 0)
                continue;
            TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK,
                                   zlink_mesh_ready_batch_take_claim (batch, i, claim_out_));
            taken = true;
        }
        TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK, zlink_mesh_ready_batch_reset (batch));
        if (!taken)
            msleep (poll_step_ms);
    }
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_ready_batch_destroy (&batch));
    TEST_ASSERT_TRUE_MESSAGE (taken, "expected ready record did not arrive");
}

//  Drains the Node owner's infrastructure lane until the completion for
//  operation_id_ arrives. Verifies the terminal result; when expected_reply_
//  is non-NULL the single reply part must match it; when kind_data_out_ is
//  non-NULL the record's kind_data is copied out. Returns 0 or a failure
//  code in the 50s range.
int wait_node_completion (void *node_,
                          const zlink_mesh_operation_id_t &operation_id_,
                          const char *expected_reply_,
                          void *kind_data_out_,
                          size_t kind_data_capacity_)
{
    void *ready = zlink_mesh_ready_batch_new (8);
    void *batch = zlink_mesh_receive_batch_new (8, 32, 64 * 1024);
    if (!ready || !batch)
        return 50;
    int result = 51;
    bool done = false;
    for (int waited = 0; waited < poll_deadline_ms && !done; waited += poll_step_ms) {
        uint32_t residue = 0;
        const zlink_recv_result_t rc = zlink_mesh_node_drain_ready (
          node_, ZLINK_MESH_READY_INFRASTRUCTURE, ready, &residue, ZLINK_RECV_FLAGS_DONTWAIT);
        if (rc == ZLINK_RECV_NO_DATA) {
            msleep (poll_step_ms);
            continue;
        }
        if (rc != ZLINK_RECV_OK) {
            result = 52;
            break;
        }
        const size_t count = zlink_mesh_ready_batch_count (ready);
        const zlink_mesh_ready_record_t *records = zlink_mesh_ready_batch_data (ready);
        bool claimed = false;
        zlink_mesh_claim_t claim;
        for (size_t i = 0; i < count && !claimed; ++i) {
            if (records[i].owner_kind != ZLINK_MESH_OWNER_NODE)
                continue;
            if (zlink_mesh_ready_batch_take_claim (ready, i, &claim) != ZLINK_CONFIG_OK) {
                result = 53;
                break;
            }
            claimed = true;
        }
        zlink_mesh_ready_batch_reset (ready);
        if (!claimed) {
            msleep (poll_step_ms);
            continue;
        }

        zlink_mesh_receive_requirements_t requirements;
        memset (&requirements, 0, sizeof (requirements));
        if (zlink_mesh_claim_recv_batch (&claim, batch, &requirements, ZLINK_RECV_FLAGS_NONE)
            != ZLINK_RECV_OK) {
            result = 54;
            zlink_mesh_claim_release (&claim);
            break;
        }
        const size_t record_count = zlink_mesh_receive_batch_count (batch);
        const zlink_mesh_receive_record_t *record = zlink_mesh_receive_batch_data (batch);
        const zlink_msg_t *parts = zlink_mesh_receive_batch_parts (batch);
        for (size_t r = 0; r < record_count; ++r) {
            if (record[r].kind != ZLINK_MESH_RECORD_COMPLETION
                || record[r].operation_id.high != operation_id_.high
                || record[r].operation_id.low != operation_id_.low)
                continue;
            done = true;
            if (record[r].terminal_result != 0 || record[r].failure_errno != 0)
                result = 55;
            else if (expected_reply_
                     && (record[r].part_count != 1
                         || zlink_msg_size (&parts[record[r].part_offset])
                              != strlen (expected_reply_)
                         || memcmp (zlink_msg_data (const_cast<zlink_msg_t *> (
                                      &parts[record[r].part_offset])),
                                    expected_reply_, strlen (expected_reply_))
                              != 0))
                result = 56;
            else if (kind_data_out_
                     && (record[r].kind_data_size < kind_data_capacity_
                         || !record[r].kind_data))
                result = 57;
            else {
                if (kind_data_out_)
                    memcpy (kind_data_out_, record[r].kind_data, kind_data_capacity_);
                result = 0;
            }
            break;
        }
        zlink_mesh_claim_release (&claim);
        zlink_mesh_receive_batch_reset (batch);
        if (done)
            break;
    }
    zlink_mesh_ready_batch_destroy (&ready);
    zlink_mesh_receive_batch_destroy (&batch);
    return result;
}

int wait_node_completion_payload (void *node_,
                                  const zlink_mesh_operation_id_t &operation_id_,
                                  const char *expected_reply_)
{
    return wait_node_completion (node_, operation_id_, expected_reply_, NULL, 0);
}

int wait_node_completion_kind_data (void *node_,
                                    const zlink_mesh_operation_id_t &operation_id_,
                                    void *kind_data_out_,
                                    size_t kind_data_capacity_)
{
    return wait_node_completion (node_, operation_id_, NULL, kind_data_out_,
                                 kind_data_capacity_);
}

//  Drains the requester Spot's infrastructure lane until the completion for
//  operation_id_ arrives; verifies the terminal result and reply payload.
//  Returns 0 on success or a distinct failure code.
int wait_spot_completion (void *node_,
                          void *spot_,
                          const zlink_mesh_operation_id_t &operation_id_,
                          const char *expected_reply_)
{
    (void) spot_;
    void *ready = zlink_mesh_ready_batch_new (8);
    void *batch = zlink_mesh_receive_batch_new (8, 32, 64 * 1024);
    if (!ready || !batch)
        return 40;
    int result = 41;
    for (int waited = 0; waited < poll_deadline_ms; waited += poll_step_ms) {
        uint32_t residue = 0;
        const zlink_recv_result_t rc = zlink_mesh_node_drain_ready (
          node_, ZLINK_MESH_READY_INFRASTRUCTURE, ready, &residue, ZLINK_RECV_FLAGS_DONTWAIT);
        if (rc == ZLINK_RECV_NO_DATA) {
            msleep (poll_step_ms);
            continue;
        }
        if (rc != ZLINK_RECV_OK) {
            result = 42;
            break;
        }
        const size_t count = zlink_mesh_ready_batch_count (ready);
        const zlink_mesh_ready_record_t *records = zlink_mesh_ready_batch_data (ready);
        bool claimed = false;
        zlink_mesh_claim_t claim;
        for (size_t i = 0; i < count && !claimed; ++i) {
            if (records[i].owner_kind != ZLINK_MESH_OWNER_SPOT)
                continue;
            if (zlink_mesh_ready_batch_take_claim (ready, i, &claim) != ZLINK_CONFIG_OK) {
                result = 43;
                break;
            }
            claimed = true;
        }
        zlink_mesh_ready_batch_reset (ready);
        if (!claimed) {
            msleep (poll_step_ms);
            continue;
        }

        zlink_mesh_receive_requirements_t requirements;
        memset (&requirements, 0, sizeof (requirements));
        if (zlink_mesh_claim_recv_batch (&claim, batch, &requirements, ZLINK_RECV_FLAGS_NONE)
            != ZLINK_RECV_OK) {
            result = 44;
            zlink_mesh_claim_release (&claim);
            break;
        }
        if (zlink_mesh_receive_batch_count (batch) != 1) {
            result = 45;
            zlink_mesh_claim_release (&claim);
            break;
        }
        const zlink_mesh_receive_record_t *record = zlink_mesh_receive_batch_data (batch);
        const zlink_msg_t *parts = zlink_mesh_receive_batch_parts (batch);
        if (record->kind != ZLINK_MESH_RECORD_COMPLETION)
            result = 46;
        else if (record->operation_id.high != operation_id_.high
                 || record->operation_id.low != operation_id_.low)
            result = 47;
        else if (record->terminal_result != 0 || record->failure_errno != 0)
            result = 48;
        else if (record->part_count != 1
                 || zlink_msg_size (&parts[record->part_offset]) != strlen (expected_reply_)
                 || memcmp (zlink_msg_data (const_cast<zlink_msg_t *> (&parts[record->part_offset])),
                            expected_reply_, strlen (expected_reply_))
                      != 0)
            result = 49;
        else
            result = 0;
        zlink_mesh_claim_release (&claim);
        break;
    }
    zlink_mesh_ready_batch_destroy (&ready);
    zlink_mesh_receive_batch_destroy (&batch);
    return result;
}

//  Runs child_fn_ in a forked process before any context exists in it and
//  fails the test if the child does not exit 0 before the deadline.
template <typename ParentFn, typename ChildFn>
void run_two_process_case (ParentFn parent_fn_, ChildFn child_fn_)
{
    int endpoint_pipe[2];
    TEST_ASSERT_EQUAL_INT (0, pipe (endpoint_pipe));

    fflush (NULL);
    const pid_t child = fork ();
    TEST_ASSERT_TRUE (child >= 0);

    if (child == 0) {
        close (endpoint_pipe[1]);
        setup_test_environment (60);
        const int rc = child_fn_ (endpoint_pipe[0]);
        close (endpoint_pipe[0]);
        fflush (NULL);
        std::_Exit (rc);
    }

    close (endpoint_pipe[0]);
    parent_fn_ (endpoint_pipe[1]);
    close (endpoint_pipe[1]);

    int status = 0;
    pid_t wait_rc = 0;
    const int wait_deadline_ms = 30000;
    for (int waited = 0; waited < wait_deadline_ms; waited += poll_step_ms) {
        wait_rc = waitpid (child, &status, WNOHANG);
        TEST_ASSERT_TRUE (wait_rc >= 0);
        if (wait_rc == child)
            break;
        msleep (poll_step_ms);
    }
    if (wait_rc != child) {
        kill (child, SIGKILL);
        (void) waitpid (child, &status, 0);
        TEST_FAIL_MESSAGE ("peer child process did not exit in time");
    }
    TEST_ASSERT_TRUE_MESSAGE (WIFEXITED (status) && WEXITSTATUS (status) == 0,
                              "peer child process reported failure");
}
} // namespace

//  B connects to A; both must observe ADMITTED with the advertised channel
//  descriptor, B reaches READY, and A's weight change propagates to B as a
//  higher descriptor revision.
void test_peer_admission_readiness_and_weight_update ()
{
    run_two_process_case (
      //  parent: node A
      [] (int endpoint_fd) {
          void *ctx = zlink_ctx_new ();
          TEST_ASSERT_NOT_NULL (ctx);
          void *node = new_started_node (ctx, mesh_name, "node-a");
          publish_endpoint (node, endpoint_fd);

          TEST_ASSERT_TRUE_MESSAGE (wait_admitted_count (node, 1),
                                    "node A did not admit the inbound peer");
          zlink_mesh_peer_entry_t entry;
          TEST_ASSERT_TRUE_MESSAGE (wait_peer_state (node, ZLINK_MESH_PEER_ADMITTED, &entry),
                                    "node A peers query did not report ADMITTED");
          TEST_ASSERT_EQUAL_UINT (strlen ("node-b"), entry.routing_id.size);
          TEST_ASSERT_EQUAL_MEMORY ("node-b", entry.routing_id.data, entry.routing_id.size);
          TEST_ASSERT_EQUAL_UINT (1, entry.channel_count);

          //  weight update must reach the admitted peer as revision 2.
          TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK,
                                 zlink_mesh_node_set_channel_weight (node, channel_name, 25));

          //  hold the node alive until the child confirms by exiting; the
          //  run_two_process_case parent hook returns before waitpid, so wait
          //  for the child-side observation through a short settle instead of
          //  tearing the wire down immediately.
          msleep (500);
          TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK, zlink_mesh_node_shutdown (node, 1000));
          TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_node_destroy (&node));
          TEST_ASSERT_EQUAL_INT (0, zlink_ctx_term (ctx));
      },
      //  child: node B
      [] (int endpoint_fd) -> int {
          char endpoint[ZLINK_MESH_ENDPOINT_MAX + 1];
          read_endpoint (endpoint_fd, endpoint, sizeof (endpoint));
          if (endpoint[0] == '\0')
            return 10;

          void *ctx = zlink_ctx_new ();
          if (!ctx)
              return 11;
          void *node = new_started_node (ctx, mesh_name, "node-b");
          if (submit_peer_intent (node, endpoint) == 0)
              return 12;

          zlink_mesh_peer_entry_t entry;
          if (!wait_peer_state (node, ZLINK_MESH_PEER_ADMITTED, &entry))
              return 13;
          if (entry.routing_id.size != strlen ("node-a")
              || memcmp (entry.routing_id.data, "node-a", entry.routing_id.size) != 0)
              return 14;
          if (entry.lifecycle_generation == 0)
              return 15;
          if (!wait_node_state (node, ZLINK_MESH_NODE_READY))
              return 16;

          //  advertised membership: channel "orders" at initial weight 100,
          //  then the parent's weight 25 update at a higher revision.
          char names[4][ZLINK_CHANNEL_NAME_MAX + 1];
          uint32_t weights[4];
          size_t count = 4;
          if (zlink_mesh_node_peer_channels (node, &entry.routing_id, entry.lifecycle_generation,
                                             names, weights, &count)
                != ZLINK_CONFIG_OK
              || count != 1 || strcmp (names[0], channel_name) != 0)
              return 17;

          bool weight_updated = false;
          for (int waited = 0; waited < poll_deadline_ms && !weight_updated;
               waited += poll_step_ms) {
              count = 4;
              if (zlink_mesh_node_peer_channels (node, &entry.routing_id,
                                                 entry.lifecycle_generation, names, weights,
                                                 &count)
                    == ZLINK_CONFIG_OK
                  && count == 1 && weights[0] == 25)
                  weight_updated = true;
              else
                  msleep (poll_step_ms);
          }
          if (!weight_updated)
              return 18;

          zlink_mesh_node_shutdown (node, 1000);
          if (zlink_mesh_node_destroy (&node) != ZLINK_CLOSE_OK)
              return 19;
          if (zlink_ctx_term (ctx) != 0)
              return 20;
          return 0;
      });
}

//  A different MeshName must be rejected during admission: the intent ends in
//  ERROR with a conflict errno and the connecting node stays PARTIAL_READY.
void test_peer_mesh_name_mismatch_is_conflict ()
{
    run_two_process_case (
      [] (int endpoint_fd) {
          void *ctx = zlink_ctx_new ();
          TEST_ASSERT_NOT_NULL (ctx);
          void *node = new_started_node (ctx, mesh_name, "node-a");
          publish_endpoint (node, endpoint_fd);
          msleep (500);
          TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK, zlink_mesh_node_shutdown (node, 1000));
          TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_node_destroy (&node));
          TEST_ASSERT_EQUAL_INT (0, zlink_ctx_term (ctx));
      },
      [] (int endpoint_fd) -> int {
          char endpoint[ZLINK_MESH_ENDPOINT_MAX + 1];
          read_endpoint (endpoint_fd, endpoint, sizeof (endpoint));
          if (endpoint[0] == '\0')
              return 10;

          void *ctx = zlink_ctx_new ();
          if (!ctx)
              return 11;
          void *node = new_started_node (ctx, "mesh-other", "node-b");
          if (submit_peer_intent (node, endpoint) == 0)
              return 12;

          zlink_mesh_peer_entry_t entry;
          if (!wait_peer_state (node, ZLINK_MESH_PEER_ERROR, &entry))
              return 13;
          if (entry.last_error != EEXIST)
              return 14;

          zlink_mesh_node_status_t status;
          memset (&status, 0, sizeof (status));
          status.struct_size = sizeof (status);
          status.version = 1;
          if (zlink_mesh_node_status (node, &status) != ZLINK_CONFIG_OK
              || status.state != ZLINK_MESH_NODE_PARTIAL_READY)
              return 15;

          zlink_mesh_node_shutdown (node, 1000);
          if (zlink_mesh_node_destroy (&node) != ZLINK_CLOSE_OK)
              return 16;
          if (zlink_ctx_term (ctx) != 0)
              return 17;
          return 0;
      });
}

//  B sends a node request to A over the wire; A claims it from its application
//  mailbox, replies through the one-shot token and B receives exactly one
//  terminal completion carrying the reply payload.
void test_remote_node_request_reply_round_trip ()
{
    run_two_process_case (
      //  parent: node A answers one request.
      [] (int endpoint_fd) {
          void *ctx = zlink_ctx_new ();
          TEST_ASSERT_NOT_NULL (ctx);
          void *node = new_started_node (ctx, mesh_name, "node-a");
          publish_endpoint (node, endpoint_fd);

          zlink_mesh_claim_t claim;
          take_ready_claim (node, ZLINK_MESH_OWNER_NODE, ZLINK_MESH_READY_APPLICATION, &claim);

          void *batch = zlink_mesh_receive_batch_new (8, 32, 64 * 1024);
          TEST_ASSERT_NOT_NULL (batch);
          zlink_mesh_receive_requirements_t requirements;
          memset (&requirements, 0, sizeof (requirements));
          TEST_ASSERT_EQUAL_INT (ZLINK_RECV_OK,
                                 zlink_mesh_claim_recv_batch (&claim, batch, &requirements,
                                                              ZLINK_RECV_FLAGS_NONE));
          TEST_ASSERT_EQUAL_UINT (1, zlink_mesh_receive_batch_count (batch));
          const zlink_mesh_receive_record_t *record = zlink_mesh_receive_batch_data (batch);
          TEST_ASSERT_EQUAL_INT (ZLINK_MESH_RECORD_NODE_REQUEST, record->kind);
          TEST_ASSERT_EQUAL_UINT (strlen ("node-b"), record->source_node_rid.size);
          TEST_ASSERT_EQUAL_MEMORY ("node-b", record->source_node_rid.data,
                                    record->source_node_rid.size);
          TEST_ASSERT_EQUAL_UINT (1, record->part_count);
          const zlink_msg_t *parts = zlink_mesh_receive_batch_parts (batch);
          TEST_ASSERT_EQUAL_UINT (strlen ("ping-remote"),
                                  zlink_msg_size (&parts[record->part_offset]));
          TEST_ASSERT_EQUAL_MEMORY (
            "ping-remote", zlink_msg_data (const_cast<zlink_msg_t *> (&parts[record->part_offset])),
            strlen ("ping-remote"));

          zlink_msg_t reply;
          make_payload (&reply, "pong-remote");
          TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_OK,
                                 zlink_mesh_reply (&record->reply_token, &reply, 1,
                                                   ZLINK_SEND_FLAGS_NONE));

          TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_claim_release (&claim));
          TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_receive_batch_destroy (&batch));

          msleep (500);
          TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK, zlink_mesh_node_shutdown (node, 2000));
          TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_node_destroy (&node));
          TEST_ASSERT_EQUAL_INT (0, zlink_ctx_term (ctx));
      },
      //  child: node B issues the request and drains the completion.
      [] (int endpoint_fd) -> int {
          char endpoint[ZLINK_MESH_ENDPOINT_MAX + 1];
          read_endpoint (endpoint_fd, endpoint, sizeof (endpoint));
          if (endpoint[0] == '\0')
              return 10;

          void *ctx = zlink_ctx_new ();
          if (!ctx)
              return 11;
          void *node = new_started_node (ctx, mesh_name, "node-b");
          if (submit_peer_intent (node, endpoint) == 0)
              return 12;
          zlink_mesh_peer_entry_t entry;
          if (!wait_peer_state (node, ZLINK_MESH_PEER_ADMITTED, &entry))
              return 13;

          zlink_msg_t payload;
          if (zlink_msg_init_size (&payload, strlen ("ping-remote")) != 0)
              return 14;
          memcpy (zlink_msg_data (&payload), "ping-remote", strlen ("ping-remote"));
          zlink_mesh_operation_id_t operation_id;
          memset (&operation_id, 0, sizeof (operation_id));
          if (zlink_mesh_node_request_to_node (node, &entry.routing_id, NULL, &payload, 1,
                                               &operation_id, ZLINK_SEND_FLAGS_NONE, 10000)
              != ZLINK_SUBMIT_OK) {
              zlink_msg_close (&payload);
              return 15;
          }
          if (operation_id.high == 0 && operation_id.low == 0)
              return 16;

          //  the completion arrives on the Node infrastructure lane.
          void *ready = zlink_mesh_ready_batch_new (8);
          void *batch = zlink_mesh_receive_batch_new (8, 32, 64 * 1024);
          if (!ready || !batch)
              return 17;

          int result = 30;
          for (int waited = 0; waited < poll_deadline_ms; waited += poll_step_ms) {
              uint32_t residue = 0;
              const zlink_recv_result_t rc = zlink_mesh_node_drain_ready (
                node, ZLINK_MESH_READY_INFRASTRUCTURE, ready, &residue, ZLINK_RECV_FLAGS_DONTWAIT);
              if (rc == ZLINK_RECV_NO_DATA) {
                  msleep (poll_step_ms);
                  continue;
              }
              if (rc != ZLINK_RECV_OK) {
                  result = 18;
                  break;
              }
              const size_t count = zlink_mesh_ready_batch_count (ready);
              const zlink_mesh_ready_record_t *records = zlink_mesh_ready_batch_data (ready);
              bool claimed = false;
              zlink_mesh_claim_t claim;
              for (size_t i = 0; i < count && !claimed; ++i) {
                  if (records[i].owner_kind != ZLINK_MESH_OWNER_NODE
                      || (records[i].domain & ZLINK_MESH_READY_INFRASTRUCTURE) == 0)
                      continue;
                  if (zlink_mesh_ready_batch_take_claim (ready, i, &claim) != ZLINK_CONFIG_OK) {
                      result = 19;
                      break;
                  }
                  claimed = true;
              }
              zlink_mesh_ready_batch_reset (ready);
              if (!claimed) {
                  msleep (poll_step_ms);
                  continue;
              }

              zlink_mesh_receive_requirements_t requirements;
              memset (&requirements, 0, sizeof (requirements));
              if (zlink_mesh_claim_recv_batch (&claim, batch, &requirements,
                                               ZLINK_RECV_FLAGS_NONE)
                  != ZLINK_RECV_OK) {
                  result = 20;
                  zlink_mesh_claim_release (&claim);
                  break;
              }
              if (zlink_mesh_receive_batch_count (batch) != 1) {
                  result = 21;
                  zlink_mesh_claim_release (&claim);
                  break;
              }
              const zlink_mesh_receive_record_t *record = zlink_mesh_receive_batch_data (batch);
              const zlink_msg_t *parts = zlink_mesh_receive_batch_parts (batch);
              if (record->kind != ZLINK_MESH_RECORD_COMPLETION)
                  result = 22;
              else if (record->operation_id.high != operation_id.high
                       || record->operation_id.low != operation_id.low)
                  result = 23;
              else if (record->terminal_result != 0 || record->failure_errno != 0)
                  result = 24;
              else if (record->part_count != 1
                       || zlink_msg_size (&parts[record->part_offset])
                            != strlen ("pong-remote")
                       || memcmp (zlink_msg_data (
                                    const_cast<zlink_msg_t *> (&parts[record->part_offset])),
                                  "pong-remote", strlen ("pong-remote"))
                            != 0)
                  result = 25;
              else
                  result = 0;
              zlink_mesh_claim_release (&claim);
              break;
          }

          zlink_mesh_ready_batch_destroy (&ready);
          zlink_mesh_receive_batch_destroy (&batch);
          zlink_mesh_node_shutdown (node, 1000);
          if (zlink_mesh_node_destroy (&node) != ZLINK_CLOSE_OK && result == 0)
              result = 26;
          if (zlink_ctx_term (ctx) != 0 && result == 0)
              result = 27;
          return result;
      });
}
//  B addresses A's entry Spot directly (rid + generation from the wire
//  descriptor is not exposed, so A publishes its spot rid + generation over
//  the pipe) and the request/reply round trip completes over the Spot lane.
void test_remote_spot_direct_request_reply ()
{
    run_two_process_case (
      //  parent: node A answers one spot-direct request on its entry Spot.
      [] (int endpoint_fd) {
          void *ctx = zlink_ctx_new ();
          TEST_ASSERT_NOT_NULL (ctx);
          void *node = new_started_node (ctx, mesh_name, "node-a");

          void *spot = NULL;
          TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK, zlink_mesh_node_entry_spot (node, &spot));
          zlink_spot_status_t spot_status;
          memset (&spot_status, 0, sizeof (spot_status));
          spot_status.struct_size = sizeof (spot_status);
          spot_status.version = 1;
          TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK, zlink_spot_status (spot, &spot_status));

          //  endpoint line, then "<generation>\n" for direct addressing.
          publish_endpoint (node, endpoint_fd);
          char generation_line[32];
          snprintf (generation_line, sizeof (generation_line), "%llu\n",
                    (unsigned long long) spot_status.lifecycle_generation);
          TEST_ASSERT_EQUAL_INT ((int) strlen (generation_line),
                                 (int) write (endpoint_fd, generation_line,
                                              strlen (generation_line)));

          zlink_mesh_claim_t claim;
          take_ready_claim (node, ZLINK_MESH_OWNER_SPOT, ZLINK_MESH_READY_APPLICATION, &claim);

          void *batch = zlink_mesh_receive_batch_new (8, 32, 64 * 1024);
          TEST_ASSERT_NOT_NULL (batch);
          zlink_mesh_receive_requirements_t requirements;
          memset (&requirements, 0, sizeof (requirements));
          TEST_ASSERT_EQUAL_INT (ZLINK_RECV_OK,
                                 zlink_mesh_claim_recv_batch (&claim, batch, &requirements,
                                                              ZLINK_RECV_FLAGS_NONE));
          TEST_ASSERT_EQUAL_UINT (1, zlink_mesh_receive_batch_count (batch));
          const zlink_mesh_receive_record_t *record = zlink_mesh_receive_batch_data (batch);
          TEST_ASSERT_EQUAL_INT (ZLINK_MESH_RECORD_SPOT_REQUEST, record->kind);
          TEST_ASSERT_EQUAL_UINT (strlen ("node-b"), record->source_node_rid.size);

          zlink_msg_t reply;
          make_payload (&reply, "spot-pong");
          TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_OK,
                                 zlink_mesh_reply (&record->reply_token, &reply, 1,
                                                   ZLINK_SEND_FLAGS_NONE));
          TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_claim_release (&claim));
          TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_receive_batch_destroy (&batch));
          TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_spot_destroy (&spot));

          msleep (500);
          TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK, zlink_mesh_node_shutdown (node, 2000));
          TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_node_destroy (&node));
          TEST_ASSERT_EQUAL_INT (0, zlink_ctx_term (ctx));
      },
      //  child: node B sends a spot-direct request through its entry Spot.
      [] (int endpoint_fd) -> int {
          char endpoint[ZLINK_MESH_ENDPOINT_MAX + 1];
          read_endpoint (endpoint_fd, endpoint, sizeof (endpoint));
          char generation_text[32];
          read_endpoint (endpoint_fd, generation_text, sizeof (generation_text));
          const uint64_t target_generation = strtoull (generation_text, NULL, 10);
          if (endpoint[0] == '\0' || target_generation == 0)
              return 10;

          void *ctx = zlink_ctx_new ();
          if (!ctx)
              return 11;
          void *node = new_started_node (ctx, mesh_name, "node-b");
          if (submit_peer_intent (node, endpoint) == 0)
              return 12;
          zlink_mesh_peer_entry_t entry;
          if (!wait_peer_state (node, ZLINK_MESH_PEER_ADMITTED, &entry))
              return 13;

          void *spot = NULL;
          if (zlink_mesh_node_entry_spot (node, &spot) != ZLINK_CONFIG_OK)
              return 14;

          //  Target spot rid on A equals A's node rid (entry Spot).
          zlink_msg_t payload;
          if (zlink_msg_init_size (&payload, strlen ("spot-ping")) != 0)
              return 15;
          memcpy (zlink_msg_data (&payload), "spot-ping", strlen ("spot-ping"));
          zlink_mesh_operation_id_t operation_id;
          memset (&operation_id, 0, sizeof (operation_id));
          if (zlink_spot_request_to_spot (spot, &entry.routing_id, &entry.routing_id,
                                          target_generation, NULL, &payload, 1, &operation_id,
                                          ZLINK_SEND_FLAGS_NONE, 10000)
              != ZLINK_SUBMIT_OK) {
              zlink_msg_close (&payload);
              return 16;
          }

          const int completion = wait_spot_completion (node, spot, operation_id, "spot-pong");
          zlink_spot_destroy (&spot);
          zlink_mesh_node_shutdown (node, 1000);
          if (zlink_mesh_node_destroy (&node) != ZLINK_CLOSE_OK && completion == 0)
              return 26;
          if (zlink_ctx_term (ctx) != 0 && completion == 0)
              return 27;
          return completion;
      });
}

//  A publishes a NODROP multicast; the remote subscriber Spot on B and the
//  publish detail counts observe the remote target.
void test_remote_multicast_publish ()
{
    run_two_process_case (
      //  parent: node A publishes after B is admitted.
      [] (int endpoint_fd) {
          void *ctx = zlink_ctx_new ();
          TEST_ASSERT_NOT_NULL (ctx);
          void *node = new_started_node (ctx, mesh_name, "node-a");
          publish_endpoint (node, endpoint_fd);

          TEST_ASSERT_TRUE_MESSAGE (wait_admitted_count (node, 1),
                                    "node A did not admit the subscriber peer");
          //  Give B a moment to install its subscription after admission.
          msleep (300);

          void *publisher = zlink_mesh_node_publisher_new (node);
          TEST_ASSERT_NOT_NULL (publisher);
          zlink_msg_t part;
          make_payload (&part, "market-tick");
          zlink_mesh_publish_detail_t detail;
          memset (&detail, 0, sizeof (detail));
          detail.struct_size = sizeof (detail);
          detail.version = 1;
          TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_OK,
                                 zlink_mesh_node_publisher_publish (publisher, channel_name,
                                                                    "ticks.krw", NULL, &part, 1,
                                                                    &detail,
                                                                    ZLINK_SEND_FLAGS_NONE));
          zlink_msg_close (&part);
          TEST_ASSERT_EQUAL_UINT (1, detail.snapshot_remote_target_count);
          TEST_ASSERT_EQUAL_UINT (1, detail.admitted_remote_target_count);
          TEST_ASSERT_EQUAL_UINT (0, detail.dropped_remote_target_count);
          TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK,
                                 zlink_mesh_node_publisher_destroy (&publisher));

          msleep (500);
          TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK, zlink_mesh_node_shutdown (node, 2000));
          TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_node_destroy (&node));
          TEST_ASSERT_EQUAL_INT (0, zlink_ctx_term (ctx));
      },
      //  child: node B subscribes and drains the multicast record.
      [] (int endpoint_fd) -> int {
          char endpoint[ZLINK_MESH_ENDPOINT_MAX + 1];
          read_endpoint (endpoint_fd, endpoint, sizeof (endpoint));
          if (endpoint[0] == '\0')
              return 10;

          void *ctx = zlink_ctx_new ();
          if (!ctx)
              return 11;
          void *node = new_started_node (ctx, mesh_name, "node-b");

          void *spot = NULL;
          if (zlink_mesh_node_entry_spot (node, &spot) != ZLINK_CONFIG_OK)
              return 12;
          if (zlink_spot_set_subscription (spot, channel_name, "ticks.",
                                           ZLINK_SPOT_SUBSCRIPTION_PREFIX)
              != ZLINK_CONFIG_OK)
              return 13;

          if (submit_peer_intent (node, endpoint) == 0)
              return 14;
          zlink_mesh_peer_entry_t entry;
          if (!wait_peer_state (node, ZLINK_MESH_PEER_ADMITTED, &entry))
              return 15;

          //  Drain the multicast record from the entry Spot's application
          //  lane.
          void *ready = zlink_mesh_ready_batch_new (8);
          void *batch = zlink_mesh_receive_batch_new (8, 32, 64 * 1024);
          if (!ready || !batch)
              return 16;
          int result = 30;
          for (int waited = 0; waited < poll_deadline_ms; waited += poll_step_ms) {
              uint32_t residue = 0;
              const zlink_recv_result_t rc = zlink_mesh_node_drain_ready (
                node, ZLINK_MESH_READY_APPLICATION, ready, &residue, ZLINK_RECV_FLAGS_DONTWAIT);
              if (rc == ZLINK_RECV_NO_DATA) {
                  msleep (poll_step_ms);
                  continue;
              }
              if (rc != ZLINK_RECV_OK) {
                  result = 17;
                  break;
              }
              const size_t count = zlink_mesh_ready_batch_count (ready);
              const zlink_mesh_ready_record_t *records = zlink_mesh_ready_batch_data (ready);
              bool claimed = false;
              zlink_mesh_claim_t claim;
              for (size_t i = 0; i < count && !claimed; ++i) {
                  if (records[i].owner_kind != ZLINK_MESH_OWNER_SPOT)
                      continue;
                  if (zlink_mesh_ready_batch_take_claim (ready, i, &claim) != ZLINK_CONFIG_OK) {
                      result = 18;
                      break;
                  }
                  claimed = true;
              }
              zlink_mesh_ready_batch_reset (ready);
              if (!claimed) {
                  msleep (poll_step_ms);
                  continue;
              }

              zlink_mesh_receive_requirements_t requirements;
              memset (&requirements, 0, sizeof (requirements));
              if (zlink_mesh_claim_recv_batch (&claim, batch, &requirements,
                                               ZLINK_RECV_FLAGS_NONE)
                  != ZLINK_RECV_OK) {
                  result = 19;
                  zlink_mesh_claim_release (&claim);
                  break;
              }
              if (zlink_mesh_receive_batch_count (batch) != 1) {
                  result = 20;
                  zlink_mesh_claim_release (&claim);
                  break;
              }
              const zlink_mesh_receive_record_t *record = zlink_mesh_receive_batch_data (batch);
              const zlink_msg_t *parts = zlink_mesh_receive_batch_parts (batch);
              if (record->kind != ZLINK_MESH_RECORD_SPOT_MULTICAST)
                  result = 21;
              else if (!record->topic || record->topic_size != strlen ("ticks.krw")
                       || memcmp (record->topic, "ticks.krw", record->topic_size) != 0)
                  result = 22;
              else if (record->part_count != 1
                       || zlink_msg_size (&parts[record->part_offset]) != strlen ("market-tick"))
                  result = 23;
              else
                  result = 0;
              zlink_mesh_claim_release (&claim);
              break;
          }

          zlink_mesh_ready_batch_destroy (&ready);
          zlink_mesh_receive_batch_destroy (&batch);
          zlink_spot_destroy (&spot);
          zlink_mesh_node_shutdown (node, 1000);
          if (zlink_mesh_node_destroy (&node) != ZLINK_CLOSE_OK && result == 0)
              result = 26;
          if (zlink_ctx_term (ctx) != 0 && result == 0)
              result = 27;
          return result;
      });
}
//  B looks up A's actor over the wire, drives a request/reply round trip on
//  the actor lane and finally destroys it remotely.
void test_remote_actor_lookup_messaging_destroy ()
{
    run_two_process_case (
      //  parent: node A hosts actor "billing-1" and answers one request.
      [] (int endpoint_fd) {
          void *ctx = zlink_ctx_new ();
          TEST_ASSERT_NOT_NULL (ctx);
          void *node = new_started_node (ctx, mesh_name, "node-a");

          zlink_actor_ref_t actor;
          memset (&actor, 0, sizeof (actor));
          TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK,
                                 zlink_mesh_node_actor_new (node, "billing-1", NULL, 0, &actor,
                                                            ZLINK_SEND_FLAGS_NONE, 1000));
          publish_endpoint (node, endpoint_fd);

          //  Answer the remote actor request.
          zlink_mesh_claim_t claim;
          take_ready_claim (node, ZLINK_MESH_OWNER_ACTOR, ZLINK_MESH_READY_APPLICATION, &claim);
          void *batch = zlink_mesh_receive_batch_new (8, 32, 64 * 1024);
          TEST_ASSERT_NOT_NULL (batch);
          zlink_mesh_receive_requirements_t requirements;
          memset (&requirements, 0, sizeof (requirements));
          TEST_ASSERT_EQUAL_INT (ZLINK_RECV_OK,
                                 zlink_mesh_claim_recv_batch (&claim, batch, &requirements,
                                                              ZLINK_RECV_FLAGS_NONE));
          TEST_ASSERT_EQUAL_UINT (1, zlink_mesh_receive_batch_count (batch));
          const zlink_mesh_receive_record_t *record = zlink_mesh_receive_batch_data (batch);
          TEST_ASSERT_EQUAL_INT (ZLINK_MESH_RECORD_ACTOR_REQUEST, record->kind);
          zlink_msg_t reply;
          make_payload (&reply, "actor-pong");
          TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_OK,
                                 zlink_mesh_reply (&record->reply_token, &reply, 1,
                                                   ZLINK_SEND_FLAGS_NONE));
          TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_claim_release (&claim));
          TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_receive_batch_destroy (&batch));

          //  Stay alive until the child's remote destroy lands and it exits.
          bool destroyed = false;
          for (int waited = 0; waited < poll_deadline_ms && !destroyed; waited += poll_step_ms) {
              zlink_actor_location_t location;
              memset (&location, 0, sizeof (location));
              location.struct_size = sizeof (location);
              location.version = 1;
              if (zlink_mesh_node_actor_lookup (node, "billing-1", &location)
                  != ZLINK_CONFIG_OK)
                  destroyed = true;
              else
                  msleep (poll_step_ms);
          }
          TEST_ASSERT_TRUE_MESSAGE (destroyed, "remote destroy did not remove the actor");

          TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK, zlink_mesh_node_shutdown (node, 2000));
          TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_node_destroy (&node));
          TEST_ASSERT_EQUAL_INT (0, zlink_ctx_term (ctx));
      },
      //  child: node B drives lookup -> request -> destroy.
      [] (int endpoint_fd) -> int {
          char endpoint[ZLINK_MESH_ENDPOINT_MAX + 1];
          read_endpoint (endpoint_fd, endpoint, sizeof (endpoint));
          if (endpoint[0] == '\0')
              return 10;
          void *ctx = zlink_ctx_new ();
          if (!ctx)
              return 11;
          void *node = new_started_node (ctx, mesh_name, "node-b");
          if (submit_peer_intent (node, endpoint) == 0)
              return 12;
          zlink_mesh_peer_entry_t entry;
          if (!wait_peer_state (node, ZLINK_MESH_PEER_ADMITTED, &entry))
              return 13;

          //  remote lookup completes with the actor location as kind_data.
          zlink_mesh_operation_id_t lookup_op;
          memset (&lookup_op, 0, sizeof (lookup_op));
          if (zlink_mesh_node_actor_lookup_remote (node, &entry.routing_id, "billing-1",
                                                   &lookup_op, 10000)
              != ZLINK_SUBMIT_OK)
              return 14;
          zlink_actor_location_t location;
          memset (&location, 0, sizeof (location));
          {
              const int rc = wait_node_completion_kind_data (node, lookup_op, &location,
                                                             sizeof (location));
              if (rc != 0)
                  return rc;
          }
          if (location.actor.generation == 0
              || strcmp (location.actor.actor_id, "billing-1") != 0)
              return 15;

          //  request to the remote actor.
          zlink_msg_t payload;
          if (zlink_msg_init_size (&payload, strlen ("actor-ping")) != 0)
              return 16;
          memcpy (zlink_msg_data (&payload), "actor-ping", strlen ("actor-ping"));
          zlink_mesh_operation_id_t request_op;
          memset (&request_op, 0, sizeof (request_op));
          if (zlink_mesh_node_request_to_actor (node, &location.actor, NULL, &payload, 1,
                                                &request_op, ZLINK_SEND_FLAGS_NONE, 10000)
              != ZLINK_SUBMIT_OK) {
              zlink_msg_close (&payload);
              return 17;
          }
          {
              const int rc = wait_node_completion_payload (node, request_op, "actor-pong");
              if (rc != 0)
                  return rc;
          }

          //  remote destroy completes and removes the actor on A.
          zlink_mesh_operation_id_t destroy_op;
          memset (&destroy_op, 0, sizeof (destroy_op));
          if (zlink_mesh_node_actor_destroy (node, &location.actor, &destroy_op, 10000)
              != ZLINK_SUBMIT_OK)
              return 18;
          {
              const int rc = wait_node_completion_payload (node, destroy_op, NULL);
              if (rc != 0)
                  return rc;
          }

          zlink_mesh_node_shutdown (node, 1000);
          if (zlink_mesh_node_destroy (&node) != ZLINK_CLOSE_OK)
              return 26;
          if (zlink_ctx_term (ctx) != 0)
              return 27;
          return 0;
      });
}

//  B's local actor joins A's entry Spot; A accepts through the one-shot join
//  token and B observes the commit as a higher membership epoch.
void test_remote_actor_join_entry_spot ()
{
    run_two_process_case (
      //  parent: node A accepts one join on its entry Spot.
      [] (int endpoint_fd) {
          void *ctx = zlink_ctx_new ();
          TEST_ASSERT_NOT_NULL (ctx);
          void *node = new_started_node (ctx, mesh_name, "node-a");
          publish_endpoint (node, endpoint_fd);

          zlink_mesh_claim_t claim;
          take_ready_claim (node, ZLINK_MESH_OWNER_SPOT, ZLINK_MESH_READY_APPLICATION, &claim);
          void *batch = zlink_mesh_receive_batch_new (8, 32, 64 * 1024);
          TEST_ASSERT_NOT_NULL (batch);
          zlink_mesh_receive_requirements_t requirements;
          memset (&requirements, 0, sizeof (requirements));
          TEST_ASSERT_EQUAL_INT (ZLINK_RECV_OK,
                                 zlink_mesh_claim_recv_batch (&claim, batch, &requirements,
                                                              ZLINK_RECV_FLAGS_NONE));
          TEST_ASSERT_EQUAL_UINT (1, zlink_mesh_receive_batch_count (batch));
          const zlink_mesh_receive_record_t *record = zlink_mesh_receive_batch_data (batch);
          TEST_ASSERT_EQUAL_INT (ZLINK_MESH_RECORD_SPOT_CONTROL, record->kind);
          TEST_ASSERT_TRUE (record->kind_data_size >= sizeof (zlink_actor_control_record_t));
          const zlink_actor_control_record_t *control =
            static_cast<const zlink_actor_control_record_t *> (record->kind_data);
          TEST_ASSERT_EQUAL_INT (ZLINK_ACTOR_LIFECYCLE_JOINED, control->kind);
          TEST_ASSERT_EQUAL_STRING ("worker-1", control->current_actor.actor_id);

          TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_OK,
                                 zlink_actor_join_reply (&record->reply_token,
                                                         ZLINK_ACTOR_JOIN_ACCEPTED, NULL, 0,
                                                         ZLINK_SEND_FLAGS_NONE));
          TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_claim_release (&claim));
          TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_receive_batch_destroy (&batch));

          msleep (500);
          TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK, zlink_mesh_node_shutdown (node, 2000));
          TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_node_destroy (&node));
          TEST_ASSERT_EQUAL_INT (0, zlink_ctx_term (ctx));
      },
      //  child: node B joins its actor to A's entry Spot.
      [] (int endpoint_fd) -> int {
          char endpoint[ZLINK_MESH_ENDPOINT_MAX + 1];
          read_endpoint (endpoint_fd, endpoint, sizeof (endpoint));
          if (endpoint[0] == '\0')
              return 10;
          void *ctx = zlink_ctx_new ();
          if (!ctx)
              return 11;
          void *node = new_started_node (ctx, mesh_name, "node-b");
          zlink_actor_ref_t actor;
          memset (&actor, 0, sizeof (actor));
          if (zlink_mesh_node_actor_new (node, "worker-1", NULL, 0, &actor,
                                         ZLINK_SEND_FLAGS_NONE, 1000)
              != ZLINK_REQUEST_OK)
              return 12;
          if (submit_peer_intent (node, endpoint) == 0)
              return 13;
          zlink_mesh_peer_entry_t entry;
          if (!wait_peer_state (node, ZLINK_MESH_PEER_ADMITTED, &entry))
              return 14;

          zlink_mesh_operation_id_t join_op;
          memset (&join_op, 0, sizeof (join_op));
          if (zlink_mesh_node_actor_join_entry_spot (node, &actor, &entry.routing_id, NULL, 0,
                                                     &join_op, 10000)
              != ZLINK_SUBMIT_OK)
              return 15;

          zlink_actor_join_completion_t completion;
          memset (&completion, 0, sizeof (completion));
          {
              const int rc = wait_node_completion_kind_data (node, join_op, &completion,
                                                             sizeof (completion));
              if (rc != 0)
                  return rc;
          }
          if (completion.join_result != ZLINK_ACTOR_JOIN_ACCEPTED)
              return 16;
          if (completion.location.membership_epoch != 2)
              return 17;
          //  joined Spot is A's entry Spot (rid == A's node rid).
          if (completion.location.spot_rid.size != entry.routing_id.size
              || memcmp (completion.location.spot_rid.data, entry.routing_id.data,
                         entry.routing_id.size)
                   != 0)
              return 18;

          zlink_mesh_node_shutdown (node, 1000);
          if (zlink_mesh_node_destroy (&node) != ZLINK_CLOSE_OK)
              return 26;
          if (zlink_ctx_term (ctx) != 0)
              return 27;
          return 0;
      });
}
//  Transfer fence: the test process plays the deterministic location
//  authority. A fences and snapshots two pending actor messages, hands the
//  prepare result to B, B stages/commits/activates and A commits; the
//  messages surface exactly once on B in order.
void test_remote_actor_transfer_fence ()
{
    int endpoint_pipe[2];
    int authority_pipe[2]; //  A -> B: source prepare result
    int back_pipe[2];      //  B -> A: activation signal
    TEST_ASSERT_EQUAL_INT (0, pipe (endpoint_pipe));
    TEST_ASSERT_EQUAL_INT (0, pipe (authority_pipe));
    TEST_ASSERT_EQUAL_INT (0, pipe (back_pipe));

    fflush (NULL);
    const pid_t child = fork ();
    TEST_ASSERT_TRUE (child >= 0);

    if (child == 0) {
        //  child: node B (transfer target)
        close (endpoint_pipe[1]);
        close (authority_pipe[1]);
        close (back_pipe[0]);
        setup_test_environment (120);
        int rc = 0;
        do {
            char endpoint[ZLINK_MESH_ENDPOINT_MAX + 1];
            read_endpoint (endpoint_pipe[0], endpoint, sizeof (endpoint));
            if (endpoint[0] == '\0') { rc = 10; break; }
            void *ctx = zlink_ctx_new ();
            if (!ctx) { rc = 11; break; }
            void *node = new_started_node (ctx, mesh_name, "node-b");
            if (submit_peer_intent (node, endpoint) == 0) { rc = 12; break; }
            zlink_mesh_peer_entry_t entry;
            if (!wait_peer_state (node, ZLINK_MESH_PEER_ADMITTED, &entry)) { rc = 13; break; }

            //  authority record: generation, final_sequence, reserves.
            char line[128];
            read_endpoint (authority_pipe[0], line, sizeof (line));
            unsigned long long generation = 0, final_sequence = 0, reserve_messages = 0,
                               reserve_bytes = 0;
            if (sscanf (line, "%llu %llu %llu %llu", &generation, &final_sequence,
                        &reserve_messages, &reserve_bytes)
                != 4) { rc = 14; break; }

            zlink_actor_transfer_prepare_t prepare;
            memset (&prepare, 0, sizeof (prepare));
            prepare.struct_size = sizeof (prepare);
            prepare.version = 1;
            prepare.role = ZLINK_ACTOR_TRANSFER_TARGET;
            prepare.transfer_id.high = 7;
            prepare.transfer_id.low = 42;
            snprintf (prepare.actor.actor_id, sizeof (prepare.actor.actor_id), "worker-t");
            prepare.actor.generation = generation;
            prepare.actor.node_rid = entry.routing_id;
            prepare.expected_membership_epoch = 1;
            prepare.peer_node_rid = entry.routing_id;
            prepare.final_sequence = final_sequence;
            prepare.reserve_message_count = reserve_messages;
            prepare.reserve_byte_count = reserve_bytes;

            zlink_actor_transfer_token_t token;
            zlink_actor_transfer_prepare_result_t result;
            memset (&result, 0, sizeof (result));
            result.struct_size = sizeof (result);
            result.version = 1;
            if (zlink_mesh_node_actor_transfer_prepare (node, &prepare, 10000, &token, &result)
                != ZLINK_REQUEST_OK) { rc = 15; break; }
            if (result.final_sequence != final_sequence) { rc = 16; break; }

            //  activate before commit is rejected.
            if (zlink_mesh_node_actor_transfer_activate (&token) != ZLINK_CONFIG_INVALID_STATE
                || errno != EINVAL) { rc = 17; break; }
            //  commit with the wrong epoch is stale.
            if (zlink_mesh_node_actor_transfer_commit (&token, 5) != ZLINK_CONFIG_INVALID_STATE
                || errno != ESTALE) { rc = 18; break; }
            if (zlink_mesh_node_actor_transfer_commit (&token, 2) != ZLINK_CONFIG_OK) {
                rc = 19; break; }
            //  idempotent commit retry with the same epoch.
            if (zlink_mesh_node_actor_transfer_commit (&token, 2) != ZLINK_CONFIG_OK) {
                rc = 20; break; }
            if (zlink_mesh_node_actor_transfer_activate (&token) != ZLINK_CONFIG_OK) {
                rc = 21; break; }
            if (zlink_mesh_node_actor_transfer_activate (&token) != ZLINK_CONFIG_OK) {
                rc = 22; break; }
            //  abort after activation is a terminal-state violation.
            if (zlink_mesh_node_actor_transfer_abort (&token) != ZLINK_CONFIG_INVALID_STATE
                || errno != EALREADY) { rc = 23; break; }

            //  the transferred backlog surfaces in order on the actor lane.
            zlink_mesh_claim_t claim;
            take_ready_claim (node, ZLINK_MESH_OWNER_ACTOR, ZLINK_MESH_READY_APPLICATION,
                              &claim);
            void *batch = zlink_mesh_receive_batch_new (8, 32, 64 * 1024);
            zlink_mesh_receive_requirements_t requirements;
            memset (&requirements, 0, sizeof (requirements));
            if (!batch
                || zlink_mesh_claim_recv_batch (&claim, batch, &requirements,
                                                ZLINK_RECV_FLAGS_NONE)
                     != ZLINK_RECV_OK) { rc = 24; break; }
            if (zlink_mesh_receive_batch_count (batch) != 2) { rc = 25; break; }
            const zlink_mesh_receive_record_t *records = zlink_mesh_receive_batch_data (batch);
            const zlink_msg_t *parts = zlink_mesh_receive_batch_parts (batch);
            if (records[0].kind != ZLINK_MESH_RECORD_ACTOR_SEND
                || zlink_msg_size (&parts[records[0].part_offset]) != strlen ("t-1")
                || memcmp (zlink_msg_data (const_cast<zlink_msg_t *> (
                             &parts[records[0].part_offset])),
                           "t-1", 3)
                     != 0) { rc = 26; break; }
            if (records[1].kind != ZLINK_MESH_RECORD_ACTOR_SEND
                || zlink_msg_size (&parts[records[1].part_offset]) != strlen ("t-2")) {
                rc = 27; break; }
            zlink_mesh_claim_release (&claim);
            zlink_mesh_receive_batch_destroy (&batch);

            //  local lookup observes the installed epoch.
            zlink_actor_location_t location;
            memset (&location, 0, sizeof (location));
            location.struct_size = sizeof (location);
            location.version = 1;
            if (zlink_mesh_node_actor_lookup (node, "worker-t", &location) != ZLINK_CONFIG_OK
                || location.membership_epoch != 2) { rc = 28; break; }

            //  signal the authority that activation completed.
            if (write (back_pipe[1], "A\n", 2) != 2) { rc = 29; break; }

            zlink_mesh_node_shutdown (node, 1000);
            if (zlink_mesh_node_destroy (&node) != ZLINK_CLOSE_OK) { rc = 30; break; }
            if (zlink_ctx_term (ctx) != 0) { rc = 31; break; }
        } while (0);
        close (endpoint_pipe[0]);
        close (authority_pipe[0]);
        close (back_pipe[1]);
        fflush (NULL);
        std::_Exit (rc);
    }

    //  parent: node A (transfer source and fake authority)
    close (endpoint_pipe[0]);
    close (authority_pipe[0]);
    close (back_pipe[1]);

    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    void *node = new_started_node (ctx, mesh_name, "node-a");

    zlink_actor_ref_t actor;
    memset (&actor, 0, sizeof (actor));
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK,
                           zlink_mesh_node_actor_new (node, "worker-t", NULL, 0, &actor,
                                                      ZLINK_SEND_FLAGS_NONE, 1000));
    //  backlog: two pending messages become the frozen snapshot.
    for (int i = 1; i <= 2; ++i) {
        char text[8];
        snprintf (text, sizeof (text), "t-%d", i);
        zlink_msg_t part;
        make_payload (&part, text);
        TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_OK,
                               zlink_mesh_node_send_to_actor (node, &actor, NULL, &part, 1,
                                                              ZLINK_SEND_FLAGS_NONE));
        zlink_msg_close (&part);
    }

    publish_endpoint (node, endpoint_pipe[1]);
    TEST_ASSERT_TRUE_MESSAGE (wait_admitted_count (node, 1), "target peer not admitted");
    zlink_mesh_peer_entry_t entry;
    TEST_ASSERT_TRUE (wait_peer_state (node, ZLINK_MESH_PEER_ADMITTED, &entry));

    zlink_actor_transfer_prepare_t prepare;
    memset (&prepare, 0, sizeof (prepare));
    prepare.struct_size = sizeof (prepare);
    prepare.version = 1;
    prepare.role = ZLINK_ACTOR_TRANSFER_SOURCE;
    prepare.transfer_id.high = 7;
    prepare.transfer_id.low = 42;
    prepare.actor = actor;
    prepare.expected_membership_epoch = 1;
    prepare.peer_node_rid = entry.routing_id;

    zlink_actor_transfer_token_t token;
    zlink_actor_transfer_prepare_result_t result;
    memset (&result, 0, sizeof (result));
    result.struct_size = sizeof (result);
    result.version = 1;
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK,
                           zlink_mesh_node_actor_transfer_prepare (node, &prepare, 10000, &token,
                                                                   &result));
    TEST_ASSERT_EQUAL_UINT (2, result.final_sequence);
    TEST_ASSERT_EQUAL_UINT (2, result.reserve_message_count);

    //  the fence rejects new application traffic with backpressure.
    {
        zlink_msg_t part;
        make_payload (&part, "post-fence");
        const zlink_submit_result_t rc = zlink_mesh_node_send_to_actor (
          node, &actor, NULL, &part, 1, ZLINK_SEND_FLAGS_DONTWAIT);
        zlink_msg_close (&part);
        TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_BACKPRESSURED, rc);
    }

    //  authority record to the target.
    char line[128];
    snprintf (line, sizeof (line), "%llu %llu %llu %llu\n",
              (unsigned long long) actor.generation,
              (unsigned long long) result.final_sequence,
              (unsigned long long) result.reserve_message_count,
              (unsigned long long) result.reserve_byte_count);
    TEST_ASSERT_EQUAL_INT ((int) strlen (line), (int) write (authority_pipe[1], line,
                                                             strlen (line)));

    //  wait for the target activation signal, then source-commit.
    char ack[8];
    read_endpoint (back_pipe[0], ack, sizeof (ack));
    TEST_ASSERT_EQUAL_STRING ("A", ack);
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK, zlink_mesh_node_actor_transfer_commit (&token, 2));
    //  the old route is gone: local lookup no longer resolves the actor.
    {
        zlink_actor_location_t location;
        memset (&location, 0, sizeof (location));
        location.struct_size = sizeof (location);
        location.version = 1;
        TEST_ASSERT_TRUE (zlink_mesh_node_actor_lookup (node, "worker-t", &location)
                          != ZLINK_CONFIG_OK);
    }
    //  abort after source commit is a terminal-state violation.
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_INVALID_STATE,
                           zlink_mesh_node_actor_transfer_abort (&token));
    TEST_ASSERT_EQUAL_INT (EALREADY, errno);

    close (endpoint_pipe[1]);
    close (authority_pipe[1]);
    close (back_pipe[0]);

    int status = 0;
    pid_t wait_rc = 0;
    for (int waited = 0; waited < 30000; waited += poll_step_ms) {
        wait_rc = waitpid (child, &status, WNOHANG);
        TEST_ASSERT_TRUE (wait_rc >= 0);
        if (wait_rc == child)
            break;
        msleep (poll_step_ms);
    }
    if (wait_rc != child) {
        kill (child, SIGKILL);
        (void) waitpid (child, &status, 0);
        TEST_FAIL_MESSAGE ("transfer child did not exit in time");
    }
    TEST_ASSERT_TRUE_MESSAGE (WIFEXITED (status) && WEXITSTATUS (status) == 0,
                              "transfer child reported failure");

    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK, zlink_mesh_node_shutdown (node, 2000));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_node_destroy (&node));
    TEST_ASSERT_EQUAL_INT (0, zlink_ctx_term (ctx));
}
//  Drain and reconnect: an inbound peer's exit does not gate A's readiness,
//  and a restarted peer with the same RID is admitted again and can complete
//  a fresh round trip.
void test_peer_drain_and_reconnect ()
{
    //  Both children fork before the parent's node exists (the per-process
    //  MeshName registry is inherited across fork); each waits for the
    //  endpoint on its own pipe.
    int ep1[2];
    int ep2[2];
    TEST_ASSERT_EQUAL_INT (0, pipe (ep1));
    TEST_ASSERT_EQUAL_INT (0, pipe (ep2));

    fflush (NULL);
    const pid_t child1 = fork ();
    TEST_ASSERT_TRUE (child1 >= 0);
    if (child1 == 0) {
        close (ep1[1]);
        close (ep2[0]);
        close (ep2[1]);
        setup_test_environment (60);
        int rc = 0;
        char endpoint[ZLINK_MESH_ENDPOINT_MAX + 1];
        read_endpoint (ep1[0], endpoint, sizeof (endpoint));
        void *child_ctx = endpoint[0] ? zlink_ctx_new () : NULL;
        void *child_node = child_ctx ? new_started_node (child_ctx, mesh_name, "node-b") : NULL;
        if (!child_node || submit_peer_intent (child_node, endpoint) == 0)
            rc = 10;
        zlink_mesh_peer_entry_t entry;
        if (rc == 0 && !wait_peer_state (child_node, ZLINK_MESH_PEER_ADMITTED, &entry))
            rc = 11;
        if (child_node) {
            zlink_mesh_node_shutdown (child_node, 1000);
            zlink_mesh_node_destroy (&child_node);
        }
        if (child_ctx)
            zlink_ctx_term (child_ctx);
        close (ep1[0]);
        fflush (NULL);
        std::_Exit (rc);
    }

    fflush (NULL);
    const pid_t child2 = fork ();
    TEST_ASSERT_TRUE (child2 >= 0);
    if (child2 == 0) {
        close (ep1[0]);
        close (ep1[1]);
        close (ep2[1]);
        setup_test_environment (90);
        int rc = 0;
        char endpoint[ZLINK_MESH_ENDPOINT_MAX + 1];
        read_endpoint (ep2[0], endpoint, sizeof (endpoint));
        void *child_ctx = endpoint[0] ? zlink_ctx_new () : NULL;
        void *child_node = child_ctx ? new_started_node (child_ctx, mesh_name, "node-b") : NULL;
        if (!child_node || submit_peer_intent (child_node, endpoint) == 0)
            rc = 20;
        zlink_mesh_peer_entry_t entry;
        if (rc == 0 && !wait_peer_state (child_node, ZLINK_MESH_PEER_ADMITTED, &entry))
            rc = 21;
        if (rc == 0) {
            zlink_msg_t payload;
            if (zlink_msg_init_size (&payload, strlen ("re-ping")) != 0)
                rc = 22;
            else {
                memcpy (zlink_msg_data (&payload), "re-ping", strlen ("re-ping"));
                zlink_mesh_operation_id_t op;
                memset (&op, 0, sizeof (op));
                if (zlink_mesh_node_request_to_node (child_node, &entry.routing_id, NULL,
                                                     &payload, 1, &op, ZLINK_SEND_FLAGS_NONE,
                                                     10000)
                    != ZLINK_SUBMIT_OK) {
                    zlink_msg_close (&payload);
                    rc = 23;
                } else {
                    rc = wait_node_completion_payload (child_node, op, "re-pong");
                }
            }
        }
        if (child_node) {
            zlink_mesh_node_shutdown (child_node, 1000);
            zlink_mesh_node_destroy (&child_node);
        }
        if (child_ctx)
            zlink_ctx_term (child_ctx);
        close (ep2[0]);
        fflush (NULL);
        std::_Exit (rc);
    }

    close (ep1[0]);
    close (ep2[0]);

    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    void *node = new_started_node (ctx, mesh_name, "node-a");
    publish_endpoint (node, ep1[1]);

    TEST_ASSERT_TRUE_MESSAGE (wait_admitted_count (node, 1), "round-1 peer not admitted");
    //  Inbound peers never gate readiness: A stays READY throughout.
    TEST_ASSERT_TRUE (wait_node_state (node, ZLINK_MESH_NODE_READY));

    int child_status = 0;
    TEST_ASSERT_EQUAL_INT (child1, waitpid (child1, &child_status, 0));
    TEST_ASSERT_TRUE (WIFEXITED (child_status) && WEXITSTATUS (child_status) == 0);

    //  drain observed: the admitted count returns to zero while A's own
    //  readiness is unaffected.
    zlink_mesh_node_status_t status;
    bool drained = false;
    for (int waited = 0; waited < poll_deadline_ms && !drained; waited += poll_step_ms) {
        node_status (node, &status);
        if (status.admitted_peer_count == 0)
            drained = true;
        else
            msleep (poll_step_ms);
    }
    TEST_ASSERT_TRUE_MESSAGE (drained, "peer drain was not observed");
    node_status (node, &status);
    TEST_ASSERT_EQUAL_INT (ZLINK_MESH_NODE_READY, status.state);

    //  round 2: the restarted peer with the same RID is admitted again.
    publish_endpoint (node, ep2[1]);
    TEST_ASSERT_TRUE_MESSAGE (wait_admitted_count (node, 1), "round-2 peer not admitted");

    //  answer the round-2 request.
    zlink_mesh_claim_t claim;
    take_ready_claim (node, ZLINK_MESH_OWNER_NODE, ZLINK_MESH_READY_APPLICATION, &claim);
    void *batch = zlink_mesh_receive_batch_new (8, 32, 64 * 1024);
    TEST_ASSERT_NOT_NULL (batch);
    zlink_mesh_receive_requirements_t requirements;
    memset (&requirements, 0, sizeof (requirements));
    TEST_ASSERT_EQUAL_INT (ZLINK_RECV_OK,
                           zlink_mesh_claim_recv_batch (&claim, batch, &requirements,
                                                        ZLINK_RECV_FLAGS_NONE));
    const zlink_mesh_receive_record_t *record = zlink_mesh_receive_batch_data (batch);
    TEST_ASSERT_EQUAL_INT (ZLINK_MESH_RECORD_NODE_REQUEST, record->kind);
    zlink_msg_t reply;
    make_payload (&reply, "re-pong");
    TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_OK,
                           zlink_mesh_reply (&record->reply_token, &reply, 1,
                                             ZLINK_SEND_FLAGS_NONE));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_claim_release (&claim));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_receive_batch_destroy (&batch));

    child_status = 0;
    pid_t wait_rc = 0;
    for (int waited = 0; waited < 30000; waited += poll_step_ms) {
        wait_rc = waitpid (child2, &child_status, WNOHANG);
        TEST_ASSERT_TRUE (wait_rc >= 0);
        if (wait_rc == child2)
            break;
        msleep (poll_step_ms);
    }
    if (wait_rc != child2) {
        kill (child2, SIGKILL);
        (void) waitpid (child2, &child_status, 0);
        TEST_FAIL_MESSAGE ("round-2 child did not exit in time");
    }
    TEST_ASSERT_TRUE_MESSAGE (WIFEXITED (child_status) && WEXITSTATUS (child_status) == 0,
                              "round-2 child reported failure");

    close (ep1[1]);
    close (ep2[1]);
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK, zlink_mesh_node_shutdown (node, 2000));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_node_destroy (&node));
    TEST_ASSERT_EQUAL_INT (0, zlink_ctx_term (ctx));
}
#endif

int main ()
{
    setup_test_environment (180);
    UNITY_BEGIN ();
#if !defined _WIN32
    RUN_TEST (test_peer_admission_readiness_and_weight_update);
    RUN_TEST (test_peer_mesh_name_mismatch_is_conflict);
    RUN_TEST (test_remote_node_request_reply_round_trip);
    RUN_TEST (test_remote_spot_direct_request_reply);
    RUN_TEST (test_remote_multicast_publish);
    RUN_TEST (test_remote_actor_lookup_messaging_destroy);
    RUN_TEST (test_remote_actor_join_entry_spot);
    RUN_TEST (test_remote_actor_transfer_fence);
    RUN_TEST (test_peer_drain_and_reconnect);
#endif
    return UNITY_END ();
}
