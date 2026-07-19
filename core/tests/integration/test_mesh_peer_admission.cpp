/* SPDX-License-Identifier: MPL-2.0 */

//  Two-process MeshNode peer contract coverage: HELLO admission, readiness
//  transitions, weight update propagation, MeshName conflict rejection and a
//  remote node request/reply round trip. Each case forks before any context
//  exists and hands the parent's resolved endpoint to the child over a pipe.

#include "../testutil_unity.hpp"

#include <string.h>

#if !defined _WIN32
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

SETUP_TEARDOWN_TESTCONTEXT

//  Test-only hook (socket_submit_retry_fault_injection.cpp): the next count_
//  wire sends fail with err_.
extern "C" void zlink_test_set_submit_retry_fault (int count_, int err_);
extern "C" void zlink_test_set_mesh_alloc_fault (int count_);
extern "C" void zlink_test_mesh_inject_disconnect (void *mesh_node_,
                                                    const zlink_routing_id_t *peer_rid_,
                                                    uint64_t connection_id_);
extern "C" uint64_t zlink_test_mesh_select_admission_transport (
  int is_hello_, uint64_t previous_connection_id_,
  uint64_t current_connection_id_);
extern "C" int zlink_test_mesh_duplicate_admitted_lifetime (
  int is_hello_, uint64_t existing_generation_,
  uint64_t incoming_generation_);
extern "C" int zlink_test_mesh_transport_ready_transition (
  uint64_t previous_connection_id_, uint64_t ready_connection_id_);

#if !defined _WIN32
namespace
{
const char mesh_name[] = "mesh-admission";
const char channel_name[] = "orders";
const int poll_deadline_ms = 15000;
const int poll_step_ms = 20;

void test_hello_before_ready_does_not_copy_predecessor_transport ()
{
    TEST_ASSERT_EQUAL_INT (
      1, zlink_test_mesh_duplicate_admitted_lifetime (1, 5, 5));
    TEST_ASSERT_EQUAL_INT (
      0, zlink_test_mesh_duplicate_admitted_lifetime (1, 5, 6));
    TEST_ASSERT_EQUAL_INT (
      0, zlink_test_mesh_duplicate_admitted_lifetime (0, 5, 5));
    TEST_ASSERT_EQUAL_UINT64 (
      0, zlink_test_mesh_select_admission_transport (1, 41, 41));
    TEST_ASSERT_EQUAL_UINT64 (
      42, zlink_test_mesh_select_admission_transport (1, 41, 42));
    TEST_ASSERT_EQUAL_UINT64 (
      41, zlink_test_mesh_select_admission_transport (0, 41, 41));
}

void test_ready_handover_reconnects_before_delayed_disconnect ()
{
    //  A READY edge for the current pipe is only an idempotent observation.
    TEST_ASSERT_EQUAL_INT (
      0, zlink_test_mesh_transport_ready_transition (41, 41));
    //  HELLO-before-READY fills the missing physical identity without
    //  restarting admission.
    TEST_ASSERT_EQUAL_INT (
      0, zlink_test_mesh_transport_ready_transition (0, 42));
    //  A different READY identity means ROUTER has already handed the RID to
    //  the replacement pipe. Its same-generation HELLO must be eligible
    //  before the predecessor's delayed disconnect arrives.
    TEST_ASSERT_EQUAL_INT (
      1, zlink_test_mesh_transport_ready_transition (41, 42));
}

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

void *new_started_node (void *ctx_,
                        const char *mesh_name_,
                        const char *rid_,
                        uint64_t mailbox_message_budget_ = 0,
                        int router_hwm_ = 0,
                        bool infinite_send_timeout_ = false)
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
    if (mailbox_message_budget_ != 0) {
        TEST_ASSERT_EQUAL_INT (
          ZLINK_CONFIG_OK,
          zlink_set_mesh_node_option (node, ZLINK_MESH_NODE_OPT_MAILBOX_MESSAGE_BUDGET,
                                      &mailbox_message_budget_,
                                      sizeof (mailbox_message_budget_)));
    }
    if (router_hwm_ > 0) {
        TEST_ASSERT_EQUAL_INT (
          ZLINK_CONFIG_OK,
          zlink_set_mesh_node_option (node, ZLINK_MESH_NODE_OPT_ROUTER_HWM,
                                      &router_hwm_, sizeof (router_hwm_)));
    }
    if (infinite_send_timeout_) {
        const int send_timeout_ms = -1;
        TEST_ASSERT_EQUAL_INT (
          ZLINK_CONFIG_OK,
          zlink_set_option (node, ZLINK_OPT_SNDTIMEO, &send_timeout_ms,
                            sizeof (send_timeout_ms)));
    }
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

int receive_exact_node_records (void *node_, size_t expected_count_, int wait_ms_)
{
    void *ready = zlink_mesh_ready_batch_new (8);
    void *batch = zlink_mesh_receive_batch_new (16, 32, 64 * 1024);
    if (!ready || !batch)
        return -1;
    size_t total = 0;
    for (int waited = 0; waited < wait_ms_; waited += poll_step_ms) {
        uint32_t residue = 0;
        const zlink_recv_result_t ready_rc = zlink_mesh_node_drain_ready (
          node_, ZLINK_MESH_READY_APPLICATION, ready, &residue, ZLINK_RECV_FLAGS_DONTWAIT);
        if (ready_rc == ZLINK_RECV_NO_DATA) {
            if (total == expected_count_ && expected_count_ != 0)
                break;
            msleep (poll_step_ms);
            continue;
        }
        if (ready_rc != ZLINK_RECV_OK) {
            total = static_cast<size_t> (-1);
            break;
        }
        const zlink_mesh_ready_record_t *records = zlink_mesh_ready_batch_data (ready);
        const size_t ready_count = zlink_mesh_ready_batch_count (ready);
        bool claimed = false;
        zlink_mesh_claim_t claim;
        for (size_t i = 0; i < ready_count; ++i) {
            if (records[i].owner_kind != ZLINK_MESH_OWNER_NODE)
                continue;
            if (zlink_mesh_ready_batch_take_claim (ready, i, &claim) != ZLINK_CONFIG_OK) {
                total = static_cast<size_t> (-1);
                break;
            }
            claimed = true;
            break;
        }
        zlink_mesh_ready_batch_reset (ready);
        if (!claimed)
            continue;
        zlink_mesh_receive_requirements_t requirements;
        memset (&requirements, 0, sizeof (requirements));
        requirements.struct_size = sizeof (requirements);
        requirements.version = 1;
        if (zlink_mesh_claim_recv_batch (&claim, batch, &requirements, ZLINK_RECV_FLAGS_NONE)
            != ZLINK_RECV_OK) {
            zlink_mesh_claim_release (&claim);
            total = static_cast<size_t> (-1);
            break;
        }
        total += zlink_mesh_receive_batch_count (batch);
        zlink_mesh_claim_release (&claim);
        zlink_mesh_receive_batch_reset (batch);
        if (total > expected_count_)
            break;
    }
    zlink_mesh_ready_batch_destroy (&ready);
    zlink_mesh_receive_batch_destroy (&batch);
    return total == expected_count_ ? 0 : -1;
}

void write_result_line (int fd_, int value_)
{
    char text[32];
    const int size = snprintf (text, sizeof (text), "%d\n", value_);
    if (size > 0 && write (fd_, text, static_cast<size_t> (size)) != size)
        std::_Exit (98);
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
//  fails the test if the child does not exit 0 before the deadline. The local
//  socket is bidirectional so lifecycle tests can add explicit phase barriers
//  without relying on sleeps.
template <typename ParentFn, typename ChildFn>
void run_two_process_case (ParentFn parent_fn_, ChildFn child_fn_)
{
    int endpoint_pipe[2];
    TEST_ASSERT_EQUAL_INT (
      0, socketpair (AF_UNIX, SOCK_STREAM, 0, endpoint_pipe));

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
    parent_fn_ (endpoint_pipe[1], child);
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
    if (!WIFEXITED (status) || WEXITSTATUS (status) != 0) {
        char failure[96];
        snprintf (failure, sizeof (failure),
                  "peer child process reported failure (exit=%d, signal=%d)",
                  WIFEXITED (status) ? WEXITSTATUS (status) : -1,
                  WIFSIGNALED (status) ? WTERMSIG (status) : 0);
        TEST_FAIL_MESSAGE (failure);
    }
}
} // namespace

//  B connects to A; both must observe ADMITTED with the advertised channel
//  descriptor, B reaches READY, and A's weight change propagates to B as a
//  higher descriptor revision.
void test_peer_admission_readiness_and_weight_update ()
{
    run_two_process_case (
      //  parent: node A
      [] (int endpoint_fd, pid_t) {
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

//  A delayed disconnect from an older physical transport may carry the same
//  RID as the currently admitted peer. It must not close that newer lifetime.
void test_stale_transport_disconnect_preserves_admitted_successor ()
{
    run_two_process_case (
      [] (int endpoint_fd, pid_t) {
          void *ctx = zlink_ctx_new ();
          TEST_ASSERT_NOT_NULL (ctx);
          void *node = new_started_node (ctx, mesh_name, "node-a");
          publish_endpoint (node, endpoint_fd);

          zlink_mesh_peer_entry_t admitted;
          TEST_ASSERT_TRUE_MESSAGE (
            wait_peer_state (node, ZLINK_MESH_PEER_ADMITTED, &admitted),
            "node A did not admit the peer before stale disconnect injection");

          zlink_test_mesh_inject_disconnect (
            node, &admitted.routing_id, UINT64_MAX);

          zlink_mesh_peer_entry_t after;
          TEST_ASSERT_TRUE_MESSAGE (
            wait_peer_state (node, ZLINK_MESH_PEER_ADMITTED, &after),
            "a stale physical transport disconnect closed the admitted successor");
          TEST_ASSERT_EQUAL_UINT64 (admitted.lifecycle_generation,
                                    after.lifecycle_generation);

          //  Let the child close first so both contexts follow the normal
          //  peer-drain order used by the other two-process lifecycle cases.
          msleep (1500);
          TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK,
                                 zlink_mesh_node_shutdown (node, 1000));
          TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK,
                                 zlink_mesh_node_destroy (&node));
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
          void *node = new_started_node (ctx, mesh_name, "node-b");
          if (submit_peer_intent (node, endpoint) == 0)
              return 12;
          zlink_mesh_peer_entry_t admitted;
          if (!wait_peer_state (node, ZLINK_MESH_PEER_ADMITTED, &admitted))
              return 13;
          msleep (1000);
          zlink_mesh_node_shutdown (node, 1000);
          const int destroy_rc = zlink_mesh_node_destroy (&node);
          const int term_rc = zlink_ctx_term (ctx);
          return destroy_rc == ZLINK_CLOSE_OK && term_rc == 0 ? 0 : 14;
      });
}

//  Disconnecting an admitted outbound peer retires its transport and
//  connector. A new intent for the same endpoint must establish a fresh
//  admission within the same MeshNode lifetime.
void test_outbound_disconnect_then_reconnect_same_endpoint ()
{
    run_two_process_case (
      [] (int endpoint_fd, pid_t) {
          void *ctx = zlink_ctx_new ();
          TEST_ASSERT_NOT_NULL (ctx);
          void *node = new_started_node (ctx, mesh_name, "node-a");
          publish_endpoint (node, endpoint_fd);

          TEST_ASSERT_TRUE_MESSAGE (wait_admitted_count (node, 1),
                                    "node A did not observe the first admission");
          const char first_observed = '1';
          TEST_ASSERT_EQUAL_INT (
            1, static_cast<int> (write (endpoint_fd, &first_observed, 1)));
          char reconnect_complete = 0;
          TEST_ASSERT_EQUAL_INT (
            1, static_cast<int> (read (endpoint_fd, &reconnect_complete, 1)));
          TEST_ASSERT_EQUAL_INT ('2', reconnect_complete);
          TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK,
                                 zlink_mesh_node_shutdown (node, 1000));
          TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK,
                                 zlink_mesh_node_destroy (&node));
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
          void *node = new_started_node (ctx, mesh_name, "node-b");
          const uint64_t first_intent_id = submit_peer_intent (node, endpoint);
          if (first_intent_id == 0)
              return 12;

          zlink_mesh_peer_entry_t first;
          if (!wait_peer_state (node, ZLINK_MESH_PEER_ADMITTED, &first))
              return 13;
          char first_observed = 0;
          if (read (endpoint_fd, &first_observed, 1) != 1
              || first_observed != '1')
              return 20;
          //  Intent removal is only for a connection that has not completed
          //  admission. An admitted lifetime must be selected by RID and
          //  generation so a reused intent id cannot close the wrong peer.
          if (zlink_mesh_node_remove_peer_connection (node, first_intent_id)
                != ZLINK_CONNECT_BUSY
              || zlink_errno () != EBUSY)
              return 19;
          if (zlink_mesh_node_disconnect_peer (
                node, &first.routing_id, first.lifecycle_generation)
              != ZLINK_CONNECT_OK)
              return 14;
          if (submit_peer_intent (node, endpoint) == 0)
              return 15;

          zlink_mesh_peer_entry_t second;
          if (!wait_peer_state (node, ZLINK_MESH_PEER_ADMITTED, &second))
              return 16;
          if (second.connection_intent_id == first.connection_intent_id)
              return 17;
          const char reconnect_complete = '2';
          if (write (endpoint_fd, &reconnect_complete, 1) != 1)
              return 21;

          zlink_mesh_node_shutdown (node, 1000);
          const int destroy_rc = zlink_mesh_node_destroy (&node);
          const int term_rc = zlink_ctx_term (ctx);
          return destroy_rc == ZLINK_CLOSE_OK && term_rc == 0 ? 0 : 18;
      });
}

//  A different MeshName must be rejected during admission: the intent ends in
//  ERROR with a conflict errno and the connecting node stays PARTIAL_READY.
void test_peer_mesh_name_mismatch_is_conflict ()
{
    run_two_process_case (
      [] (int endpoint_fd, pid_t) {
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

          zlink_mesh_monitor_open_options_t monitor_options;
          memset (&monitor_options, 0, sizeof (monitor_options));
          monitor_options.struct_size = sizeof (monitor_options);
          monitor_options.version = 1;
          void *monitor = zlink_mesh_node_monitor_open (node, &monitor_options);
          if (!monitor)
              return 18;

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

          //  The rejected side reports PEER_REJECTED with the admission
          //  conflict result and errno.
          int rejected_seen = 0;
          for (int waited = 0; waited < poll_deadline_ms && !rejected_seen;
               waited += poll_step_ms) {
              zlink_mesh_monitor_event_t event;
              memset (&event, 0, sizeof (event));
              event.struct_size = sizeof (event);
              event.version = 1;
              const zlink_recv_result_t rc =
                zlink_mesh_node_monitor_recv (monitor, &event, ZLINK_RECV_FLAGS_DONTWAIT);
              if (rc != ZLINK_RECV_OK) {
                  msleep (poll_step_ms);
                  continue;
              }
              if (event.kind == ZLINK_MESH_MONITOR_PEER_REJECTED) {
                  if (event.failure_errno != EEXIST)
                      return 19;
                  if (event.result_code != ZLINK_CONNECT_CONFLICT)
                      return 20;
                  rejected_seen = 1;
              }
          }
          if (!rejected_seen)
              return 21;

          if (zlink_mesh_node_monitor_close (&monitor) != ZLINK_CLOSE_OK)
              return 22;
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
      [] (int endpoint_fd, pid_t) {
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
      [] (int endpoint_fd, pid_t) {
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

//  A publishes a multicast; the remote subscriber Spot on B and the
//  publish detail counts observe the remote target.
void test_remote_multicast_publish ()
{
    int completion_pipe[2];
    TEST_ASSERT_EQUAL_INT (0, pipe (completion_pipe));
    run_two_process_case (
      //  parent: node A publishes after B is admitted.
      [&] (int endpoint_fd, pid_t) {
          close (completion_pipe[1]);
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
          //  Force allocation failure after the ROUTER envelope MORE frame.
          //  The following successful publish proves that the failed
          //  multipart scope was rolled back instead of contaminating it.
          zlink_msg_t failed_part;
          make_payload (&failed_part, "discarded-market-tick");
          zlink_mesh_publish_detail_t failed_detail;
          memset (&failed_detail, 0, sizeof (failed_detail));
          failed_detail.struct_size = sizeof (failed_detail);
          failed_detail.version = 1;
          zlink_test_set_mesh_alloc_fault (1);
          TEST_ASSERT_EQUAL_INT (
            ZLINK_SUBMIT_OUT_OF_MEMORY,
            zlink_mesh_node_publisher_publish (publisher, channel_name, "ticks.krw", NULL,
                                               &failed_part, 1, &failed_detail,
                                               ZLINK_SEND_FLAGS_NONE));
          zlink_test_set_mesh_alloc_fault (0);
          zlink_msg_close (&failed_part);
          TEST_ASSERT_EQUAL_UINT (1, failed_detail.snapshot_remote_target_count);
          TEST_ASSERT_EQUAL_UINT (0, failed_detail.admitted_remote_target_count);
          TEST_ASSERT_EQUAL_UINT (1, failed_detail.dropped_remote_target_count);

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

          make_payload (&part, "dropped-market-tick");
          TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_OK,
                                 zlink_mesh_node_publisher_publish (publisher, channel_name,
                                                                    "ticks.krw", NULL, &part, 1,
                                                                    &detail,
                                                                    ZLINK_SEND_FLAGS_NONE));
          zlink_msg_close (&part);
          TEST_ASSERT_EQUAL_UINT (1, detail.snapshot_remote_target_count);
          TEST_ASSERT_EQUAL_UINT (1, detail.admitted_remote_target_count);
          TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK,
                                 zlink_mesh_node_publisher_destroy (&publisher));

          //  Keep A connected until B has checked both ingress records and
          //  closed its node. The raw context contract permits infinite linger
          //  for an internal ROUTER with an active reconnect pipe.
          unsigned char completion = 0;
          TEST_ASSERT_EQUAL_INT (
            1, static_cast<int> (read (completion_pipe[0], &completion, 1)));
          TEST_ASSERT_EQUAL_HEX8 (0xA5, completion);
          close (completion_pipe[0]);
          TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK, zlink_mesh_node_shutdown (node, 2000));
          TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_node_destroy (&node));
          TEST_ASSERT_EQUAL_INT (0, zlink_ctx_term (ctx));
      },
      //  child: node B subscribes and drains the multicast record.
      [&] (int endpoint_fd) -> int {
          close (completion_pipe[0]);
          char endpoint[ZLINK_MESH_ENDPOINT_MAX + 1];
          read_endpoint (endpoint_fd, endpoint, sizeof (endpoint));
          if (endpoint[0] == '\0')
              return 10;

          void *ctx = zlink_ctx_new ();
          if (!ctx)
              return 11;
          void *node = new_started_node (ctx, mesh_name, "node-b", 1);

          zlink_mesh_monitor_open_options_t monitor_options;
          memset (&monitor_options, 0, sizeof (monitor_options));
          monitor_options.struct_size = sizeof (monitor_options);
          monitor_options.version = 1;
          void *monitor = zlink_mesh_node_monitor_open (node, &monitor_options);
          if (!monitor)
              return 12;

          void *spot = NULL;
          if (zlink_mesh_node_entry_spot (node, &spot) != ZLINK_CONFIG_OK)
              return 13;
          if (zlink_spot_set_subscription (spot, channel_name, "ticks.",
                                           ZLINK_SPOT_SUBSCRIPTION_PREFIX)
              != ZLINK_CONFIG_OK)
              return 14;

          if (submit_peer_intent (node, endpoint) == 0)
              return 15;
          zlink_mesh_peer_entry_t entry;
          if (!wait_peer_state (node, ZLINK_MESH_PEER_ADMITTED, &entry))
              return 16;
          //  Keep the one-record mailbox occupied until both successful
          //  publishes have reached this node.
          msleep (1000);

          //  Drain the multicast record from the entry Spot's application
          //  lane.
          void *ready = zlink_mesh_ready_batch_new (8);
          void *batch = zlink_mesh_receive_batch_new (8, 32, 64 * 1024);
          if (!ready || !batch)
              return 17;
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

          //  Duplicate guard: one publish delivers exactly once. No second
          //  multicast record may surface after the first turn.
          if (result == 0) {
              for (int waited = 0; waited < 400; waited += poll_step_ms) {
                  uint32_t residue = 0;
                  const zlink_recv_result_t rc =
                    zlink_mesh_node_drain_ready (node, ZLINK_MESH_READY_APPLICATION, ready,
                                                 &residue, ZLINK_RECV_FLAGS_DONTWAIT);
                  if (rc == ZLINK_RECV_OK && zlink_mesh_ready_batch_count (ready) > 0) {
                      result = 28;
                      break;
                  }
                  zlink_mesh_ready_batch_reset (ready);
                  msleep (poll_step_ms);
              }
          }

          //  The ingress-side aggregate event reports the complete local
          //  target grid for the second publish, without a per-target event.
          bool aggregate_drop_seen = false;
          for (int waited = 0; result == 0 && waited < 2000; waited += poll_step_ms) {
              zlink_mesh_monitor_event_t event;
              memset (&event, 0, sizeof (event));
              event.struct_size = sizeof (event);
              event.version = 1;
              const zlink_recv_result_t rc =
                zlink_mesh_node_monitor_recv (monitor, &event,
                                              ZLINK_RECV_FLAGS_DONTWAIT);
              if (rc == ZLINK_RECV_NO_DATA) {
                  msleep (poll_step_ms);
                  continue;
              }
              if (rc != ZLINK_RECV_OK) {
                  result = 29;
                  break;
              }
              if (event.kind == ZLINK_MESH_MONITOR_BACKPRESSURED) {
                  result = 31;
                  break;
              }
              if (event.kind == ZLINK_MESH_MONITOR_MULTICAST_DROPPED) {
                  if (event.snapshot_local_spot_count != 1
                      || event.admitted_local_spot_count != 0
                      || event.dropped_local_spot_count != 1)
                      result = 32;
                  aggregate_drop_seen = true;
                  break;
              }
          }
          if (result == 0 && !aggregate_drop_seen)
              result = 33;

          zlink_mesh_ready_batch_destroy (&ready);
          zlink_mesh_receive_batch_destroy (&batch);
          zlink_spot_destroy (&spot);
          zlink_mesh_node_monitor_close (&monitor);
          zlink_mesh_node_shutdown (node, 1000);
          if (zlink_mesh_node_destroy (&node) != ZLINK_CLOSE_OK && result == 0)
              result = 26;
          if (zlink_ctx_term (ctx) != 0 && result == 0)
              result = 27;
          const unsigned char completion = 0xA5;
          if (write (completion_pipe[1], &completion, 1) != 1 && result == 0)
              result = 34;
          close (completion_pipe[1]);
          return result;
      });
}

//  Shutdown interrupts an infinite-timeout blocking ROUTER send. The child
//  process is stopped after admission so its transport cannot drain; with a
//  one-message ROUTER HWM, the publisher must eventually hold the wire send
//  scope until wire_stop cancels it. A concurrent descriptor update must not
//  retain the node mutex while waiting for that wire send scope.
void test_shutdown_interrupts_infinite_blocking_mesh_send ()
{
    int ready_pipe[2];
    int resume_pipe[2];
    TEST_ASSERT_EQUAL_INT (0, pipe (ready_pipe));
    TEST_ASSERT_EQUAL_INT (0, pipe (resume_pipe));

    run_two_process_case (
      [&] (int endpoint_fd, pid_t child_pid) {
          close (ready_pipe[1]);
          close (resume_pipe[0]);

          void *ctx = zlink_ctx_new ();
          TEST_ASSERT_NOT_NULL (ctx);
          TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK,
                                 zlink_ctx_set (ctx, ZLINK_CTX_OPT_BLOCKY, 0));
          void *node =
            new_started_node (ctx, mesh_name, "node-a", 0, 1, true);
          publish_endpoint (node, endpoint_fd);
          TEST_ASSERT_TRUE_MESSAGE (wait_admitted_count (node, 1),
                                    "node A did not admit the stalled peer");

          unsigned char child_ready = 0;
          TEST_ASSERT_EQUAL_INT (
            1, static_cast<int> (read (ready_pipe[0], &child_ready, 1)));
          TEST_ASSERT_EQUAL_HEX8 (0xA5, child_ready);
          close (ready_pipe[0]);

          TEST_ASSERT_EQUAL_INT (0, kill (child_pid, SIGSTOP));
          int stopped_status = 0;
          TEST_ASSERT_EQUAL_INT (child_pid,
                                 waitpid (child_pid, &stopped_status, WUNTRACED));
          TEST_ASSERT_TRUE (WIFSTOPPED (stopped_status));

          void *publisher = zlink_mesh_node_publisher_new (node);
          TEST_ASSERT_NOT_NULL (publisher);
          zlink_msg_t part;
          const size_t payload_size = 4 * 1024 * 1024;
          TEST_ASSERT_EQUAL_INT (0, zlink_msg_init_size (&part, payload_size));
          memset (zlink_msg_data (&part), 0x5A, payload_size);

          std::atomic<uint32_t> started (0);
          std::atomic<uint32_t> completed (0);
          std::atomic<bool> in_publish (false);
          std::atomic<int> terminal_result (ZLINK_SUBMIT_OK);
          std::thread sender ([&] {
              for (;;) {
                  zlink_mesh_publish_detail_t detail;
                  memset (&detail, 0, sizeof (detail));
                  detail.struct_size = sizeof (detail);
                  detail.version = 1;
                  started.fetch_add (1, std::memory_order_release);
                  in_publish.store (true, std::memory_order_release);
                  const zlink_submit_result_t rc =
                    zlink_mesh_node_publisher_publish (
                      publisher, channel_name, "blocked.remote", NULL, &part, 1,
                      &detail, ZLINK_SEND_FLAGS_NONE);
                  in_publish.store (false, std::memory_order_release);
                  if (rc != ZLINK_SUBMIT_OK) {
                      terminal_result.store (rc, std::memory_order_release);
                      return;
                  }
                  completed.fetch_add (1, std::memory_order_release);
              }
          });

          bool blocked = false;
          uint32_t previous_completed = completed.load (std::memory_order_acquire);
          int stable_turns = 0;
          for (int waited = 0; waited < 5000; waited += poll_step_ms) {
              const uint32_t current_completed =
                completed.load (std::memory_order_acquire);
              const bool call_in_progress =
                in_publish.load (std::memory_order_acquire)
                && started.load (std::memory_order_acquire) > current_completed;
              if (current_completed > 0 && current_completed == previous_completed
                  && call_in_progress)
                  ++stable_turns;
              else
                  stable_turns = 0;
              if (stable_turns >= 10) {
                  blocked = true;
                  break;
              }
              previous_completed = current_completed;
              msleep (poll_step_ms);
          }
          TEST_ASSERT_TRUE_MESSAGE (blocked,
                                    "blocking publish did not reach ROUTER backpressure");

          std::atomic<bool> update_started (false);
          std::atomic<int> update_result (-1);
          std::thread updater ([&] {
              update_started.store (true, std::memory_order_release);
              update_result.store (
                zlink_mesh_node_set_channel_weight (node, channel_name, 99),
                std::memory_order_release);
          });
          while (!update_started.load (std::memory_order_acquire))
              std::this_thread::yield ();
          //  Give the updater time to reach the wire mutex. An implementation
          //  that sends while retaining node->mutex now deadlocks shutdown.
          msleep (200);

          std::atomic<bool> shutdown_done (false);
          std::atomic<bool> watchdog_resumed_child (false);
          std::atomic<int> resume_write_result (-1);
          std::atomic<int> resume_kill_result (-1);
          std::thread shutdown_watchdog ([&] {
              for (int waited = 0; waited < 1500; waited += poll_step_ms) {
                  if (shutdown_done.load (std::memory_order_acquire))
                      return;
                  msleep (poll_step_ms);
              }
              const unsigned char resume = 0x5A;
              resume_write_result.store (
                static_cast<int> (write (resume_pipe[1], &resume, 1)),
                std::memory_order_release);
              close (resume_pipe[1]);
              resume_kill_result.store (kill (child_pid, SIGCONT),
                                        std::memory_order_release);
              watchdog_resumed_child.store (true, std::memory_order_release);
          });

          const std::chrono::steady_clock::time_point shutdown_started =
            std::chrono::steady_clock::now ();
          const zlink_request_result_t shutdown_result =
            zlink_mesh_node_shutdown (node, 2000);
          const int64_t shutdown_ms =
            std::chrono::duration_cast<std::chrono::milliseconds> (
              std::chrono::steady_clock::now () - shutdown_started)
              .count ();
          shutdown_done.store (true, std::memory_order_release);
          shutdown_watchdog.join ();
          sender.join ();
          updater.join ();

          if (!watchdog_resumed_child.load (std::memory_order_acquire)) {
              const unsigned char resume = 0x5A;
              resume_write_result.store (
                static_cast<int> (write (resume_pipe[1], &resume, 1)),
                std::memory_order_release);
              close (resume_pipe[1]);
              resume_kill_result.store (kill (child_pid, SIGCONT),
                                        std::memory_order_release);
          }

          TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK, shutdown_result);
          TEST_ASSERT_TRUE_MESSAGE (shutdown_ms < 1500,
                                    "shutdown did not interrupt the blocking send");
          TEST_ASSERT_FALSE_MESSAGE (
            watchdog_resumed_child.load (std::memory_order_acquire),
            "descriptor update retained the node mutex and blocked shutdown");
          TEST_ASSERT_NOT_EQUAL (ZLINK_SUBMIT_OK,
                                 terminal_result.load (std::memory_order_acquire));
          TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK,
                                 update_result.load (std::memory_order_acquire));
          TEST_ASSERT_EQUAL_INT (1,
                                 resume_write_result.load (std::memory_order_acquire));
          TEST_ASSERT_EQUAL_INT (0,
                                 resume_kill_result.load (std::memory_order_acquire));

          zlink_msg_close (&part);
          TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK,
                                 zlink_mesh_node_publisher_destroy (&publisher));
          TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_node_destroy (&node));
          TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_ctx_term (ctx));
      },
      [&] (int endpoint_fd) -> int {
          close (ready_pipe[0]);
          close (resume_pipe[1]);

          char endpoint[ZLINK_MESH_ENDPOINT_MAX + 1];
          read_endpoint (endpoint_fd, endpoint, sizeof (endpoint));
          if (endpoint[0] == '\0')
              return 10;

          void *ctx = zlink_ctx_new ();
          if (!ctx)
              return 11;
          if (zlink_ctx_set (ctx, ZLINK_CTX_OPT_BLOCKY, 0) != ZLINK_CONFIG_OK)
              return 12;
          void *node = new_started_node (ctx, mesh_name, "node-b", 0, 1);
          void *spot = NULL;
          if (zlink_mesh_node_entry_spot (node, &spot) != ZLINK_CONFIG_OK)
              return 13;
          if (zlink_spot_set_subscription (spot, channel_name, "blocked.",
                                           ZLINK_SPOT_SUBSCRIPTION_PREFIX)
              != ZLINK_CONFIG_OK)
              return 14;
          if (submit_peer_intent (node, endpoint) == 0)
              return 15;
          zlink_mesh_peer_entry_t entry;
          if (!wait_peer_state (node, ZLINK_MESH_PEER_ADMITTED, &entry))
              return 16;

          const unsigned char child_ready = 0xA5;
          if (write (ready_pipe[1], &child_ready, 1) != 1)
              return 17;
          close (ready_pipe[1]);

          unsigned char resume = 0;
          if (read (resume_pipe[0], &resume, 1) != 1 || resume != 0x5A)
              return 18;
          close (resume_pipe[0]);

          int result = 0;
          if (zlink_spot_destroy (&spot) != ZLINK_CLOSE_OK)
              result = 19;
          if (zlink_mesh_node_shutdown (node, 1000) != ZLINK_REQUEST_OK
              && result == 0)
              result = 20;
          if (zlink_mesh_node_destroy (&node) != ZLINK_CLOSE_OK && result == 0)
              result = 21;
          if (zlink_ctx_term (ctx) != ZLINK_CLOSE_OK && result == 0)
              result = 22;
          return result;
      });
}
//  An admitted remote target that loses its ROUTER pipe during publish is
//  reported as unreachable while the independent local leg still delivers.
void test_router_unreachable_target_accounting ()
{
    run_two_process_case (
          //  parent: node A injects ROUTER send faults and checks that remote
          //  accounting does not roll back the independent local delivery.
      [] (int endpoint_fd, pid_t) {
          void *ctx = zlink_ctx_new ();
          TEST_ASSERT_NOT_NULL (ctx);
          void *node = new_started_node (ctx, mesh_name, "node-a");
          publish_endpoint (node, endpoint_fd);

          //  A local Spot match keeps the local leg observable alongside the
          //  injected remote departure.
          void *spot = NULL;
          TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK, zlink_mesh_node_entry_spot (node, &spot));
          TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK,
                                 zlink_spot_set_subscription (spot, channel_name, "ticks.",
                                                              ZLINK_SPOT_SUBSCRIPTION_PREFIX));

          TEST_ASSERT_TRUE_MESSAGE (wait_admitted_count (node, 1),
                                    "node A did not admit the subscriber peer");
          //  Give B a moment to install its subscription after admission.
          msleep (300);

          void *publisher = zlink_mesh_node_publisher_new (node);
          TEST_ASSERT_NOT_NULL (publisher);
          bool observed = false;
          for (int attempt = 0; attempt < 10 && !observed; ++attempt) {
              zlink_mesh_publish_detail_t detail;
              memset (&detail, 0, sizeof (detail));
              detail.struct_size = sizeof (detail);
              detail.version = 1;
              zlink_msg_t part;
              make_payload (&part, "market-tick");
              //  A concurrent control frame may consume the injected fault;
              //  that round observes a clean publish and retries.
              zlink_test_set_submit_retry_fault (1, ENOTCONN);
              const zlink_submit_result_t rc = zlink_mesh_node_publisher_publish (
                publisher, channel_name, "ticks.krw", NULL, &part, 1, &detail,
                ZLINK_SEND_FLAGS_DONTWAIT);
              zlink_test_set_submit_retry_fault (0, 0);
              zlink_msg_close (&part);
              TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_OK, rc);
              TEST_ASSERT_EQUAL_UINT (1, detail.snapshot_remote_target_count);
              TEST_ASSERT_EQUAL_UINT (0, detail.dropped_remote_target_count);
              TEST_ASSERT_EQUAL_UINT (detail.snapshot_remote_target_count,
                                      detail.admitted_remote_target_count
                                        + detail.dropped_remote_target_count
                                        + detail.unreachable_remote_target_count);
              TEST_ASSERT_EQUAL_UINT (1, detail.snapshot_local_spot_count);
              TEST_ASSERT_EQUAL_UINT (1, detail.admitted_local_spot_count);
              TEST_ASSERT_EQUAL_UINT (0, detail.dropped_local_spot_count);
              observed = detail.unreachable_remote_target_count == 1
                         && detail.admitted_remote_target_count == 0;
          }
          TEST_ASSERT_TRUE_MESSAGE (observed,
                                    "injected departure never surfaced as unreachable");

          observed = false;
          for (int attempt = 0; attempt < 10 && !observed; ++attempt) {
              zlink_mesh_publish_detail_t detail;
              memset (&detail, 0, sizeof (detail));
              detail.struct_size = sizeof (detail);
              detail.version = 1;
              zlink_msg_t part;
              make_payload (&part, "market-tick");
              zlink_test_set_submit_retry_fault (1, EAGAIN);
              const zlink_submit_result_t rc = zlink_mesh_node_publisher_publish (
                publisher, channel_name, "ticks.krw", NULL, &part, 1, &detail,
                ZLINK_SEND_FLAGS_DONTWAIT);
              zlink_test_set_submit_retry_fault (0, 0);
              zlink_msg_close (&part);
              if (rc == ZLINK_SUBMIT_BACKPRESSURED) {
                  TEST_ASSERT_EQUAL_UINT (1, detail.snapshot_remote_target_count);
                  TEST_ASSERT_EQUAL_UINT (0, detail.admitted_remote_target_count);
                  TEST_ASSERT_EQUAL_UINT (1, detail.dropped_remote_target_count);
                  TEST_ASSERT_EQUAL_UINT (0, detail.unreachable_remote_target_count);
                  TEST_ASSERT_EQUAL_UINT (1, detail.snapshot_local_spot_count);
                  TEST_ASSERT_EQUAL_UINT (1, detail.admitted_local_spot_count);
                  TEST_ASSERT_EQUAL_UINT (0, detail.dropped_local_spot_count);
                  observed = true;
              } else {
                  TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_OK, rc);
              }
          }
          TEST_ASSERT_TRUE_MESSAGE (
            observed, "injected ROUTER backpressure never reached publish result");

          TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_node_publisher_destroy (&publisher));
          TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_spot_destroy (&spot));

          //  Let the child exit first so parent teardown does not linger.
          msleep (1500);
          TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK, zlink_mesh_node_shutdown (node, 2000));
          TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_node_destroy (&node));
          TEST_ASSERT_EQUAL_INT (0, zlink_ctx_term (ctx));
      },
      //  child: node B subscribes, stays admitted through the parent's
      //  publish attempts and exits before the parent tears down.
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

          msleep (800);
          zlink_spot_destroy (&spot);
          zlink_mesh_node_shutdown (node, 1000);
          if (zlink_mesh_node_destroy (&node) != ZLINK_CLOSE_OK)
              return 16;
          if (zlink_ctx_term (ctx) != 0)
              return 17;
          return 0;
      });
}

//  B looks up A's actor over the wire, drives a request/reply round trip on
//  the actor lane and finally destroys it remotely.
void test_remote_actor_lookup_messaging_destroy ()
{
    run_two_process_case (
      //  parent: node A hosts actor "billing-1" and answers one request.
      [] (int endpoint_fd, pid_t) {
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
      [] (int endpoint_fd, pid_t) {
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

            //  Target readiness gives the source STREAM participant a
            //  private bounded allowance. Let the source admit one message,
            //  then start commit so the terminal high-water is sealed.
            if (write (back_pipe[1], "R\n", 2) != 2) { rc = 32; break; }
            char commit_line[8];
            read_endpoint (authority_pipe[0], commit_line, sizeof (commit_line));
            if (strcmp (commit_line, "C") != 0) { rc = 33; break; }

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
            if (zlink_mesh_receive_batch_count (batch) != 4) { rc = 25; break; }
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
            if (records[2].kind != ZLINK_MESH_RECORD_ACTOR_SEND
                || zlink_msg_size (&parts[records[2].part_offset])
                     != strlen ("session-post-fence")
                || memcmp (zlink_msg_data (const_cast<zlink_msg_t *> (
                             &parts[records[2].part_offset])),
                           "session-post-fence", strlen ("session-post-fence"))
                     != 0) {
                rc = 34; break;
            }
            if (records[3].kind != ZLINK_MESH_RECORD_ACTOR_REQUEST
                || zlink_msg_size (&parts[records[3].part_offset])
                     != strlen ("session-request")) {
                rc = 35; break;
            }
            zlink_msg_t session_reply;
            if (zlink_msg_init_size (&session_reply, strlen ("session-reply")) != 0) {
                rc = 36; break;
            }
            memcpy (zlink_msg_data (&session_reply), "session-reply",
                    strlen ("session-reply"));
            if (zlink_mesh_reply (&records[3].reply_token, &session_reply, 1,
                                  ZLINK_SEND_FLAGS_NONE)
                != ZLINK_SUBMIT_OK) {
                zlink_msg_close (&session_reply);
                rc = 37; break;
            }
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
    char stream_endpoint[ZLINK_MESH_ENDPOINT_MAX + 1];
    size_t stream_endpoint_size = sizeof (stream_endpoint);
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK,
      zlink_get_option (stream, ZLINK_OPT_LAST_ENDPOINT, stream_endpoint,
                        &stream_endpoint_size));
    stream_endpoint[sizeof (stream_endpoint) - 1] = '\0';
    void *session_service = zlink_stream_session_service_new (node, stream);
    TEST_ASSERT_NOT_NULL (session_service);
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK,
                           zlink_stream_session_service_start (session_service));
    const int stream_client_fd = connect_raw_stream_client (stream_endpoint);
    TEST_ASSERT_TRUE (stream_client_fd >= 0);
    TEST_ASSERT_EQUAL_INT (1, send (stream_client_fd, "!", 1, 0));

    zlink_msg_t connect_part;
    TEST_ASSERT_EQUAL_INT (0, zlink_msg_init (&connect_part));
    const zlink_routing_id_t *source_session_rid = NULL;
    zlink_part_flag_t has_more = ZLINK_PART_MORE;
    TEST_ASSERT_EQUAL_INT (
      ZLINK_RECV_OK,
      zlink_recv_part (stream, &source_session_rid, &connect_part, &has_more,
                       ZLINK_RECV_FLAGS_NONE));
    TEST_ASSERT_NOT_NULL (source_session_rid);
    TEST_ASSERT_EQUAL_INT (ZLINK_PART_FINAL, has_more);
    zlink_routing_id_t session_rid = *source_session_rid;
    zlink_msg_close (&connect_part);

    zlink_actor_ref_t actor;
    memset (&actor, 0, sizeof (actor));
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK,
                           zlink_mesh_node_actor_new (node, "worker-t", NULL, 0, &actor,
                                                      ZLINK_SEND_FLAGS_NONE, 1000));
    zlink_mesh_operation_id_t bind_operation;
    TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_OK,
                           zlink_stream_session_bind_actor (session_service, &session_rid, &actor,
                                                            &bind_operation, 1000));
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
    {
        zlink_msg_t part;
        make_payload (&part, "session-post-fence");
        const zlink_submit_result_t rc = zlink_stream_session_send_to_actor (
          session_service, &session_rid, &actor, NULL, &part, 1, ZLINK_SEND_FLAGS_DONTWAIT);
        zlink_msg_close (&part);
        TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_BACKPRESSURED, rc);
        TEST_ASSERT_EQUAL_INT (EAGAIN, errno);
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

    //  Target readiness installs a private allowance for the bound STREAM
    //  participant. The accepted message remains pending until transfer and
    //  is visible in the service status counters.
    char ack[8];
    read_endpoint (back_pipe[0], ack, sizeof (ack));
    TEST_ASSERT_EQUAL_STRING ("R", ack);
    {
        zlink_msg_t oversized;
        TEST_ASSERT_EQUAL_INT (0, zlink_msg_init_size (&oversized, 1024 * 1024 + 1));
        const zlink_submit_result_t oversized_result =
          zlink_stream_session_send_to_actor (session_service, &session_rid, &actor, NULL,
                                              &oversized, 1, ZLINK_SEND_FLAGS_DONTWAIT);
        zlink_msg_close (&oversized);
        TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_BACKPRESSURED, oversized_result);
        TEST_ASSERT_EQUAL_INT (EAGAIN, errno);
    }
    zlink_submit_result_t participant_submit = ZLINK_SUBMIT_INTERNAL_ERROR;
    {
        zlink_msg_t part;
        make_payload (&part, "session-post-fence");
        participant_submit = zlink_stream_session_send_to_actor (
          session_service, &session_rid, &actor, NULL, &part, 1, ZLINK_SEND_FLAGS_DONTWAIT);
        zlink_msg_close (&part);
    }
    zlink_mesh_operation_id_t participant_request_operation;
    memset (&participant_request_operation, 0, sizeof (participant_request_operation));
    zlink_submit_result_t participant_request = ZLINK_SUBMIT_INTERNAL_ERROR;
    {
        zlink_msg_t part;
        make_payload (&part, "session-request");
        participant_request = zlink_stream_session_request_to_actor (
          session_service, &session_rid, &actor, NULL, &part, 1,
          &participant_request_operation, ZLINK_SEND_FLAGS_DONTWAIT, 10000);
        zlink_msg_close (&part);
    }
    zlink_stream_session_status_t session_status;
    memset (&session_status, 0, sizeof (session_status));
    session_status.struct_size = sizeof (session_status);
    session_status.version = ZLINK_STREAM_SESSION_ABI_VERSION;
    const zlink_config_result_t status_result =
      zlink_stream_session_service_status (session_service, &session_status);
    TEST_ASSERT_EQUAL_INT (2, (int) write (authority_pipe[1], "C\n", 2));

    //  Wait for the target activation signal, then source-commit.
    read_endpoint (back_pipe[0], ack, sizeof (ack));
    TEST_ASSERT_EQUAL_STRING ("A", ack);
    TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_OK, participant_submit);
    TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_OK, participant_request);
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK, status_result);
    TEST_ASSERT_EQUAL_UINT64 (2, session_status.pending_message_count);
    TEST_ASSERT_EQUAL_UINT64 (strlen ("session-post-fence") + strlen ("session-request"),
                              session_status.pending_byte_count);
    TEST_ASSERT_EQUAL_INT (
      0, wait_node_completion_payload (node, participant_request_operation, "session-reply"));
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK, zlink_mesh_node_actor_transfer_commit (&token, 2));
    {
        zlink_stream_session_binding_t binding;
        memset (&binding, 0, sizeof (binding));
        binding.struct_size = sizeof (binding);
        binding.version = ZLINK_STREAM_SESSION_ABI_VERSION;
        size_t binding_count = 1;
        TEST_ASSERT_EQUAL_INT (
          ZLINK_CONFIG_OK,
          zlink_stream_session_bindings (session_service, &session_rid, &binding,
                                         &binding_count));
        TEST_ASSERT_EQUAL_UINT64 (1, binding_count);
        TEST_ASSERT_EQUAL_UINT64 (2, binding.membership_epoch);
        TEST_ASSERT_EQUAL_UINT8 (entry.routing_id.size, binding.actor.node_rid.size);
        TEST_ASSERT_EQUAL_MEMORY (entry.routing_id.data, binding.actor.node_rid.data,
                                  entry.routing_id.size);
    }
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

    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK,
                           zlink_stream_session_service_shutdown (session_service, 1000));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK,
                           zlink_stream_session_service_destroy (&session_service));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_close (stream));
    close (stream_client_fd);
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK, zlink_mesh_node_shutdown (node, 2000));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_node_destroy (&node));
    TEST_ASSERT_EQUAL_INT (0, zlink_ctx_term (ctx));
}
//  Drain and reconnect: an inbound peer's exit does not gate A's readiness,
//  and a restarted peer with the same RID is admitted again and can complete
//  a fresh round trip.
void test_remote_channel_round_robin_and_zero_weight_exclusion ()
{
    int child_to_parent[2][2];
    int parent_to_child[2][2];
    for (size_t i = 0; i < 2; ++i) {
        TEST_ASSERT_EQUAL_INT (0, pipe (child_to_parent[i]));
        TEST_ASSERT_EQUAL_INT (0, pipe (parent_to_child[i]));
    }

    fflush (NULL);
    pid_t children[2] = {-1, -1};
    for (size_t child_index = 0; child_index < 2; ++child_index) {
        children[child_index] = fork ();
        TEST_ASSERT_TRUE (children[child_index] >= 0);
        if (children[child_index] != 0)
            continue;

        for (size_t i = 0; i < 2; ++i) {
            close (child_to_parent[i][0]);
            close (parent_to_child[i][1]);
            if (i != child_index) {
                close (child_to_parent[i][1]);
                close (parent_to_child[i][0]);
            }
        }
        setup_test_environment (60);
        void *ctx = zlink_ctx_new ();
        if (!ctx)
            std::_Exit (10);
        const char *rid = child_index == 0 ? "rr-target-a" : "rr-target-b";
        void *node = new_started_node (ctx, "mesh-round-robin", rid);
        publish_endpoint (node, child_to_parent[child_index][1]);

        int phase = 0;
        int result = 0;
        for (;;) {
            char command = 0;
            if (read (parent_to_child[child_index][0], &command, 1) != 1) {
                result = 11;
                break;
            }
            if (command == 'Q')
                break;
            if (command == 'Z') {
                if (child_index != 0
                    || zlink_mesh_node_set_channel_weight (node, channel_name, 0)
                         != ZLINK_CONFIG_OK) {
                    result = 12;
                    break;
                }
                write_result_line (child_to_parent[child_index][1], 0);
                continue;
            }
            if (command != 'D') {
                result = 13;
                break;
            }
            const size_t expected = phase == 0 ? 3 : (child_index == 0 ? 0 : 4);
            const int wait_ms = expected == 0 ? 500 : poll_deadline_ms;
            const int drain_result = receive_exact_node_records (node, expected, wait_ms);
            write_result_line (child_to_parent[child_index][1], drain_result);
            if (drain_result != 0) {
                result = 14 + phase;
                break;
            }
            ++phase;
        }

        zlink_mesh_node_shutdown (node, 1000);
        if (zlink_mesh_node_destroy (&node) != ZLINK_CLOSE_OK && result == 0)
            result = 20;
        if (zlink_ctx_term (ctx) != 0 && result == 0)
            result = 21;
        close (child_to_parent[child_index][1]);
        close (parent_to_child[child_index][0]);
        fflush (NULL);
        std::_Exit (result);
    }

    for (size_t i = 0; i < 2; ++i) {
        close (child_to_parent[i][1]);
        close (parent_to_child[i][0]);
    }
    char endpoints[2][ZLINK_MESH_ENDPOINT_MAX + 1];
    for (size_t i = 0; i < 2; ++i)
        read_endpoint (child_to_parent[i][0], endpoints[i], sizeof (endpoints[i]));

    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    void *source = new_started_node (ctx, "mesh-round-robin", "rr-source");
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK,
                           zlink_mesh_node_set_channel_weight (source, channel_name, 0));
    for (size_t i = 0; i < 2; ++i)
        TEST_ASSERT_NOT_EQUAL (0, submit_peer_intent (source, endpoints[i]));

    bool both_admitted = false;
    for (int waited = 0; waited < poll_deadline_ms && !both_admitted; waited += poll_step_ms) {
        zlink_mesh_node_status_t status;
        node_status (source, &status);
        both_admitted = status.state == ZLINK_MESH_NODE_READY && status.admitted_peer_count == 2;
        if (!both_admitted)
            msleep (poll_step_ms);
    }
    TEST_ASSERT_TRUE_MESSAGE (both_admitted, "both round-robin targets must be admitted");

    for (int i = 0; i < 6; ++i) {
        zlink_msg_t part;
        make_payload (&part, "rr");
        TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_OK,
                               zlink_mesh_node_send_to_channel (
                                 source, channel_name, NULL, &part, 1, ZLINK_SEND_FLAGS_NONE));
        zlink_msg_close (&part);
    }
    for (size_t i = 0; i < 2; ++i)
        TEST_ASSERT_EQUAL_INT (1, (int) write (parent_to_child[i][1], "D", 1));
    for (size_t i = 0; i < 2; ++i) {
        char result_text[32];
        read_endpoint (child_to_parent[i][0], result_text, sizeof (result_text));
        TEST_ASSERT_EQUAL_INT (0, atoi (result_text));
    }

    TEST_ASSERT_EQUAL_INT (1, (int) write (parent_to_child[0][1], "Z", 1));
    char result_text[32];
    read_endpoint (child_to_parent[0][0], result_text, sizeof (result_text));
    TEST_ASSERT_EQUAL_INT (0, atoi (result_text));

    bool zero_observed = false;
    for (int waited = 0; waited < poll_deadline_ms && !zero_observed; waited += poll_step_ms) {
        zlink_mesh_peer_entry_t entries[4];
        memset (entries, 0, sizeof (entries));
        for (size_t i = 0; i < 4; ++i) {
            entries[i].struct_size = sizeof (entries[i]);
            entries[i].version = 1;
        }
        size_t count = 4;
        if (zlink_mesh_node_peers (source, entries, &count) == ZLINK_CONFIG_OK) {
            for (size_t i = 0; i < count; ++i) {
                if (entries[i].routing_id.size != strlen ("rr-target-a")
                    || memcmp (entries[i].routing_id.data, "rr-target-a",
                               entries[i].routing_id.size)
                         != 0)
                    continue;
                char names[4][ZLINK_CHANNEL_NAME_MAX + 1];
                uint32_t weights[4];
                size_t channel_count = 4;
                if (zlink_mesh_node_peer_channels (source, &entries[i].routing_id,
                                                   entries[i].lifecycle_generation, names,
                                                   weights, &channel_count)
                      == ZLINK_CONFIG_OK
                    && channel_count == 1 && strcmp (names[0], channel_name) == 0
                    && weights[0] == 0)
                    zero_observed = true;
            }
        }
        if (!zero_observed)
            msleep (poll_step_ms);
    }
    TEST_ASSERT_TRUE_MESSAGE (zero_observed, "weight zero must update the selection index");

    for (int i = 0; i < 4; ++i) {
        zlink_msg_t part;
        make_payload (&part, "only-b");
        TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_OK,
                               zlink_mesh_node_send_to_channel (
                                 source, channel_name, NULL, &part, 1, ZLINK_SEND_FLAGS_NONE));
        zlink_msg_close (&part);
    }
    for (size_t i = 0; i < 2; ++i)
        TEST_ASSERT_EQUAL_INT (1, (int) write (parent_to_child[i][1], "D", 1));
    for (size_t i = 0; i < 2; ++i) {
        read_endpoint (child_to_parent[i][0], result_text, sizeof (result_text));
        TEST_ASSERT_EQUAL_INT (0, atoi (result_text));
    }

    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK, zlink_mesh_node_shutdown (source, 1000));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_node_destroy (&source));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_ctx_term (ctx));
    for (size_t i = 0; i < 2; ++i) {
        TEST_ASSERT_EQUAL_INT (1, (int) write (parent_to_child[i][1], "Q", 1));
        close (parent_to_child[i][1]);
        close (child_to_parent[i][0]);
    }
    for (size_t i = 0; i < 2; ++i) {
        int child_status = 0;
        TEST_ASSERT_EQUAL_INT (children[i], waitpid (children[i], &child_status, 0));
        TEST_ASSERT_TRUE_MESSAGE (WIFEXITED (child_status) && WEXITSTATUS (child_status) == 0,
                                  "round-robin target process reported failure");
    }
}

//  An inbound-observed peer records the endpoint it advertises, so a later
//  manual intent for the same endpoint merges into one MIXED-source entry;
//  removing the manual source keeps the connection under DISCOVERY.
void test_inbound_peer_merges_manual_intent_to_mixed ()
{
    run_two_process_case (
      //  parent: node A only accepts the inbound connection, then adds a
      //  manual intent for the child's advertised endpoint.
      [] (int endpoint_fd, pid_t) {
          void *ctx = zlink_ctx_new ();
          TEST_ASSERT_NOT_NULL (ctx);
          void *node = new_started_node (ctx, mesh_name, "node-a");
          publish_endpoint (node, endpoint_fd);

          TEST_ASSERT_TRUE_MESSAGE (wait_admitted_count (node, 1),
                                    "node A did not admit the inbound peer");

          zlink_mesh_peer_entry_t entry;
          TEST_ASSERT_TRUE (wait_peer_state (node, ZLINK_MESH_PEER_ADMITTED, &entry));
          TEST_ASSERT_EQUAL_INT (ZLINK_MESH_PEER_DISCOVERY, entry.source);
          TEST_ASSERT_TRUE_MESSAGE (entry.endpoint[0] != '\0',
                                    "inbound peer lost its advertised endpoint");

          //  The manual intent for the same endpoint merges into MIXED.
          zlink_mesh_peer_connection_options_t options;
          memset (&options, 0, sizeof (options));
          options.struct_size = sizeof (options);
          options.version = 1;
          options.endpoint = entry.endpoint;
          options.endpoint_size = strlen (entry.endpoint);
          uint64_t intent_id = 0;
          TEST_ASSERT_EQUAL_INT (ZLINK_CONNECT_OK,
                                 zlink_mesh_node_connect_peer (node, &options, &intent_id));
          TEST_ASSERT_EQUAL_UINT64 (entry.connection_intent_id, intent_id);

          zlink_mesh_peer_entry_t merged;
          TEST_ASSERT_TRUE (wait_peer_state (node, ZLINK_MESH_PEER_ADMITTED, &merged));
          TEST_ASSERT_EQUAL_INT (ZLINK_MESH_PEER_MIXED, merged.source);

          //  Removing the manual source keeps the discovery-observed side.
          TEST_ASSERT_EQUAL_INT (ZLINK_CONNECT_OK,
                                 zlink_mesh_node_remove_peer_connection (node, intent_id));
          zlink_mesh_peer_entry_t after;
          TEST_ASSERT_TRUE (wait_peer_state (node, ZLINK_MESH_PEER_ADMITTED, &after));
          TEST_ASSERT_EQUAL_INT (ZLINK_MESH_PEER_DISCOVERY, after.source);

          //  Outlive the child's teardown before closing this side.
          msleep (1500);
          TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK, zlink_mesh_node_shutdown (node, 2000));
          TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_node_destroy (&node));
          TEST_ASSERT_EQUAL_INT (0, zlink_ctx_term (ctx));
      },
      //  child: node B connects manually and stays admitted briefly.
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
          //  Stay admitted long enough for the parent's merge assertions,
          //  then leave first so context teardown never lingers on a peer
          //  that already closed its socket.
          msleep (800);
          zlink_mesh_node_shutdown (node, 1000);
          if (zlink_mesh_node_destroy (&node) != ZLINK_CLOSE_OK)
              return 14;
          if (zlink_ctx_term (ctx) != 0)
              return 15;
          return 0;
      });
}

void test_peer_drain_and_reconnect ()
{
    //  Both children fork before the parent's node exists (the per-process
    //  MeshName registry is inherited across fork); each waits for the
    //  endpoint on its own local socket.
    int ep1[2];
    int ep2[2];
    TEST_ASSERT_EQUAL_INT (0, socketpair (AF_UNIX, SOCK_STREAM, 0, ep1));
    TEST_ASSERT_EQUAL_INT (0, socketpair (AF_UNIX, SOCK_STREAM, 0, ep2));

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
        char parent_observed = 0;
        if (rc == 0
            && (read (ep1[0], &parent_observed, 1) != 1
                || parent_observed != '1'))
            rc = 12;
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
    const char round1_observed = '1';
    TEST_ASSERT_EQUAL_INT (
      1, static_cast<int> (write (ep1[1], &round1_observed, 1)));
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

int main (int argc, char **argv)
{
    setup_test_environment (180);
    UNITY_BEGIN ();
#if !defined _WIN32
#define RUN_SELECTED(test_)                                                   \
    do {                                                                      \
        if (argc == 1 || strcmp (argv[1], #test_) == 0)                       \
            RUN_TEST (test_);                                                 \
    } while (false)
    RUN_SELECTED (test_hello_before_ready_does_not_copy_predecessor_transport);
    RUN_SELECTED (test_ready_handover_reconnects_before_delayed_disconnect);
    RUN_SELECTED (test_peer_admission_readiness_and_weight_update);
    RUN_SELECTED (test_stale_transport_disconnect_preserves_admitted_successor);
    RUN_SELECTED (test_outbound_disconnect_then_reconnect_same_endpoint);
    RUN_SELECTED (test_peer_mesh_name_mismatch_is_conflict);
    RUN_SELECTED (test_remote_node_request_reply_round_trip);
    RUN_SELECTED (test_remote_spot_direct_request_reply);
    RUN_SELECTED (test_remote_multicast_publish);
    RUN_SELECTED (test_shutdown_interrupts_infinite_blocking_mesh_send);
    RUN_SELECTED (test_router_unreachable_target_accounting);
    RUN_SELECTED (test_remote_actor_lookup_messaging_destroy);
    RUN_SELECTED (test_remote_actor_join_entry_spot);
    RUN_SELECTED (test_remote_actor_transfer_fence);
    RUN_SELECTED (test_remote_channel_round_robin_and_zero_weight_exclusion);
    RUN_SELECTED (test_inbound_peer_merges_manual_intent_to_mixed);
    RUN_SELECTED (test_peer_drain_and_reconnect);
#undef RUN_SELECTED
#endif
    return UNITY_END ();
}
