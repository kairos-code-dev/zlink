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
extern "C" void zlink_test_set_lifecycle_generation_floor (uint64_t floor_);
extern "C" void zlink_test_mesh_inject_disconnect (void *mesh_node_,
                                                    const zlink_routing_id_t *peer_rid_,
                                                    uint64_t connection_id_);
extern "C" uint64_t zlink_test_mesh_select_admission_transport (
  int is_hello_, uint64_t previous_connection_id_,
  uint64_t current_connection_id_);
extern "C" int zlink_test_mesh_duplicate_admitted_lifetime (
  int is_hello_, uint64_t existing_generation_,
  uint64_t incoming_generation_);
extern "C" int zlink_test_mesh_repeated_selected_hello (
  uint64_t existing_generation_, uint64_t incoming_generation_,
  uint64_t existing_connection_id_, uint64_t selected_connection_id_);
extern "C" int zlink_test_mesh_transport_ready_transition (
  uint64_t previous_connection_id_, uint64_t ready_connection_id_);
extern "C" void zlink_test_stream_session_fence_actor (
  void *service_, const zlink_actor_ref_t *actor_, uint64_t transfer_serial_);
extern "C" void zlink_test_stream_session_pause_bound_session_admit (
  int pause_);
extern "C" int zlink_test_stream_session_bound_session_admit_paused ();

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
    //  A repeated HELLO on ROUTER's already-selected physical transport is
    //  acknowledged again. A different transport remains a duplicate
    //  lifetime conflict.
    TEST_ASSERT_EQUAL_INT (
      1, zlink_test_mesh_repeated_selected_hello (5, 5, 41, 41));
    TEST_ASSERT_EQUAL_INT (
      0, zlink_test_mesh_repeated_selected_hello (5, 5, 41, 42));
    TEST_ASSERT_EQUAL_INT (
      0, zlink_test_mesh_repeated_selected_hello (5, 6, 41, 41));
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
                        bool infinite_send_timeout_ = false,
                        int send_timeout_ms_ = 0,
                        const char *bind_endpoint_ = "tcp://127.0.0.1:0")
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
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK,
                           zlink_mesh_node_set_bind (node, bind_endpoint_));
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
    } else if (send_timeout_ms_ > 0) {
        TEST_ASSERT_EQUAL_INT (
          ZLINK_CONFIG_OK,
          zlink_set_option (node, ZLINK_OPT_SNDTIMEO, &send_timeout_ms_,
                            sizeof (send_timeout_ms_)));
    }
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK, zlink_mesh_node_start (node));
    return node;
}

void *new_started_zero_membership_node (void *ctx_,
                                        const char *mesh_name_,
                                        const char *rid_)
{
    zlink_mesh_node_options_t options;
    memset (&options, 0, sizeof (options));
    options.struct_size = sizeof (options);
    options.version = 1;
    options.mesh_name = mesh_name_;
    options.mesh_name_size = strlen (mesh_name_);
    void *node = zlink_mesh_node_new (ctx_, &options);
    TEST_ASSERT_NOT_NULL (node);

    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK, zlink_set_routing_id (node, rid_, strlen (rid_)));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK,
      zlink_mesh_node_set_bind (node, "tcp://127.0.0.1:0"));
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK, zlink_mesh_node_start (node));
    return node;
}

void *new_tls_started_node (void *ctx_,
                            const char *rid_,
                            const char *bind_endpoint_,
                            const tls_test_files_t &files_)
{
    zlink_mesh_node_options_t options;
    memset (&options, 0, sizeof (options));
    options.struct_size = sizeof (options);
    options.version = 1;
    options.mesh_name = mesh_name;
    options.mesh_name_size = strlen (mesh_name);
    void *node = zlink_mesh_node_new (ctx_, &options);
    TEST_ASSERT_NOT_NULL (node);

    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK, zlink_set_routing_id (node, rid_, strlen (rid_)));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK, zlink_mesh_node_set_bind (node, bind_endpoint_));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK, zlink_mesh_node_add_channel_name (node, channel_name));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK,
      zlink_set_tls_server (
        node, files_.server_cert.c_str (), files_.server_key.c_str (), 0));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK,
      zlink_set_tls_client (
        node, files_.ca_cert.c_str (), "localhost", 0));
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

void test_mesh_tls_bind_uses_configured_server_material ()
{
    const tls_test_files_t files = make_tls_test_files ();
    const char *const transports[] = {"tls", "wss"};
    for (size_t i = 0; i < sizeof (transports) / sizeof (transports[0]); ++i) {
        if (zlink_has (transports[i]) == 0)
            continue;

        char bind_endpoint[64];
        snprintf (bind_endpoint, sizeof (bind_endpoint),
                  "%s://localhost:0", transports[i]);
        void *server_ctx = zlink_ctx_new ();
        TEST_ASSERT_NOT_NULL (server_ctx);
        void *server =
          new_tls_started_node (
            server_ctx, "tls-server", bind_endpoint, files);

        zlink_mesh_node_status_t server_status;
        node_status (server, &server_status);
        TEST_ASSERT_TRUE (server_status.local_endpoint[0] != '\0');

        TEST_ASSERT_EQUAL_INT (
          ZLINK_CLOSE_OK, zlink_mesh_node_shutdown (server, 1000));
        TEST_ASSERT_EQUAL_INT (
          ZLINK_CLOSE_OK, zlink_mesh_node_destroy (&server));
        TEST_ASSERT_EQUAL_INT (0, zlink_ctx_term (server_ctx));
    }
    cleanup_tls_test_files (files);
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

bool write_routing_id (int fd_, const zlink_routing_id_t &rid_)
{
    const unsigned char *data =
      reinterpret_cast<const unsigned char *> (&rid_);
    size_t written = 0;
    while (written < sizeof (rid_)) {
        const ssize_t rc = write (fd_, data + written, sizeof (rid_) - written);
        if (rc <= 0)
            return false;
        written += static_cast<size_t> (rc);
    }
    return true;
}

bool read_routing_id (int fd_, zlink_routing_id_t *rid_out_)
{
    unsigned char *data = reinterpret_cast<unsigned char *> (rid_out_);
    size_t received = 0;
    while (received < sizeof (*rid_out_)) {
        const ssize_t rc = read (fd_, data + received,
                                 sizeof (*rid_out_) - received);
        if (rc <= 0)
            return false;
        received += static_cast<size_t> (rc);
    }
    return rid_out_->size > 0;
}

bool write_u64 (int fd_, uint64_t value_)
{
    const unsigned char *data =
      reinterpret_cast<const unsigned char *> (&value_);
    size_t written = 0;
    while (written < sizeof (value_)) {
        const ssize_t rc = write (fd_, data + written,
                                  sizeof (value_) - written);
        if (rc <= 0)
            return false;
        written += static_cast<size_t> (rc);
    }
    return true;
}

bool read_u64 (int fd_, uint64_t *value_out_)
{
    unsigned char *data = reinterpret_cast<unsigned char *> (value_out_);
    size_t received = 0;
    while (received < sizeof (*value_out_)) {
        const ssize_t rc = read (fd_, data + received,
                                 sizeof (*value_out_) - received);
        if (rc <= 0)
            return false;
        received += static_cast<size_t> (rc);
    }
    return true;
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
        if (zlink_mesh_node_peers (node_, entries, &count) == ZLINK_CONFIG_OK) {
            for (size_t i = 0; i < count; ++i) {
                if (entries[i].state != state_)
                    continue;
                *entry_out_ = entries[i];
                return true;
            }
        }
        msleep (poll_step_ms);
    }
    return false;
}

bool wait_peer_rid_state (void *node_,
                          const char *rid_,
                          zlink_mesh_peer_state_t state_,
                          zlink_mesh_peer_entry_t *entry_out_)
{
    const size_t rid_size = strlen (rid_);
    for (int waited = 0; waited < poll_deadline_ms; waited += poll_step_ms) {
        zlink_mesh_peer_entry_t entries[8];
        memset (entries, 0, sizeof (entries));
        for (size_t i = 0; i < 8; ++i) {
            entries[i].struct_size = sizeof (entries[i]);
            entries[i].version = 1;
        }
        size_t count = 8;
        if (zlink_mesh_node_peers (node_, entries, &count) == ZLINK_CONFIG_OK) {
            for (size_t i = 0; i < count; ++i) {
                if (entries[i].state != state_
                    || entries[i].routing_id.size != rid_size
                    || memcmp (entries[i].routing_id.data, rid_, rid_size) != 0)
                    continue;
                *entry_out_ = entries[i];
                return true;
            }
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

bool wait_peer_channel_weight (void *node_,
                               const char *rid_,
                               uint32_t weight_,
                               int timeout_ms_)
{
    for (int waited = 0; waited < timeout_ms_; waited += poll_step_ms) {
        zlink_mesh_peer_entry_t entries[8];
        memset (entries, 0, sizeof (entries));
        for (size_t i = 0; i < 8; ++i) {
            entries[i].struct_size = sizeof (entries[i]);
            entries[i].version = 1;
        }
        size_t count = 8;
        if (zlink_mesh_node_peers (node_, entries, &count) == ZLINK_CONFIG_OK) {
            for (size_t i = 0; i < count; ++i) {
                if (entries[i].state != ZLINK_MESH_PEER_ADMITTED
                    || entries[i].routing_id.size != strlen (rid_)
                    || memcmp (entries[i].routing_id.data, rid_, strlen (rid_)) != 0)
                    continue;
                char names[4][ZLINK_CHANNEL_NAME_MAX + 1];
                uint32_t weights[4];
                size_t channel_count = 4;
                if (zlink_mesh_node_peer_channels (
                      node_, &entries[i].routing_id,
                      entries[i].lifecycle_generation, names, weights,
                      &channel_count)
                      == ZLINK_CONFIG_OK
                    && channel_count == 1
                    && strcmp (names[0], channel_name) == 0
                    && weights[0] == weight_)
                    return true;
            }
        }
        msleep (poll_step_ms);
    }
    return false;
}

bool wait_peer_monitor_event (void *monitor_,
                              zlink_mesh_monitor_event_kind_t kind_,
                              const char *rid_,
                              zlink_mesh_monitor_event_t *event_out_)
{
    const size_t rid_size = strlen (rid_);
    for (int waited = 0; waited < poll_deadline_ms; waited += poll_step_ms) {
        zlink_mesh_monitor_event_t event;
        memset (&event, 0, sizeof (event));
        event.struct_size = sizeof (event);
        event.version = 1;
        const zlink_recv_result_t rc =
          zlink_mesh_node_monitor_recv (
            monitor_, &event, ZLINK_RECV_FLAGS_DONTWAIT);
        if (rc == ZLINK_RECV_NO_DATA) {
            msleep (poll_step_ms);
            continue;
        }
        if (rc != ZLINK_RECV_OK)
            return false;
        if (event.kind != kind_ || event.peer_rid.size != rid_size
            || memcmp (event.peer_rid.data, rid_, rid_size) != 0)
            continue;
        *event_out_ = event;
        return true;
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

int receive_one_spot_multicast (void *node_,
                                const char *topic_,
                                const char *payload_,
                                int wait_ms_)
{
    void *ready = zlink_mesh_ready_batch_new (8);
    void *batch = zlink_mesh_receive_batch_new (8, 16, 4096);
    if (!ready || !batch)
        return -1;
    int result = -1;
    for (int waited = 0; waited < wait_ms_; waited += poll_step_ms) {
        uint32_t residue = 0;
        const zlink_recv_result_t ready_rc = zlink_mesh_node_drain_ready (
          node_, ZLINK_MESH_READY_APPLICATION, ready, &residue,
          ZLINK_RECV_FLAGS_DONTWAIT);
        if (ready_rc == ZLINK_RECV_NO_DATA) {
            msleep (poll_step_ms);
            continue;
        }
        if (ready_rc != ZLINK_RECV_OK)
            break;
        const zlink_mesh_ready_record_t *ready_records =
          zlink_mesh_ready_batch_data (ready);
        const size_t ready_count = zlink_mesh_ready_batch_count (ready);
        for (size_t i = 0; i < ready_count; ++i) {
            if (ready_records[i].owner_kind != ZLINK_MESH_OWNER_SPOT)
                continue;
            zlink_mesh_claim_t claim;
            if (zlink_mesh_ready_batch_take_claim (ready, i, &claim)
                != ZLINK_CONFIG_OK)
                continue;
            zlink_mesh_receive_requirements_t requirements;
            memset (&requirements, 0, sizeof (requirements));
            requirements.struct_size = sizeof (requirements);
            requirements.version = 1;
            if (zlink_mesh_claim_recv_batch (
                  &claim, batch, &requirements, ZLINK_RECV_FLAGS_NONE)
                == ZLINK_RECV_OK) {
                const zlink_mesh_receive_record_t *records =
                  zlink_mesh_receive_batch_data (batch);
                const zlink_msg_t *parts =
                  zlink_mesh_receive_batch_parts (batch);
                for (size_t j = 0;
                     j < zlink_mesh_receive_batch_count (batch); ++j) {
                    if (records[j].kind != ZLINK_MESH_RECORD_SPOT_MULTICAST
                        || !records[j].topic
                        || records[j].topic_size != strlen (topic_)
                        || memcmp (records[j].topic, topic_, strlen (topic_)) != 0
                        || records[j].part_count != 1)
                        continue;
                    const zlink_msg_t *part = &parts[records[j].part_offset];
                    if (zlink_msg_size (part) == strlen (payload_)
                        && memcmp (zlink_msg_data (
                                     const_cast<zlink_msg_t *> (part)),
                                   payload_, strlen (payload_))
                             == 0)
                        result = 0;
                }
            }
            zlink_mesh_claim_release (&claim);
            zlink_mesh_receive_batch_reset (batch);
            if (result == 0)
                break;
        }
        zlink_mesh_ready_batch_reset (ready);
        if (result == 0)
            break;
    }
    zlink_mesh_ready_batch_destroy (&ready);
    zlink_mesh_receive_batch_destroy (&batch);
    return result;
}

void write_result_line (int fd_, int value_)
{
    char text[32];
    const int size = snprintf (text, sizeof (text), "%d\n", value_);
    if (size > 0 && write (fd_, text, static_cast<size_t> (size)) != size)
        std::_Exit (98);
}

int receive_node_application_record (void *node_,
                                     zlink_mesh_record_kind_t expected_kind_,
                                     const char *expected_payload_,
                                     const char *reply_payload_)
{
    void *ready = zlink_mesh_ready_batch_new (8);
    void *batch = zlink_mesh_receive_batch_new (8, 32, 64 * 1024);
    if (!ready || !batch)
        return 1;
    int result = 2;
    for (int waited = 0; waited < poll_deadline_ms; waited += poll_step_ms) {
        uint32_t residue = 0;
        const zlink_recv_result_t ready_rc = zlink_mesh_node_drain_ready (
          node_, ZLINK_MESH_READY_APPLICATION, ready, &residue,
          ZLINK_RECV_FLAGS_DONTWAIT);
        if (ready_rc == ZLINK_RECV_NO_DATA) {
            msleep (poll_step_ms);
            continue;
        }
        if (ready_rc != ZLINK_RECV_OK) {
            result = 3;
            break;
        }

        const zlink_mesh_ready_record_t *ready_records =
          zlink_mesh_ready_batch_data (ready);
        const size_t ready_count = zlink_mesh_ready_batch_count (ready);
        zlink_mesh_claim_t claim;
        bool claimed = false;
        for (size_t i = 0; i < ready_count; ++i) {
            if (ready_records[i].owner_kind != ZLINK_MESH_OWNER_NODE)
                continue;
            if (zlink_mesh_ready_batch_take_claim (ready, i, &claim)
                != ZLINK_CONFIG_OK) {
                result = 4;
                break;
            }
            claimed = true;
            break;
        }
        zlink_mesh_ready_batch_reset (ready);
        if (!claimed) {
            msleep (poll_step_ms);
            continue;
        }

        zlink_mesh_receive_requirements_t requirements;
        memset (&requirements, 0, sizeof (requirements));
        requirements.struct_size = sizeof (requirements);
        requirements.version = 1;
        if (zlink_mesh_claim_recv_batch (
              &claim, batch, &requirements, ZLINK_RECV_FLAGS_NONE)
            != ZLINK_RECV_OK) {
            zlink_mesh_claim_release (&claim);
            result = 5;
            break;
        }

        const zlink_mesh_receive_record_t *records =
          zlink_mesh_receive_batch_data (batch);
        const zlink_msg_t *parts = zlink_mesh_receive_batch_parts (batch);
        const size_t record_count = zlink_mesh_receive_batch_count (batch);
        for (size_t i = 0; i < record_count; ++i) {
            if (records[i].kind != expected_kind_ || records[i].part_count != 1)
                continue;
            const zlink_msg_t *part = &parts[records[i].part_offset];
            if (zlink_msg_size (part) != strlen (expected_payload_)
                || memcmp (zlink_msg_data (const_cast<zlink_msg_t *> (part)),
                           expected_payload_, strlen (expected_payload_))
                     != 0)
                continue;
            result = 0;
            if (reply_payload_) {
                zlink_msg_t reply;
                if (zlink_msg_init_size (&reply, strlen (reply_payload_))
                    != ZLINK_CONFIG_OK) {
                    result = 6;
                    break;
                }
                memcpy (zlink_msg_data (&reply), reply_payload_,
                        strlen (reply_payload_));
                if (zlink_mesh_reply (&records[i].reply_token, &reply, 1,
                                     ZLINK_SEND_FLAGS_NONE)
                    != ZLINK_SUBMIT_OK)
                    result = 7;
                zlink_msg_close (&reply);
            }
            break;
        }
        zlink_mesh_claim_release (&claim);
        zlink_mesh_receive_batch_reset (batch);
        if (result == 0 || result >= 6)
            break;
    }
    zlink_mesh_ready_batch_destroy (&ready);
    zlink_mesh_receive_batch_destroy (&batch);
    return result;
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

void wait_node_send_ready (void *node_, const char *target_rid_)
{
    zlink_mesh_claim_t claim;
    take_ready_claim (node_, ZLINK_MESH_OWNER_NODE,
                      ZLINK_MESH_READY_INFRASTRUCTURE, &claim);
    void *batch = zlink_mesh_receive_batch_new (8, 8, 64 * 1024);
    TEST_ASSERT_NOT_NULL (batch);
    zlink_mesh_receive_requirements_t requirements;
    memset (&requirements, 0, sizeof (requirements));
    requirements.struct_size = sizeof (requirements);
    requirements.version = 1;
    TEST_ASSERT_EQUAL_INT (
      ZLINK_RECV_OK,
      zlink_mesh_claim_recv_batch (
        &claim, batch, &requirements, ZLINK_RECV_FLAGS_NONE));
    bool found = false;
    const size_t count = zlink_mesh_receive_batch_count (batch);
    const zlink_mesh_receive_record_t *records =
      zlink_mesh_receive_batch_data (batch);
    for (size_t i = 0; i < count; ++i) {
        if (records[i].kind != ZLINK_MESH_RECORD_SEND_READY)
            continue;
        TEST_ASSERT_EQUAL_UINT (sizeof (zlink_mesh_send_ready_data_t),
                                records[i].kind_data_size);
        const zlink_mesh_send_ready_data_t *ready =
          static_cast<const zlink_mesh_send_ready_data_t *> (
            records[i].kind_data);
        TEST_ASSERT_EQUAL_INT (ZLINK_MESH_DESTINATION_NODE,
                               ready->destination_kind);
        TEST_ASSERT_EQUAL_UINT (strlen (target_rid_),
                                ready->target_node_rid.size);
        TEST_ASSERT_EQUAL_MEMORY (target_rid_, ready->target_node_rid.data,
                                  ready->target_node_rid.size);
        found = true;
    }
    TEST_ASSERT_TRUE_MESSAGE (found, "Node SEND_READY did not arrive");
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_claim_release (&claim));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK,
                           zlink_mesh_receive_batch_destroy (&batch));
}

//  Drains a Node-owner infrastructure claim when available and counts only
//  SEND_READY records for actor_. A zero timeout performs one non-blocking
//  observation, which is used to prove that a transfer fence does not create
//  an immediate notification loop.
size_t drain_node_actor_send_ready (void *node_,
                                    const zlink_actor_ref_t &actor_,
                                    int timeout_ms_)
{
    void *ready_batch = zlink_mesh_ready_batch_new (16);
    void *receive_batch = zlink_mesh_receive_batch_new (16, 16, 64 * 1024);
    TEST_ASSERT_NOT_NULL (ready_batch);
    TEST_ASSERT_NOT_NULL (receive_batch);
    size_t matching = 0;
    for (int waited = 0; waited <= timeout_ms_; waited += poll_step_ms) {
        uint32_t residue = 0;
        const zlink_recv_result_t ready_rc = zlink_mesh_node_drain_ready (
          node_, ZLINK_MESH_READY_INFRASTRUCTURE, ready_batch, &residue,
          ZLINK_RECV_FLAGS_DONTWAIT);
        if (ready_rc == ZLINK_RECV_NO_DATA) {
            if (waited < timeout_ms_)
                msleep (poll_step_ms);
            continue;
        }
        TEST_ASSERT_EQUAL_INT (ZLINK_RECV_OK, ready_rc);
        const size_t ready_count = zlink_mesh_ready_batch_count (ready_batch);
        const zlink_mesh_ready_record_t *ready_records =
          zlink_mesh_ready_batch_data (ready_batch);
        bool claimed = false;
        zlink_mesh_claim_t claim;
        for (size_t i = 0; i < ready_count && !claimed; ++i) {
            if (ready_records[i].owner_kind != ZLINK_MESH_OWNER_NODE)
                continue;
            TEST_ASSERT_EQUAL_INT (
              ZLINK_CONFIG_OK,
              zlink_mesh_ready_batch_take_claim (ready_batch, i, &claim));
            claimed = true;
        }
        TEST_ASSERT_EQUAL_INT (
          ZLINK_CONFIG_OK, zlink_mesh_ready_batch_reset (ready_batch));
        if (!claimed) {
            if (waited < timeout_ms_)
                msleep (poll_step_ms);
            continue;
        }

        zlink_mesh_receive_requirements_t requirements;
        memset (&requirements, 0, sizeof (requirements));
        requirements.struct_size = sizeof (requirements);
        requirements.version = 1;
        TEST_ASSERT_EQUAL_INT (
          ZLINK_RECV_OK,
          zlink_mesh_claim_recv_batch (
            &claim, receive_batch, &requirements, ZLINK_RECV_FLAGS_NONE));
        const size_t record_count =
          zlink_mesh_receive_batch_count (receive_batch);
        const zlink_mesh_receive_record_t *records =
          zlink_mesh_receive_batch_data (receive_batch);
        for (size_t i = 0; i < record_count; ++i) {
            if (records[i].kind != ZLINK_MESH_RECORD_SEND_READY
                || records[i].kind_data_size
                     != sizeof (zlink_mesh_send_ready_data_t))
                continue;
            const zlink_mesh_send_ready_data_t *data =
              static_cast<const zlink_mesh_send_ready_data_t *> (
                records[i].kind_data);
            if (data->destination_kind != ZLINK_MESH_DESTINATION_ACTOR
                || data->target_actor.generation != actor_.generation
                || strncmp (data->target_actor.actor_id, actor_.actor_id,
                            sizeof (actor_.actor_id))
                     != 0
                || data->target_actor.node_rid.size != actor_.node_rid.size
                || memcmp (data->target_actor.node_rid.data,
                           actor_.node_rid.data, actor_.node_rid.size)
                     != 0)
                continue;
            ++matching;
        }
        TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_claim_release (&claim));
        TEST_ASSERT_EQUAL_INT (
          ZLINK_CONFIG_OK, zlink_mesh_receive_batch_reset (receive_batch));
        if (matching != 0)
            break;
    }
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CLOSE_OK, zlink_mesh_ready_batch_destroy (&ready_batch));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CLOSE_OK, zlink_mesh_receive_batch_destroy (&receive_batch));
    return matching;
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
                          size_t kind_data_capacity_,
                          int32_t expected_terminal_result_ = ZLINK_REQUEST_OK,
                          int32_t expected_failure_errno_ = 0)
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
            if (record[r].terminal_result != expected_terminal_result_
                || record[r].failure_errno != expected_failure_errno_)
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

//  A configured outbound intent owns its connector across transport loss.
//  When the remote process restarts on the same endpoint and reuses its RID,
//  the connector must re-run admission without an application resubmitting
//  the intent, and application traffic must reach the replacement process.
void test_outbound_intent_auto_reconnects_restarted_peer ()
{
    char endpoint[64];
    snprintf (endpoint, sizeof (endpoint), "tcp://127.0.0.1:%d",
              test_port (17631));
    char requester_endpoint[64];
    snprintf (requester_endpoint, sizeof (requester_endpoint),
              "tcp://127.0.0.1:%d", test_port (17632));
    char stable_endpoint[64];
    snprintf (stable_endpoint, sizeof (stable_endpoint),
              "tcp://127.0.0.1:%d", test_port (17633));

    int first_control[2];
    int second_control[2];
    int stable_control[2];
    TEST_ASSERT_EQUAL_INT (
      0, socketpair (AF_UNIX, SOCK_STREAM, 0, first_control));
    TEST_ASSERT_EQUAL_INT (
      0, socketpair (AF_UNIX, SOCK_STREAM, 0, second_control));
    TEST_ASSERT_EQUAL_INT (
      0, socketpair (AF_UNIX, SOCK_STREAM, 0, stable_control));

    const auto serve_one_request =
      [endpoint, requester_endpoint] (int control_fd_, bool await_kill_) -> int {
        char start = 0;
        if (read (control_fd_, &start, 1) != 1 || start != 'S')
            return 10;

        void *ctx = zlink_ctx_new ();
        if (!ctx)
            return 11;
        void *node = new_started_node (
          ctx, mesh_name, "node-b", 0, 0, false, 0, endpoint);
        if (submit_peer_intent (node, requester_endpoint) == 0)
            return 12;
        void *spot = NULL;
        if (zlink_mesh_node_entry_spot (node, &spot) != ZLINK_CONFIG_OK)
            return 13;
        zlink_spot_status_t spot_status;
        memset (&spot_status, 0, sizeof (spot_status));
        spot_status.struct_size = sizeof (spot_status);
        spot_status.version = 1;
        if (zlink_spot_status (spot, &spot_status) != ZLINK_CONFIG_OK
            || spot_status.lifecycle_generation == 0)
            return 14;
        const char bound = 'B';
        if (write (control_fd_, &bound, 1) != 1)
            return 15;
        char generation_line[32];
        const int generation_size =
          snprintf (generation_line, sizeof (generation_line), "%llu\n",
                    (unsigned long long) spot_status.lifecycle_generation);
        if (generation_size <= 0
            || write (control_fd_, generation_line,
                      static_cast<size_t> (generation_size))
                 != generation_size)
            return 16;

        zlink_mesh_peer_entry_t admitted;
        if (!wait_peer_state (node, ZLINK_MESH_PEER_ADMITTED, &admitted))
            return 17;
        //  Reciprocal connectors may still be converging on their
        //  deterministic physical direction when the first admitted
        //  snapshot appears. Require the logical admission to remain present
        //  across that handover before application traffic starts.
        msleep (500);
        if (!wait_peer_state (node, ZLINK_MESH_PEER_ADMITTED, &admitted))
            return 18;
        const char admitted_state = 'A';
        if (write (control_fd_, &admitted_state, 1) != 1)
            return 19;

        zlink_mesh_claim_t claim;
        take_ready_claim (
          node, ZLINK_MESH_OWNER_NODE, ZLINK_MESH_READY_APPLICATION, &claim);
        void *batch = zlink_mesh_receive_batch_new (8, 32, 64 * 1024);
        if (!batch)
            return 20;
        zlink_mesh_receive_requirements_t requirements;
        memset (&requirements, 0, sizeof (requirements));
        if (zlink_mesh_claim_recv_batch (
              &claim, batch, &requirements, ZLINK_RECV_FLAGS_NONE)
            != ZLINK_RECV_OK)
            return 21;
        const zlink_mesh_receive_record_t *record =
          zlink_mesh_receive_batch_data (batch);
        if (zlink_mesh_receive_batch_count (batch) != 1
            || record->kind != ZLINK_MESH_RECORD_NODE_REQUEST)
            return 22;
        zlink_msg_t reply;
        make_payload (&reply, "pong-reconnect");
        if (zlink_mesh_reply (
              &record->reply_token, &reply, 1, ZLINK_SEND_FLAGS_NONE)
            != ZLINK_SUBMIT_OK)
            return 23;
        zlink_mesh_claim_release (&claim);
        zlink_mesh_receive_batch_reset (batch);

        take_ready_claim (
          node, ZLINK_MESH_OWNER_SPOT, ZLINK_MESH_READY_APPLICATION, &claim);
        memset (&requirements, 0, sizeof (requirements));
        if (zlink_mesh_claim_recv_batch (
              &claim, batch, &requirements, ZLINK_RECV_FLAGS_NONE)
            != ZLINK_RECV_OK)
            return 24;
        record = zlink_mesh_receive_batch_data (batch);
        if (zlink_mesh_receive_batch_count (batch) != 1
            || record->kind != ZLINK_MESH_RECORD_SPOT_REQUEST)
            return 25;
        make_payload (&reply, "spot-pong-reconnect");
        if (zlink_mesh_reply (
              &record->reply_token, &reply, 1, ZLINK_SEND_FLAGS_NONE)
            != ZLINK_SUBMIT_OK)
            return 26;
        zlink_mesh_claim_release (&claim);
        zlink_mesh_receive_batch_destroy (&batch);

        const char replied = 'R';
        if (write (control_fd_, &replied, 1) != 1)
            return 27;
        if (await_kill_) {
            char ignored = 0;
            if (read (control_fd_, &ignored, 1) < 0)
                return 28;
            return 29;
        }

        msleep (300);
        zlink_spot_destroy (&spot);
        zlink_mesh_node_shutdown (node, 1000);
        const int destroy_rc = zlink_mesh_node_destroy (&node);
        const int term_rc = zlink_ctx_term (ctx);
        return destroy_rc == ZLINK_CLOSE_OK && term_rc == 0 ? 0 : 30;
      };

    fflush (NULL);
    const pid_t first_child = fork ();
    TEST_ASSERT_TRUE (first_child >= 0);
    if (first_child == 0) {
        close (first_control[1]);
        close (second_control[0]);
        close (second_control[1]);
        close (stable_control[0]);
        close (stable_control[1]);
        setup_test_environment (70);
        const int rc = serve_one_request (first_control[0], true);
        close (first_control[0]);
        fflush (NULL);
        std::_Exit (rc);
    }

    fflush (NULL);
    const pid_t second_child = fork ();
    TEST_ASSERT_TRUE (second_child >= 0);
    if (second_child == 0) {
        close (second_control[1]);
        close (first_control[0]);
        close (first_control[1]);
        close (stable_control[0]);
        close (stable_control[1]);
        setup_test_environment (80);
        const int rc = serve_one_request (second_control[0], false);
        close (second_control[0]);
        fflush (NULL);
        std::_Exit (rc);
    }

    fflush (NULL);
    const pid_t stable_child = fork ();
    TEST_ASSERT_TRUE (stable_child >= 0);
    if (stable_child == 0) {
        close (stable_control[1]);
        close (first_control[0]);
        close (first_control[1]);
        close (second_control[0]);
        close (second_control[1]);
        setup_test_environment (85);
        int rc = 0;
        char command = 0;
        if (read (stable_control[0], &command, 1) != 1 || command != 'S')
            rc = 40;
        void *stable_ctx = rc == 0 ? zlink_ctx_new () : NULL;
        void *stable_node =
          stable_ctx
            ? new_started_node (
                stable_ctx, mesh_name, "node-c", 0, 0, false, 0,
                stable_endpoint)
            : NULL;
        if (!stable_node
            || submit_peer_intent (stable_node, requester_endpoint) == 0)
            rc = 41;
        zlink_mesh_peer_entry_t stable_peer;
        if (rc == 0
            && !wait_peer_rid_state (
              stable_node, "node-a", ZLINK_MESH_PEER_ADMITTED,
              &stable_peer))
            rc = 42;
        void *stable_spot = NULL;
        zlink_spot_status_t stable_spot_status;
        memset (&stable_spot_status, 0, sizeof (stable_spot_status));
        stable_spot_status.struct_size = sizeof (stable_spot_status);
        stable_spot_status.version = 1;
        if (rc == 0
            && (zlink_mesh_node_entry_spot (stable_node, &stable_spot)
                  != ZLINK_CONFIG_OK
                || zlink_spot_status (stable_spot, &stable_spot_status)
                     != ZLINK_CONFIG_OK))
            rc = 43;
        const char admitted = rc == 0 ? 'A' : 'E';
        if (write (stable_control[0], &admitted, 1) != 1)
            rc = 44;
        if (rc == 0) {
            char generation_line[32];
            const int generation_size =
              snprintf (generation_line, sizeof (generation_line), "%llu\n",
                        (unsigned long long)
                          stable_spot_status.lifecycle_generation);
            if (generation_size <= 0
                || write (stable_control[0], generation_line,
                          static_cast<size_t> (generation_size))
                     != generation_size)
                rc = 45;
        }
        command = 0;
        if (rc == 0
            && (read (stable_control[0], &command, 1) != 1
                || command != 'P'))
            rc = 46;
        if (rc == 0) {
            zlink_mesh_claim_t claim;
            take_ready_claim (
              stable_node, ZLINK_MESH_OWNER_SPOT,
              ZLINK_MESH_READY_APPLICATION, &claim);
            void *batch =
              zlink_mesh_receive_batch_new (8, 32, 64 * 1024);
            zlink_mesh_receive_requirements_t requirements;
            memset (&requirements, 0, sizeof (requirements));
            if (!batch
                || zlink_mesh_claim_recv_batch (
                     &claim, batch, &requirements, ZLINK_RECV_FLAGS_NONE)
                     != ZLINK_RECV_OK)
                rc = 47;
            const zlink_mesh_receive_record_t *record =
              batch ? zlink_mesh_receive_batch_data (batch) : NULL;
            if (rc == 0
                && (zlink_mesh_receive_batch_count (batch) != 1
                    || record->kind != ZLINK_MESH_RECORD_SPOT_REQUEST))
                rc = 48;
            if (rc == 0) {
                zlink_msg_t reply;
                make_payload (&reply, "spot-pong-stable");
                if (zlink_mesh_reply (
                      &record->reply_token, &reply, 1,
                      ZLINK_SEND_FLAGS_NONE)
                    != ZLINK_SUBMIT_OK)
                    rc = 49;
            }
            zlink_mesh_claim_release (&claim);
            if (batch)
                zlink_mesh_receive_batch_destroy (&batch);
            const char replied = rc == 0 ? 'R' : 'E';
            if (write (stable_control[0], &replied, 1) != 1)
                rc = 50;
        }
        command = 0;
        if (rc == 0
            && (read (stable_control[0], &command, 1) != 1
                || command != 'Q'))
            rc = 51;
        if (stable_spot)
            zlink_spot_destroy (&stable_spot);
        if (stable_node) {
            zlink_mesh_node_shutdown (stable_node, 1000);
            if (zlink_mesh_node_destroy (&stable_node) != ZLINK_CLOSE_OK)
                rc = 52;
        }
        if (stable_ctx && zlink_ctx_term (stable_ctx) != 0)
            rc = 53;
        close (stable_control[0]);
        fflush (NULL);
        std::_Exit (rc);
    }

    close (first_control[0]);
    close (second_control[0]);
    close (stable_control[0]);

    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    void *node = new_started_node (
      ctx, mesh_name, "node-a", 0, 0, false, 0, requester_endpoint);
    TEST_ASSERT_TRUE (submit_peer_intent (node, endpoint) != 0);
    TEST_ASSERT_TRUE (submit_peer_intent (node, stable_endpoint) != 0);

    const char start = 'S';
    TEST_ASSERT_EQUAL_INT (
      1, static_cast<int> (write (stable_control[1], &start, 1)));
    char stable_state = 0;
    TEST_ASSERT_EQUAL_INT (
      1, static_cast<int> (read (stable_control[1], &stable_state, 1)));
    TEST_ASSERT_EQUAL_INT ('A', stable_state);
    char stable_generation_text[32];
    read_endpoint (
      stable_control[1], stable_generation_text,
      sizeof (stable_generation_text));
    const uint64_t stable_spot_generation =
      strtoull (stable_generation_text, NULL, 10);
    TEST_ASSERT_TRUE (stable_spot_generation != 0);

    TEST_ASSERT_EQUAL_INT (
      1, static_cast<int> (write (first_control[1], &start, 1)));
    char child_state = 0;
    TEST_ASSERT_EQUAL_INT (
      1, static_cast<int> (read (first_control[1], &child_state, 1)));
    TEST_ASSERT_EQUAL_INT ('B', child_state);
    char generation_text[32];
    read_endpoint (
      first_control[1], generation_text, sizeof (generation_text));
    const uint64_t first_spot_generation =
      strtoull (generation_text, NULL, 10);
    TEST_ASSERT_TRUE (first_spot_generation != 0);
    TEST_ASSERT_EQUAL_INT (
      1, static_cast<int> (read (first_control[1], &child_state, 1)));
    TEST_ASSERT_EQUAL_INT ('A', child_state);

    zlink_mesh_peer_entry_t peer;
    TEST_ASSERT_TRUE_MESSAGE (
      wait_peer_rid_state (
        node, "node-b", ZLINK_MESH_PEER_ADMITTED, &peer),
      "first outbound admission did not complete");
    TEST_ASSERT_TRUE_MESSAGE (
      wait_admitted_count (node, 2),
      "unrelated live peer was not retained beside the restart target");
    zlink_msg_t payload;
    make_payload (&payload, "ping-first");
    zlink_mesh_operation_id_t operation;
    memset (&operation, 0, sizeof (operation));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_OK,
      zlink_mesh_node_request_to_node (
        node, &peer.routing_id, NULL, &payload, 1, &operation,
        ZLINK_SEND_FLAGS_NONE, 10000));
    TEST_ASSERT_EQUAL_INT (
      0, wait_node_completion_payload (node, operation, "pong-reconnect"));

    void *spot = NULL;
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK, zlink_mesh_node_entry_spot (node, &spot));
    make_payload (&payload, "spot-ping-first");
    memset (&operation, 0, sizeof (operation));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_OK,
      zlink_spot_request_to_spot (
        spot, &peer.routing_id, &peer.routing_id, first_spot_generation,
        NULL, &payload, 1, &operation, ZLINK_SEND_FLAGS_NONE, 10000));
    TEST_ASSERT_EQUAL_INT (
      0, wait_spot_completion (
           node, spot, operation, "spot-pong-reconnect"));
    TEST_ASSERT_EQUAL_INT (
      1, static_cast<int> (read (first_control[1], &child_state, 1)));
    TEST_ASSERT_EQUAL_INT ('R', child_state);

    TEST_ASSERT_EQUAL_INT (0, kill (first_child, SIGKILL));
    int child_status = 0;
    TEST_ASSERT_EQUAL_INT (
      first_child, waitpid (first_child, &child_status, 0));
    TEST_ASSERT_TRUE (WIFSIGNALED (child_status));
    //  The replacement process starts only after the listener and reciprocal
    //  connector owned by the killed process have both left the kernel. The
    //  persistent connector can move ERROR to CONNECTING faster than a
    //  snapshot observes the transient ERROR state, so replacement traffic is
    //  the authoritative recovery assertion below.
    msleep (500);

    TEST_ASSERT_EQUAL_INT (
      1, static_cast<int> (write (second_control[1], &start, 1)));
    TEST_ASSERT_EQUAL_INT (
      1, static_cast<int> (read (second_control[1], &child_state, 1)));
    TEST_ASSERT_EQUAL_INT ('B', child_state);
    read_endpoint (
      second_control[1], generation_text, sizeof (generation_text));
    const uint64_t second_spot_generation =
      strtoull (generation_text, NULL, 10);
    TEST_ASSERT_TRUE (second_spot_generation != 0);
    TEST_ASSERT_EQUAL_INT (
      1, static_cast<int> (read (second_control[1], &child_state, 1)));
    TEST_ASSERT_EQUAL_INT ('A', child_state);
    TEST_ASSERT_TRUE_MESSAGE (
      wait_peer_rid_state (
        node, "node-b", ZLINK_MESH_PEER_ADMITTED, &peer),
      "configured outbound intent did not auto-reconnect");
    TEST_ASSERT_TRUE_MESSAGE (
      wait_peer_rid_state (
        node, "node-c", ZLINK_MESH_PEER_ADMITTED, &peer),
      "unrelated live peer was disrupted by the same-RID restart");
    TEST_ASSERT_TRUE_MESSAGE (
      wait_peer_rid_state (
        node, "node-b", ZLINK_MESH_PEER_ADMITTED, &peer),
      "restart target disappeared after the live-peer assertion");

    make_payload (&payload, "ping-second");
    memset (&operation, 0, sizeof (operation));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_OK,
      zlink_mesh_node_request_to_node (
        node, &peer.routing_id, NULL, &payload, 1, &operation,
        ZLINK_SEND_FLAGS_NONE, 10000));
    TEST_ASSERT_EQUAL_INT (
      0, wait_node_completion_payload (node, operation, "pong-reconnect"));

    make_payload (&payload, "spot-ping-second");
    memset (&operation, 0, sizeof (operation));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_OK,
      zlink_spot_request_to_spot (
        spot, &peer.routing_id, &peer.routing_id, second_spot_generation,
        NULL, &payload, 1, &operation, ZLINK_SEND_FLAGS_NONE, 10000));
    TEST_ASSERT_EQUAL_INT (
      0, wait_spot_completion (
           node, spot, operation, "spot-pong-reconnect"));
    TEST_ASSERT_EQUAL_INT (
      1, static_cast<int> (read (second_control[1], &child_state, 1)));
    TEST_ASSERT_EQUAL_INT ('R', child_state);

    TEST_ASSERT_EQUAL_INT (
      second_child, waitpid (second_child, &child_status, 0));
    TEST_ASSERT_TRUE (
      WIFEXITED (child_status) && WEXITSTATUS (child_status) == 0);
    TEST_ASSERT_TRUE_MESSAGE (
      wait_peer_rid_state (
        node, "node-c", ZLINK_MESH_PEER_ADMITTED, &peer),
      "surviving alternate peer was not retained after the restart target exited");
    const char request_stable = 'P';
    TEST_ASSERT_EQUAL_INT (
      1, static_cast<int> (
           write (stable_control[1], &request_stable, 1)));
    make_payload (&payload, "spot-ping-stable");
    memset (&operation, 0, sizeof (operation));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_OK,
      zlink_spot_request_to_spot (
        spot, &peer.routing_id, &peer.routing_id,
        stable_spot_generation, NULL, &payload, 1, &operation,
        ZLINK_SEND_FLAGS_NONE, 10000));
    TEST_ASSERT_EQUAL_INT (
      0, wait_spot_completion (
           node, spot, operation, "spot-pong-stable"));
    TEST_ASSERT_EQUAL_INT (
      1, static_cast<int> (
           read (stable_control[1], &stable_state, 1)));
    TEST_ASSERT_EQUAL_INT ('R', stable_state);
    const char quit = 'Q';
    TEST_ASSERT_EQUAL_INT (
      1, static_cast<int> (write (stable_control[1], &quit, 1)));
    TEST_ASSERT_EQUAL_INT (
      stable_child, waitpid (stable_child, &child_status, 0));
    TEST_ASSERT_TRUE (
      WIFEXITED (child_status) && WEXITSTATUS (child_status) == 0);
    close (first_control[1]);
    close (second_control[1]);
    close (stable_control[1]);
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_spot_destroy (&spot));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_REQUEST_OK, zlink_mesh_node_shutdown (node, 1000));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CLOSE_OK, zlink_mesh_node_destroy (&node));
    TEST_ASSERT_EQUAL_INT (0, zlink_ctx_term (ctx));
}

//  Discovery can replace one RID at a different endpoint while the surviving
//  peer stays running. Reciprocal intents must converge again on the
//  deterministic physical direction after the old intent is removed.
void test_reciprocal_peer_replacement_moves_same_rid_to_new_endpoint ()
{
    char low_endpoint[64];
    char old_endpoint[64];
    char replacement_endpoint[64];
    memset (low_endpoint, 0, sizeof (low_endpoint));
    memset (old_endpoint, 0, sizeof (old_endpoint));
    memset (replacement_endpoint, 0, sizeof (replacement_endpoint));

    int control[2];
    TEST_ASSERT_EQUAL_INT (0, socketpair (AF_UNIX, SOCK_STREAM, 0, control));
    fflush (NULL);
    const pid_t child = fork ();
    TEST_ASSERT_TRUE (child >= 0);
    if (child == 0) {
        close (control[1]);
        int rc = 0;
        char child_low_endpoint[64];
        read_endpoint (
          control[0], child_low_endpoint, sizeof (child_low_endpoint));
        if (child_low_endpoint[0] == '\0')
            rc = 9;
        void *old_ctx = zlink_ctx_new ();
        void *old_high = old_ctx
                           ? new_started_node (
                               old_ctx, mesh_name, "play2")
                           : NULL;
        if (rc == 0 && old_high)
            publish_endpoint (old_high, control[0]);
        if (!old_high
            || submit_peer_intent (old_high, child_low_endpoint) == 0)
            rc = 10;
        zlink_mesh_peer_entry_t peer;
        if (rc == 0
            && !wait_peer_rid_state (
              old_high, "play1", ZLINK_MESH_PEER_ADMITTED, &peer))
            rc = 11;
        if (rc == 0 && write (control[0], "A", 1) != 1)
            rc = 12;
        char command = 0;
        if (rc == 0
            && (read (control[0], &command, 1) != 1 || command != 'R'))
            rc = 13;
        if (old_high) {
            zlink_mesh_node_shutdown (old_high, 1000);
            if (zlink_mesh_node_destroy (&old_high) != ZLINK_CLOSE_OK)
                rc = 14;
        }
        if (old_ctx && zlink_ctx_term (old_ctx) != 0)
            rc = 15;

        void *replacement_ctx = rc == 0 ? zlink_ctx_new () : NULL;
        void *replacement = replacement_ctx
                              ? new_started_node (
                                  replacement_ctx, mesh_name, "play2")
                              : NULL;
        if (rc == 0
            && (!replacement
                || submit_peer_intent (
                     replacement, child_low_endpoint) == 0))
            rc = 16;
        if (rc == 0) {
            publish_endpoint (replacement, control[0]);
            if (write (control[0], "N", 1) != 1)
                rc = 17;
        }
        if (rc == 0
            && !wait_peer_rid_state (
              replacement, "play1", ZLINK_MESH_PEER_ADMITTED, &peer))
            rc = 18;
        msleep (500);
        if (rc == 0
            && !wait_peer_rid_state (
              replacement, "play1", ZLINK_MESH_PEER_ADMITTED, &peer))
            rc = 19;
        if (rc == 0 && write (control[0], "A", 1) != 1)
            rc = 20;
        if (rc == 0
            && (read (control[0], &command, 1) != 1 || command != 'Q'))
            rc = 21;
        if (replacement) {
            zlink_mesh_node_shutdown (replacement, 1000);
            if (zlink_mesh_node_destroy (&replacement) != ZLINK_CLOSE_OK)
                rc = 22;
        }
        if (replacement_ctx && zlink_ctx_term (replacement_ctx) != 0)
            rc = 23;
        close (control[0]);
        fflush (NULL);
        std::_Exit (rc);
    }

    close (control[0]);
    void *low_ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (low_ctx);
    void *low = new_started_node (low_ctx, mesh_name, "play1");
    publish_endpoint (low, control[1]);
    read_endpoint (control[1], old_endpoint, sizeof (old_endpoint));
    TEST_ASSERT_TRUE (old_endpoint[0] != '\0');
    const uint64_t old_intent = submit_peer_intent (low, old_endpoint);
    TEST_ASSERT_NOT_EQUAL (0, old_intent);
    zlink_mesh_peer_entry_t peer;
    TEST_ASSERT_TRUE_MESSAGE (
      wait_peer_rid_state (low, "play2", ZLINK_MESH_PEER_ADMITTED, &peer),
      "survivor did not admit the original peer");
    const uint64_t old_generation = peer.lifecycle_generation;
    char child_state = 0;
    TEST_ASSERT_EQUAL_INT (
      1, static_cast<int> (read (control[1], &child_state, 1)));
    TEST_ASSERT_EQUAL_INT ('A', child_state);
    TEST_ASSERT_EQUAL_INT (
      1, static_cast<int> (write (control[1], "R", 1)));
    read_endpoint (
      control[1], replacement_endpoint, sizeof (replacement_endpoint));
    TEST_ASSERT_TRUE (replacement_endpoint[0] != '\0');
    TEST_ASSERT_EQUAL_INT (
      1, static_cast<int> (read (control[1], &child_state, 1)));
    TEST_ASSERT_EQUAL_INT ('N', child_state);
    bool old_endpoint_disconnected = false;
    for (int waited = 0; waited < 15000 && !old_endpoint_disconnected;
         waited += poll_step_ms) {
        zlink_mesh_peer_entry_t entries[8];
        memset (entries, 0, sizeof (entries));
        for (size_t i = 0; i < 8; ++i) {
            entries[i].struct_size = sizeof (entries[i]);
            entries[i].version = 1;
        }
        size_t count = 8;
        bool admitted_old_endpoint = false;
        if (zlink_mesh_node_peers (low, entries, &count) == ZLINK_CONFIG_OK) {
            for (size_t i = 0; i < count; ++i) {
                admitted_old_endpoint =
                  admitted_old_endpoint
                  || (entries[i].state == ZLINK_MESH_PEER_ADMITTED
                      && strcmp (entries[i].endpoint, old_endpoint) == 0);
            }
            old_endpoint_disconnected = !admitted_old_endpoint;
        }
        if (!old_endpoint_disconnected)
            msleep (poll_step_ms);
    }
    TEST_ASSERT_TRUE_MESSAGE (
      old_endpoint_disconnected,
      "survivor retained the old admitted endpoint after peer replacement");
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONNECT_OK,
      zlink_mesh_node_remove_peer_connection (low, old_intent));

    //  The higher RID also dials, matching framework reciprocal discovery.
    //  The lower RID's new endpoint intent is the selected physical route.
    zlink_mesh_peer_connection_options_t options;
    memset (&options, 0, sizeof (options));
    options.struct_size = sizeof (options);
    options.version = 1;
    options.endpoint = replacement_endpoint;
    options.endpoint_size = strlen (replacement_endpoint);
    options.has_expected_rid = 1;
    options.expected_rid.size = strlen ("play2");
    memcpy (options.expected_rid.data, "play2", options.expected_rid.size);
    uint64_t replacement_intent = 0;
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONNECT_OK,
      zlink_mesh_node_connect_peer (low, &options, &replacement_intent));
    TEST_ASSERT_NOT_EQUAL (0, replacement_intent);

    TEST_ASSERT_TRUE_MESSAGE (
      wait_peer_rid_state (low, "play2", ZLINK_MESH_PEER_ADMITTED, &peer),
      "survivor did not admit the same-RID replacement at its new endpoint");
    msleep (500);
    TEST_ASSERT_TRUE_MESSAGE (
      wait_peer_rid_state (low, "play2", ZLINK_MESH_PEER_ADMITTED, &peer),
      "replacement admission did not survive reciprocal handover");
    TEST_ASSERT_EQUAL_INT (
      1, static_cast<int> (read (control[1], &child_state, 1)));
    TEST_ASSERT_EQUAL_INT ('A', child_state);
    TEST_ASSERT_NOT_EQUAL (old_generation, peer.lifecycle_generation);
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONNECT_CONFLICT,
      zlink_mesh_node_disconnect_peer (
        low, &peer.routing_id, old_generation));
    TEST_ASSERT_EQUAL_INT (ESTALE, zlink_errno ());
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONNECT_OK,
      zlink_mesh_node_disconnect_peer (
        low, &peer.routing_id, peer.lifecycle_generation));
    TEST_ASSERT_EQUAL_INT (
      1, static_cast<int> (write (control[1], "Q", 1)));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_REQUEST_OK, zlink_mesh_node_shutdown (low, 1000));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_node_destroy (&low));
    TEST_ASSERT_EQUAL_INT (0, zlink_ctx_term (low_ctx));
    close (control[1]);
    int child_status = 0;
    TEST_ASSERT_EQUAL_INT (child, waitpid (child, &child_status, 0));
    TEST_ASSERT_TRUE_MESSAGE (
      WIFEXITED (child_status) && WEXITSTATUS (child_status) == 0,
      "same-RID replacement child reported failure");
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

//  Empty Channel membership is a valid peer descriptor. Two such peers can
//  use RID-addressed Node send and request/reply in both directions, while the
//  public descriptor query reports an empty Channel set.
void test_zero_membership_peers_exchange_node_messages_both_directions ()
{
    run_two_process_case (
      [] (int endpoint_fd, pid_t) {
          void *ctx = zlink_ctx_new ();
          TEST_ASSERT_NOT_NULL (ctx);
          void *node = new_started_zero_membership_node (
            ctx, "zero-peer-mesh", "zero-a");
          publish_endpoint (node, endpoint_fd);

          zlink_mesh_peer_entry_t peer;
          TEST_ASSERT_TRUE_MESSAGE (
            wait_peer_state (node, ZLINK_MESH_PEER_ADMITTED, &peer),
            "zero-membership peer A did not admit peer B");
          TEST_ASSERT_EQUAL_UINT32 (0, peer.channel_count);
          size_t channel_count = 99;
          TEST_ASSERT_EQUAL_INT (
            ZLINK_CONFIG_OK,
            zlink_mesh_node_peer_channels (
              node, &peer.routing_id, peer.lifecycle_generation, NULL, NULL,
              &channel_count));
          TEST_ASSERT_EQUAL_UINT64 (0, channel_count);

          char phase = 0;
          TEST_ASSERT_EQUAL_INT (1, (int) read (endpoint_fd, &phase, 1));
          TEST_ASSERT_EQUAL_INT ('S', phase);
          TEST_ASSERT_EQUAL_INT (
            0, receive_node_application_record (
                 node, ZLINK_MESH_RECORD_NODE_SEND, "send-b-to-a", NULL));
          TEST_ASSERT_EQUAL_INT (1, (int) write (endpoint_fd, "A", 1));

          TEST_ASSERT_EQUAL_INT (
            0, receive_node_application_record (
                 node, ZLINK_MESH_RECORD_NODE_REQUEST, "request-b-to-a",
                 "reply-a-to-b"));
          TEST_ASSERT_EQUAL_INT (1, (int) read (endpoint_fd, &phase, 1));
          TEST_ASSERT_EQUAL_INT ('R', phase);

          zlink_msg_t payload;
          make_payload (&payload, "send-a-to-b");
          TEST_ASSERT_EQUAL_INT (
            ZLINK_SUBMIT_OK,
            zlink_mesh_node_send_to_node (
              node, &peer.routing_id, NULL, &payload, 1,
              ZLINK_SEND_FLAGS_NONE));
          zlink_msg_close (&payload);
          TEST_ASSERT_EQUAL_INT (1, (int) write (endpoint_fd, "P", 1));
          TEST_ASSERT_EQUAL_INT (1, (int) read (endpoint_fd, &phase, 1));
          TEST_ASSERT_EQUAL_INT ('s', phase);

          make_payload (&payload, "request-a-to-b");
          zlink_mesh_operation_id_t operation_id;
          memset (&operation_id, 0, sizeof (operation_id));
          TEST_ASSERT_EQUAL_INT (
            ZLINK_SUBMIT_OK,
            zlink_mesh_node_request_to_node (
              node, &peer.routing_id, NULL, &payload, 1, &operation_id,
              ZLINK_SEND_FLAGS_NONE, 2000));
          zlink_msg_close (&payload);
          TEST_ASSERT_EQUAL_INT (1, (int) write (endpoint_fd, "Q", 1));
          TEST_ASSERT_EQUAL_INT (
            0, wait_node_completion (
                 node, operation_id, "reply-b-to-a", NULL, 0));
          TEST_ASSERT_EQUAL_INT (1, (int) read (endpoint_fd, &phase, 1));
          TEST_ASSERT_EQUAL_INT ('C', phase);

          TEST_ASSERT_EQUAL_INT (
            ZLINK_REQUEST_OK, zlink_mesh_node_shutdown (node, 1000));
          TEST_ASSERT_EQUAL_INT (
            ZLINK_CLOSE_OK, zlink_mesh_node_destroy (&node));
          TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_ctx_term (ctx));
      },
      [] (int endpoint_fd) -> int {
          char endpoint[ZLINK_MESH_ENDPOINT_MAX + 1];
          read_endpoint (endpoint_fd, endpoint, sizeof (endpoint));
          void *ctx = zlink_ctx_new ();
          if (!ctx)
              return 10;
          void *node = new_started_zero_membership_node (
            ctx, "zero-peer-mesh", "zero-b");
          if (submit_peer_intent (node, endpoint) == 0)
              return 11;
          zlink_mesh_peer_entry_t peer;
          if (!wait_peer_state (node, ZLINK_MESH_PEER_ADMITTED, &peer)
              || peer.channel_count != 0)
              return 12;
          size_t channel_count = 99;
          if (zlink_mesh_node_peer_channels (
                node, &peer.routing_id, peer.lifecycle_generation, NULL, NULL,
                &channel_count)
                != ZLINK_CONFIG_OK
              || channel_count != 0)
              return 13;

          zlink_msg_t payload;
          make_payload (&payload, "send-b-to-a");
          if (zlink_mesh_node_send_to_node (
                node, &peer.routing_id, NULL, &payload, 1,
                ZLINK_SEND_FLAGS_NONE)
              != ZLINK_SUBMIT_OK)
              return 14;
          zlink_msg_close (&payload);
          if (write (endpoint_fd, "S", 1) != 1)
              return 15;
          char phase = 0;
          if (read (endpoint_fd, &phase, 1) != 1 || phase != 'A')
              return 16;

          make_payload (&payload, "request-b-to-a");
          zlink_mesh_operation_id_t operation_id;
          memset (&operation_id, 0, sizeof (operation_id));
          if (zlink_mesh_node_request_to_node (
                node, &peer.routing_id, NULL, &payload, 1, &operation_id,
                ZLINK_SEND_FLAGS_NONE, 2000)
              != ZLINK_SUBMIT_OK)
              return 17;
          zlink_msg_close (&payload);
          if (wait_node_completion (
                node, operation_id, "reply-a-to-b", NULL, 0)
              != 0)
              return 18;
          if (write (endpoint_fd, "R", 1) != 1)
              return 19;

          if (read (endpoint_fd, &phase, 1) != 1 || phase != 'P')
              return 20;
          if (receive_node_application_record (
                node, ZLINK_MESH_RECORD_NODE_SEND, "send-a-to-b", NULL)
              != 0)
              return 21;
          if (write (endpoint_fd, "s", 1) != 1)
              return 22;

          if (read (endpoint_fd, &phase, 1) != 1 || phase != 'Q')
              return 23;
          if (receive_node_application_record (
                node, ZLINK_MESH_RECORD_NODE_REQUEST, "request-a-to-b",
                "reply-b-to-a")
              != 0)
              return 24;
          if (write (endpoint_fd, "C", 1) != 1)
              return 25;

          const int shutdown_rc = zlink_mesh_node_shutdown (node, 1000);
          const int destroy_rc = zlink_mesh_node_destroy (&node);
          const int term_rc = zlink_ctx_term (ctx);
          return shutdown_rc == ZLINK_REQUEST_OK
                     && destroy_rc == ZLINK_CLOSE_OK
                     && term_rc == ZLINK_CLOSE_OK
                   ? 0
                   : 26;
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
          const uint8_t expected_metadata[] = {
            1, 1, 3, 'k', 'e', 'y', 0, 5, 'v', 'a', 'l', 'u', 'e'};
          TEST_ASSERT_EQUAL_UINT64 (sizeof (expected_metadata),
                                    record->application_metadata_size);
          TEST_ASSERT_EQUAL_MEMORY (expected_metadata, record->application_metadata,
                                    sizeof (expected_metadata));

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
          const uint8_t metadata_bytes[] = {
            1, 1, 3, 'k', 'e', 'y', 0, 5, 'v', 'a', 'l', 'u', 'e'};
          const zlink_mesh_metadata_view_t metadata = {
            metadata_bytes, sizeof (metadata_bytes)};
          zlink_mesh_operation_id_t operation_id;
          memset (&operation_id, 0, sizeof (operation_id));
          if (zlink_spot_request_to_spot (spot, &entry.routing_id, &entry.routing_id,
                                          target_generation, &metadata, &payload, 1, &operation_id,
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

//  Reciprocal peer intents can replace the physical ROUTER direction after
//  admission. A Spot-origin multicast must keep using the merged logical
//  peer descriptor and deliver to an exact subscription on the other node.
void test_reciprocal_peer_spot_publish_exact_subscription ()
{
    run_two_process_case (
      [&] (int endpoint_fd, pid_t) {
          void *ctx = zlink_ctx_new ();
          TEST_ASSERT_NOT_NULL (ctx);
          void *node = new_started_node (ctx, mesh_name, "node-a");
          void *spot = NULL;
          TEST_ASSERT_EQUAL_INT (
            ZLINK_CONFIG_OK, zlink_mesh_node_entry_spot (node, &spot));
          TEST_ASSERT_EQUAL_INT (
            ZLINK_CONFIG_OK,
            zlink_spot_set_subscription (
              spot, channel_name, "tictactoe.player.milestone",
              ZLINK_SPOT_SUBSCRIPTION_EXACT));

          publish_endpoint (node, endpoint_fd);
          char child_endpoint[ZLINK_MESH_ENDPOINT_MAX + 1];
          read_endpoint (
            endpoint_fd, child_endpoint, sizeof (child_endpoint));
          TEST_ASSERT_TRUE (child_endpoint[0] != '\0');
          TEST_ASSERT_NOT_EQUAL (
            0, submit_peer_intent (node, child_endpoint));
          TEST_ASSERT_TRUE_MESSAGE (
            wait_admitted_count (node, 1),
            "publisher did not admit reciprocal subscriber peer");

          char phase = 0;
          TEST_ASSERT_EQUAL_INT (
            1, static_cast<int> (read (endpoint_fd, &phase, 1)));
          TEST_ASSERT_EQUAL_HEX8 ('R', phase);

          zlink_msg_t part;
          make_payload (&part, "player-win");
          zlink_mesh_publish_detail_t detail;
          memset (&detail, 0, sizeof (detail));
          detail.struct_size = sizeof (detail);
          detail.version = 1;
          TEST_ASSERT_EQUAL_INT (
            ZLINK_SUBMIT_OK,
            zlink_spot_publish (
              spot, channel_name, "tictactoe.player.milestone", NULL,
              &part, 1, &detail, ZLINK_SEND_FLAGS_NONE));
          zlink_msg_close (&part);
          TEST_ASSERT_EQUAL_UINT (1, detail.snapshot_remote_target_count);
          TEST_ASSERT_EQUAL_UINT (1, detail.admitted_remote_target_count);
          TEST_ASSERT_EQUAL_UINT (0, detail.dropped_remote_target_count);
          TEST_ASSERT_EQUAL_UINT (0, detail.unreachable_remote_target_count);
          TEST_ASSERT_EQUAL_UINT (1, detail.snapshot_local_spot_count);
          TEST_ASSERT_EQUAL_UINT (1, detail.admitted_local_spot_count);
          TEST_ASSERT_EQUAL_UINT (0, detail.dropped_local_spot_count);

          phase = 0;
          TEST_ASSERT_EQUAL_INT (
            1, static_cast<int> (read (endpoint_fd, &phase, 1)));
          TEST_ASSERT_EQUAL_HEX8 ('D', phase);
          TEST_ASSERT_EQUAL_INT (
            ZLINK_CLOSE_OK, zlink_spot_destroy (&spot));
          TEST_ASSERT_EQUAL_INT (
            ZLINK_REQUEST_OK, zlink_mesh_node_shutdown (node, 2000));
          TEST_ASSERT_EQUAL_INT (
            ZLINK_CLOSE_OK, zlink_mesh_node_destroy (&node));
          TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_ctx_term (ctx));
      },
      [&] (int endpoint_fd) -> int {
          char parent_endpoint[ZLINK_MESH_ENDPOINT_MAX + 1];
          read_endpoint (
            endpoint_fd, parent_endpoint, sizeof (parent_endpoint));
          if (parent_endpoint[0] == '\0')
              return 10;

          void *ctx = zlink_ctx_new ();
          if (!ctx)
              return 11;
          void *node = new_started_node (ctx, mesh_name, "node-b");
          void *spot = NULL;
          if (zlink_mesh_node_entry_spot (node, &spot) != ZLINK_CONFIG_OK)
              return 12;
          if (zlink_spot_set_subscription (
                spot, channel_name, "tictactoe.player.milestone",
                ZLINK_SPOT_SUBSCRIPTION_EXACT)
              != ZLINK_CONFIG_OK)
              return 13;

          publish_endpoint (node, endpoint_fd);
          if (submit_peer_intent (node, parent_endpoint) == 0)
              return 14;
          if (!wait_admitted_count (node, 1))
              return 15;
          if (write (endpoint_fd, "R", 1) != 1)
              return 16;

          void *ready = zlink_mesh_ready_batch_new (8);
          void *batch = zlink_mesh_receive_batch_new (8, 16, 4096);
          if (!ready || !batch)
              return 17;
          int result = 18;
          for (int waited = 0; waited < poll_deadline_ms;
               waited += poll_step_ms) {
              uint32_t residue = 0;
              const zlink_recv_result_t ready_rc =
                zlink_mesh_node_drain_ready (
                  node, ZLINK_MESH_READY_APPLICATION, ready, &residue,
                  ZLINK_RECV_FLAGS_DONTWAIT);
              if (ready_rc == ZLINK_RECV_NO_DATA) {
                  msleep (poll_step_ms);
                  continue;
              }
              if (ready_rc != ZLINK_RECV_OK) {
                  result = 19;
                  break;
              }
              const zlink_mesh_ready_record_t *ready_records =
                zlink_mesh_ready_batch_data (ready);
              const size_t ready_count =
                zlink_mesh_ready_batch_count (ready);
              bool consumed = false;
              for (size_t i = 0; i < ready_count && !consumed; ++i) {
                  if (ready_records[i].owner_kind != ZLINK_MESH_OWNER_SPOT)
                      continue;
                  zlink_mesh_claim_t claim;
                  if (zlink_mesh_ready_batch_take_claim (
                        ready, i, &claim)
                      != ZLINK_CONFIG_OK) {
                      result = 20;
                      break;
                  }
                  zlink_mesh_receive_requirements_t requirements;
                  memset (&requirements, 0, sizeof (requirements));
                  if (zlink_mesh_claim_recv_batch (
                        &claim, batch, &requirements,
                        ZLINK_RECV_FLAGS_NONE)
                      != ZLINK_RECV_OK) {
                      zlink_mesh_claim_release (&claim);
                      result = 21;
                      break;
                  }
                  const zlink_mesh_receive_record_t *records =
                    zlink_mesh_receive_batch_data (batch);
                  const zlink_msg_t *parts =
                    zlink_mesh_receive_batch_parts (batch);
                  if (zlink_mesh_receive_batch_count (batch) != 1
                      || records[0].kind
                           != ZLINK_MESH_RECORD_SPOT_MULTICAST
                      || !records[0].topic
                      || records[0].topic_size
                           != strlen ("tictactoe.player.milestone")
                      || memcmp (
                           records[0].topic,
                           "tictactoe.player.milestone",
                           records[0].topic_size)
                           != 0
                      || records[0].part_count != 1
                      || zlink_msg_size (
                           &parts[records[0].part_offset])
                           != strlen ("player-win")
                      || memcmp (
                           zlink_msg_data (const_cast<zlink_msg_t *> (
                             &parts[records[0].part_offset])),
                           "player-win", strlen ("player-win"))
                           != 0) {
                      result = 22;
                  } else {
                      result = 0;
                  }
                  zlink_mesh_receive_batch_reset (batch);
                  zlink_mesh_claim_release (&claim);
                  consumed = true;
              }
              zlink_mesh_ready_batch_reset (ready);
              if (consumed || result >= 19)
                  break;
          }
          zlink_mesh_ready_batch_destroy (&ready);
          zlink_mesh_receive_batch_destroy (&batch);
          if (result == 0 && write (endpoint_fd, "D", 1) != 1)
              result = 23;
          if (zlink_spot_destroy (&spot) != ZLINK_CLOSE_OK
              && result == 0)
              result = 24;
          zlink_mesh_node_shutdown (node, 2000);
          if (zlink_mesh_node_destroy (&node) != ZLINK_CLOSE_OK
              && result == 0)
              result = 25;
          if (zlink_ctx_term (ctx) != ZLINK_CLOSE_OK && result == 0)
              result = 26;
          return result;
      });
}

//  Logical Multicast selects one logical RID after an eligible peer arrives
//  inbound and then gains a reciprocal local intent. Two independent outbound
//  peers advertising weight zero remain excluded from the same snapshot.
void test_multicast_selects_inbound_eligible_peer_over_zero_weight_outbound_peers ()
{
    const char topic[] = "tictactoe.player.milestone";
    const char payload[] = "player-win";
    int peer_fds[3][2];
    for (size_t i = 0; i < 3; ++i)
        TEST_ASSERT_EQUAL_INT (
          0, socketpair (AF_UNIX, SOCK_STREAM, 0, peer_fds[i]));

    fflush (NULL);
    pid_t children[3] = {-1, -1, -1};
    for (size_t child_index = 0; child_index < 3; ++child_index) {
        children[child_index] = fork ();
        TEST_ASSERT_TRUE (children[child_index] >= 0);
        if (children[child_index] != 0)
            continue;

        for (size_t i = 0; i < 3; ++i) {
            close (peer_fds[i][1]);
            if (i != child_index)
                close (peer_fds[i][0]);
        }
        setup_test_environment (30);
        void *ctx = zlink_ctx_new ();
        if (!ctx)
            std::_Exit (10);
        const char *rid = child_index == 0
                            ? "eligible-inbound"
                            : (child_index == 1 ? "zero-outbound-a"
                                                : "zero-outbound-b");
        void *node = new_started_node (ctx, mesh_name, rid);
        if (zlink_mesh_node_set_channel_weight (
              node, channel_name, child_index == 0 ? 100 : 0)
            != ZLINK_CONFIG_OK)
            std::_Exit (11);

        void *spot = NULL;
        int result = 0;
        if (child_index == 0) {
            if (zlink_mesh_node_entry_spot (node, &spot) != ZLINK_CONFIG_OK
                || zlink_spot_set_subscription (
                     spot, channel_name, topic,
                     ZLINK_SPOT_SUBSCRIPTION_EXACT)
                     != ZLINK_CONFIG_OK)
                std::_Exit (12);
            char publisher_endpoint[ZLINK_MESH_ENDPOINT_MAX + 1];
            read_endpoint (
              peer_fds[child_index][0], publisher_endpoint,
              sizeof (publisher_endpoint));
            publish_endpoint (node, peer_fds[child_index][0]);
            if (submit_peer_intent (node, publisher_endpoint) == 0
                || !wait_admitted_count (node, 1))
                std::_Exit (13);
            if (write (peer_fds[child_index][0], "R", 1) != 1)
                std::_Exit (14);
            char command = 0;
            if (read (peer_fds[child_index][0], &command, 1) != 1
                || command != 'P')
                std::_Exit (15);
            const int receive_result = receive_one_spot_multicast (
              node, topic, payload, 3000);
            write_result_line (
              peer_fds[child_index][0], receive_result);
        } else {
            publish_endpoint (node, peer_fds[child_index][0]);
            char command = 0;
            if (read (peer_fds[child_index][0], &command, 1) != 1
                || command != 'Q')
                result = 16;
        }

        if (spot && zlink_spot_destroy (&spot) != ZLINK_CLOSE_OK
            && result == 0)
            result = 17;
        zlink_mesh_node_shutdown (node, 1000);
        if (zlink_mesh_node_destroy (&node) != ZLINK_CLOSE_OK
            && result == 0)
            result = 18;
        if (zlink_ctx_term (ctx) != ZLINK_CLOSE_OK && result == 0)
            result = 19;
        close (peer_fds[child_index][0]);
        fflush (NULL);
        std::_Exit (result);
    }

    for (size_t i = 0; i < 3; ++i)
        close (peer_fds[i][0]);

    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    void *publisher_node =
      new_started_node (ctx, mesh_name, "publisher-node");
    void *publisher_spot = NULL;
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK,
      zlink_mesh_node_entry_spot (publisher_node, &publisher_spot));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK,
      zlink_spot_set_subscription (
        publisher_spot, channel_name, topic,
        ZLINK_SPOT_SUBSCRIPTION_EXACT));

    publish_endpoint (publisher_node, peer_fds[0][1]);
    char eligible_endpoint[ZLINK_MESH_ENDPOINT_MAX + 1];
    read_endpoint (
      peer_fds[0][1], eligible_endpoint, sizeof (eligible_endpoint));
    TEST_ASSERT_NOT_EQUAL (
      0, submit_peer_intent (publisher_node, eligible_endpoint));
    for (size_t i = 1; i < 3; ++i) {
        char endpoint[ZLINK_MESH_ENDPOINT_MAX + 1];
        read_endpoint (peer_fds[i][1], endpoint, sizeof (endpoint));
        TEST_ASSERT_NOT_EQUAL (
          0, submit_peer_intent (publisher_node, endpoint));
    }
    TEST_ASSERT_TRUE_MESSAGE (
      wait_admitted_count (publisher_node, 3),
      "publisher did not admit all three peers");
    TEST_ASSERT_TRUE_MESSAGE (
      wait_peer_channel_weight (
        publisher_node, "eligible-inbound", 100, 3000),
      "eligible inbound peer descriptor was not ready");
    TEST_ASSERT_TRUE_MESSAGE (
      wait_peer_channel_weight (
        publisher_node, "zero-outbound-a", 0, 3000),
      "first zero-weight outbound peer descriptor was not ready");
    TEST_ASSERT_TRUE_MESSAGE (
      wait_peer_channel_weight (
        publisher_node, "zero-outbound-b", 0, 3000),
      "second zero-weight outbound peer descriptor was not ready");
    zlink_mesh_peer_entry_t peer_entries[8];
    memset (peer_entries, 0, sizeof (peer_entries));
    for (size_t i = 0; i < 8; ++i) {
        peer_entries[i].struct_size = sizeof (peer_entries[i]);
        peer_entries[i].version = 1;
    }
    size_t peer_entry_count = 8;
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK,
      zlink_mesh_node_peers (
        publisher_node, peer_entries, &peer_entry_count));
    size_t eligible_entry_count = 0;
    zlink_mesh_peer_source_t eligible_source = ZLINK_MESH_PEER_MANUAL;
    for (size_t i = 0; i < peer_entry_count; ++i) {
        if (peer_entries[i].routing_id.size
              != strlen ("eligible-inbound")
            || memcmp (peer_entries[i].routing_id.data,
                       "eligible-inbound",
                       peer_entries[i].routing_id.size)
                 != 0)
            continue;
        ++eligible_entry_count;
        eligible_source = peer_entries[i].source;
    }
    char eligible_ready = 0;
    TEST_ASSERT_EQUAL_INT (
      1, static_cast<int> (read (peer_fds[0][1], &eligible_ready, 1)));
    TEST_ASSERT_EQUAL_HEX8 ('R', eligible_ready);

    zlink_msg_t part;
    make_payload (&part, payload);
    zlink_mesh_publish_detail_t detail;
    memset (&detail, 0, sizeof (detail));
    detail.struct_size = sizeof (detail);
    detail.version = 1;
    const zlink_submit_result_t publish_result = zlink_spot_publish (
      publisher_spot, channel_name, topic, NULL, &part, 1, &detail,
      ZLINK_SEND_FLAGS_NONE);
    zlink_msg_close (&part);

    TEST_ASSERT_EQUAL_INT (
      1, static_cast<int> (write (peer_fds[0][1], "P", 1)));
    for (size_t i = 1; i < 3; ++i)
        TEST_ASSERT_EQUAL_INT (
          1, static_cast<int> (write (peer_fds[i][1], "Q", 1)));
    char eligible_result_text[32];
    read_endpoint (
      peer_fds[0][1], eligible_result_text,
      sizeof (eligible_result_text));
    const int eligible_receive_result = atoi (eligible_result_text);

    for (size_t i = 0; i < 3; ++i) {
        close (peer_fds[i][1]);
        int child_status = 0;
        pid_t wait_rc = 0;
        for (int waited = 0; waited < 5000; waited += poll_step_ms) {
            wait_rc = waitpid (children[i], &child_status, WNOHANG);
            TEST_ASSERT_TRUE (wait_rc >= 0);
            if (wait_rc == children[i])
                break;
            msleep (poll_step_ms);
        }
        if (wait_rc != children[i]) {
            kill (children[i], SIGKILL);
            (void) waitpid (children[i], &child_status, 0);
            TEST_FAIL_MESSAGE ("multicast peer child did not exit in time");
        }
        TEST_ASSERT_TRUE_MESSAGE (
          WIFEXITED (child_status) && WEXITSTATUS (child_status) == 0,
          "multicast peer child reported failure");
    }

    TEST_ASSERT_EQUAL_INT (
      ZLINK_CLOSE_OK, zlink_spot_destroy (&publisher_spot));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_REQUEST_OK,
      zlink_mesh_node_shutdown (publisher_node, 2000));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CLOSE_OK, zlink_mesh_node_destroy (&publisher_node));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_ctx_term (ctx));

    TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_OK, publish_result);
    TEST_ASSERT_EQUAL_UINT (1, detail.snapshot_remote_target_count);
    TEST_ASSERT_EQUAL_UINT (1, detail.admitted_remote_target_count);
    TEST_ASSERT_EQUAL_UINT (0, detail.dropped_remote_target_count);
    TEST_ASSERT_EQUAL_UINT (0, detail.unreachable_remote_target_count);
    TEST_ASSERT_EQUAL_UINT (1, detail.snapshot_local_spot_count);
    TEST_ASSERT_EQUAL_UINT (1, detail.admitted_local_spot_count);
    TEST_ASSERT_EQUAL_UINT (0, detail.dropped_local_spot_count);
    TEST_ASSERT_EQUAL_INT_MESSAGE (
      0, eligible_receive_result,
      "eligible inbound peer did not receive Logical Multicast");
    TEST_ASSERT_EQUAL_UINT (3, peer_entry_count);
    TEST_ASSERT_EQUAL_UINT (1, eligible_entry_count);
    TEST_ASSERT_EQUAL_INT (ZLINK_MESH_PEER_MIXED, eligible_source);
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

          void *waiting_publisher = zlink_mesh_node_publisher_new (node);
          TEST_ASSERT_NOT_NULL (waiting_publisher);
          std::atomic<bool> waiting_publish_started (false);
          std::atomic<int> waiting_publish_result (ZLINK_SUBMIT_OK);
          std::thread waiting_sender ([&] {
              zlink_mesh_publish_detail_t detail;
              memset (&detail, 0, sizeof (detail));
              detail.struct_size = sizeof (detail);
              detail.version = 1;
              waiting_publish_started.store (true, std::memory_order_release);
              waiting_publish_result.store (
                zlink_mesh_node_publisher_publish (
                  waiting_publisher, channel_name, "blocked.remote", NULL,
                  &part, 1, &detail, ZLINK_SEND_FLAGS_NONE),
                std::memory_order_release);
          });
          while (!waiting_publish_started.load (std::memory_order_acquire))
              std::this_thread::yield ();
          msleep (100);

          std::atomic<bool> peer_query_completed (false);
          std::atomic<int> peer_query_result (ZLINK_CONFIG_INTERNAL_ERROR);
          std::thread peer_query ([&] {
              size_t count = 0;
              peer_query_result.store (
                zlink_mesh_node_peers (node, NULL, &count),
                std::memory_order_release);
              peer_query_completed.store (true, std::memory_order_release);
          });
          for (int waited = 0;
               waited < 500
               && !peer_query_completed.load (std::memory_order_acquire);
               waited += poll_step_ms)
              msleep (poll_step_ms);
          const bool peer_query_completed_before_shutdown =
            peer_query_completed.load (std::memory_order_acquire);

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
          waiting_sender.join ();
          peer_query.join ();
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
          TEST_ASSERT_TRUE_MESSAGE (
            peer_query_completed_before_shutdown,
            "a sender waiting for the wire mutex retained the node mutex and blocked peer queries");
          TEST_ASSERT_EQUAL_INT (
            ZLINK_CONFIG_OK,
            peer_query_result.load (std::memory_order_acquire));
          TEST_ASSERT_NOT_EQUAL (ZLINK_SUBMIT_OK,
                                 terminal_result.load (std::memory_order_acquire));
          TEST_ASSERT_NOT_EQUAL (
            ZLINK_SUBMIT_OK,
            waiting_publish_result.load (std::memory_order_acquire));
          TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK,
                                 update_result.load (std::memory_order_acquire));
          TEST_ASSERT_EQUAL_INT (1,
                                 resume_write_result.load (std::memory_order_acquire));
          TEST_ASSERT_EQUAL_INT (0,
                                 resume_kill_result.load (std::memory_order_acquire));

          zlink_msg_close (&part);
          TEST_ASSERT_EQUAL_INT (
            ZLINK_CLOSE_OK,
            zlink_mesh_node_publisher_destroy (&waiting_publisher));
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

//  A DONTWAIT capacity rejection marks the ROUTER pipe temporarily inactive.
//  A following blocking publish must keep treating that admitted route as
//  connected: it may accept after write credit returns or time out with
//  backpressure, but it must not report an unreachable peer.
void test_blocking_publish_after_dontwait_backpressure_stays_connected ()
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
            new_started_node (ctx, mesh_name, "node-a", 0, 1, false, 10);
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
          zlink_mesh_monitor_open_options_t monitor_options;
          memset (&monitor_options, 0, sizeof (monitor_options));
          monitor_options.struct_size = sizeof (monitor_options);
          monitor_options.version = 1;
          void *monitor =
            zlink_mesh_node_monitor_open (node, &monitor_options);
          TEST_ASSERT_NOT_NULL (monitor);
          zlink_msg_t part;
          TEST_ASSERT_EQUAL_INT (0, zlink_msg_init_size (&part, 4 * 1024 * 1024));
          memset (zlink_msg_data (&part), 0x5A, zlink_msg_size (&part));

          zlink_routing_id_t target_rid;
          memset (&target_rid, 0, sizeof (target_rid));
          target_rid.size = strlen ("node-b");
          memcpy (target_rid.data, "node-b", target_rid.size);
          bool direct_backpressured = false;
          for (int attempt = 0; attempt < 1000 && !direct_backpressured;
               ++attempt) {
              const zlink_submit_result_t direct_rc =
                zlink_mesh_node_send_to_node (
                  node, &target_rid, NULL, &part, 1,
                  ZLINK_SEND_FLAGS_DONTWAIT);
              if (direct_rc == ZLINK_SUBMIT_BACKPRESSURED) {
                  TEST_ASSERT_EQUAL_INT (EAGAIN, zlink_errno ());
                  direct_backpressured = true;
              } else {
                  TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_OK, direct_rc);
              }
          }
          TEST_ASSERT_TRUE_MESSAGE (
            direct_backpressured,
            "DONTWAIT direct send did not reach ROUTER HWM");
          TEST_ASSERT_EQUAL_INT (
            ZLINK_SUBMIT_BACKPRESSURED,
            zlink_mesh_node_send_to_node (
              node, &target_rid, NULL, &part, 1,
              ZLINK_SEND_FLAGS_DONTWAIT));
          TEST_ASSERT_EQUAL_INT (EAGAIN, zlink_errno ());

          bool backpressured = false;
          for (int attempt = 0; attempt < 1000 && !backpressured; ++attempt) {
              zlink_mesh_publish_detail_t detail;
              memset (&detail, 0, sizeof (detail));
              detail.struct_size = sizeof (detail);
              detail.version = 1;
              const zlink_submit_result_t rc =
                zlink_mesh_node_publisher_publish (
                  publisher, channel_name, "blocked.remote", NULL, &part, 1,
                  &detail, ZLINK_SEND_FLAGS_DONTWAIT);
              if (rc == ZLINK_SUBMIT_BACKPRESSURED) {
                  TEST_ASSERT_EQUAL_UINT (1, detail.snapshot_remote_target_count);
                  TEST_ASSERT_EQUAL_UINT (0, detail.admitted_remote_target_count);
                  TEST_ASSERT_EQUAL_UINT (1, detail.dropped_remote_target_count);
                  TEST_ASSERT_EQUAL_UINT (0, detail.unreachable_remote_target_count);
                  backpressured = true;
              } else {
                  TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_OK, rc);
              }
          }
          TEST_ASSERT_TRUE_MESSAGE (backpressured,
                                    "DONTWAIT publish did not reach ROUTER HWM");

          zlink_mesh_monitor_event_t pressure_event;
          bool pressure_seen = false;
          for (int waited = 0; waited < 2000; waited += poll_step_ms) {
              memset (&pressure_event, 0, sizeof (pressure_event));
              pressure_event.struct_size = sizeof (pressure_event);
              pressure_event.version = 1;
              const zlink_recv_result_t monitor_rc =
                zlink_mesh_node_monitor_recv (monitor, &pressure_event,
                                              ZLINK_RECV_FLAGS_DONTWAIT);
              if (monitor_rc == ZLINK_RECV_NO_DATA) {
                  msleep (poll_step_ms);
                  continue;
              }
              TEST_ASSERT_EQUAL_INT (ZLINK_RECV_OK, monitor_rc);
              if (pressure_event.kind == ZLINK_MESH_MONITOR_MULTICAST_DROPPED)
                  TEST_FAIL_MESSAGE (
                    "remote-only ROUTER pressure must not be reported as a local multicast drop");
              if (pressure_event.kind != ZLINK_MESH_MONITOR_BACKPRESSURED
                  || pressure_event.snapshot_remote_target_count != 1)
                  continue;
              pressure_seen = true;
              break;
          }
          TEST_ASSERT_TRUE_MESSAGE (
            pressure_seen,
            "remote multicast ROUTER pressure did not emit BACKPRESSURED");
          TEST_ASSERT_EQUAL_UINT (1, pressure_event.snapshot_remote_target_count);
          TEST_ASSERT_EQUAL_UINT (0, pressure_event.admitted_remote_target_count);
          TEST_ASSERT_EQUAL_UINT (1, pressure_event.dropped_remote_target_count);
          TEST_ASSERT_EQUAL_UINT (0, pressure_event.snapshot_local_spot_count);
          TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_BACKPRESSURED,
                                 pressure_event.result_code);
          TEST_ASSERT_EQUAL_INT (EAGAIN, pressure_event.failure_errno);

          zlink_mesh_publish_detail_t detail;
          memset (&detail, 0, sizeof (detail));
          detail.struct_size = sizeof (detail);
          detail.version = 1;
          const std::chrono::steady_clock::time_point started =
            std::chrono::steady_clock::now ();
          const zlink_submit_result_t rc =
            zlink_mesh_node_publisher_publish (
              publisher, channel_name, "blocked.remote", NULL, &part, 1,
              &detail, ZLINK_SEND_FLAGS_NONE);
          const int64_t elapsed_ms =
            std::chrono::duration_cast<std::chrono::milliseconds> (
              std::chrono::steady_clock::now () - started)
              .count ();
          TEST_ASSERT_TRUE (rc == ZLINK_SUBMIT_OK
                            || rc == ZLINK_SUBMIT_BACKPRESSURED);
          TEST_ASSERT_EQUAL_UINT (1, detail.snapshot_remote_target_count);
          TEST_ASSERT_EQUAL_UINT (0, detail.unreachable_remote_target_count);
          TEST_ASSERT_EQUAL_UINT (
            detail.snapshot_remote_target_count,
            detail.admitted_remote_target_count + detail.dropped_remote_target_count);
          if (rc == ZLINK_SUBMIT_OK) {
              TEST_ASSERT_EQUAL_UINT (1, detail.admitted_remote_target_count);
              TEST_ASSERT_EQUAL_UINT (0, detail.dropped_remote_target_count);
          } else {
              TEST_ASSERT_TRUE_MESSAGE (elapsed_ms >= 5,
                                        "blocking publish did not wait through SNDTIMEO");
              TEST_ASSERT_EQUAL_UINT (0, detail.admitted_remote_target_count);
              TEST_ASSERT_EQUAL_UINT (1, detail.dropped_remote_target_count);
          }

          const unsigned char resume = 0x5A;
          TEST_ASSERT_EQUAL_INT (
            1, static_cast<int> (write (resume_pipe[1], &resume, 1)));
          close (resume_pipe[1]);
          TEST_ASSERT_EQUAL_INT (0, kill (child_pid, SIGCONT));

          wait_node_send_ready (node, "node-b");

          zlink_msg_close (&part);
          TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK,
                                 zlink_mesh_node_monitor_close (&monitor));
          TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK,
                                 zlink_mesh_node_publisher_destroy (&publisher));
          TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK,
                                 zlink_mesh_node_shutdown (node, 2000));
          TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_node_destroy (&node));
          TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_ctx_term (ctx));
      },
      [&] (int endpoint_fd) -> int {
          close (ready_pipe[0]);
          close (resume_pipe[1]);
          char endpoint[ZLINK_MESH_ENDPOINT_MAX + 1];
          read_endpoint (endpoint_fd, endpoint, sizeof (endpoint));
          void *ctx = zlink_ctx_new ();
          if (!ctx)
              return 10;
          if (zlink_ctx_set (ctx, ZLINK_CTX_OPT_BLOCKY, 0) != ZLINK_CONFIG_OK)
              return 11;
          void *node = new_started_node (ctx, mesh_name, "node-b", 0, 1);
          void *spot = NULL;
          if (zlink_mesh_node_entry_spot (node, &spot) != ZLINK_CONFIG_OK)
              return 12;
          if (zlink_spot_set_subscription (spot, channel_name, "blocked.",
                                           ZLINK_SPOT_SUBSCRIPTION_PREFIX)
              != ZLINK_CONFIG_OK)
              return 13;
          if (submit_peer_intent (node, endpoint) == 0)
              return 14;
          zlink_mesh_peer_entry_t entry;
          if (!wait_peer_state (node, ZLINK_MESH_PEER_ADMITTED, &entry))
              return 15;
          const unsigned char ready = 0xA5;
          if (write (ready_pipe[1], &ready, 1) != 1)
              return 16;
          close (ready_pipe[1]);
          unsigned char resume = 0;
          if (read (resume_pipe[0], &resume, 1) != 1 || resume != 0x5A)
              return 17;
          close (resume_pipe[0]);
          msleep (1000);
          int result = 0;
          if (zlink_spot_destroy (&spot) != ZLINK_CLOSE_OK)
              result = 18;
          if (zlink_mesh_node_shutdown (node, 1000) != ZLINK_REQUEST_OK
              && result == 0)
              result = 19;
          if (zlink_mesh_node_destroy (&node) != ZLINK_CLOSE_OK && result == 0)
              result = 20;
          if (zlink_ctx_term (ctx) != ZLINK_CLOSE_OK && result == 0)
              result = 21;
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

          zlink_routing_id_t expected_session_rid;
          memset (&expected_session_rid, 0, sizeof (expected_session_rid));
          TEST_ASSERT_TRUE (read_routing_id (endpoint_fd, &expected_session_rid));

          //  The first remote message originates from the child's bound raw
          //  STREAM session. Its receive record must retain both the physical
          //  MeshNode identity and the exact session routing id.
          zlink_mesh_claim_t claim;
          take_ready_claim (node, ZLINK_MESH_OWNER_ACTOR,
                            ZLINK_MESH_READY_APPLICATION, &claim);
          void *batch = zlink_mesh_receive_batch_new (1, 1, 64 * 1024);
          TEST_ASSERT_NOT_NULL (batch);
          zlink_mesh_receive_requirements_t requirements;
          memset (&requirements, 0, sizeof (requirements));
          TEST_ASSERT_EQUAL_INT (
            ZLINK_RECV_OK,
            zlink_mesh_claim_recv_batch (&claim, batch, &requirements,
                                         ZLINK_RECV_FLAGS_NONE));
          TEST_ASSERT_EQUAL_UINT (1, zlink_mesh_receive_batch_count (batch));
          const zlink_mesh_receive_record_t *record =
            zlink_mesh_receive_batch_data (batch);
          TEST_ASSERT_EQUAL_INT (ZLINK_MESH_RECORD_ACTOR_SEND, record->kind);
          TEST_ASSERT_EQUAL_UINT8 (strlen ("node-b"), record->source_node_rid.size);
          TEST_ASSERT_EQUAL_MEMORY ("node-b", record->source_node_rid.data,
                                    strlen ("node-b"));
          TEST_ASSERT_EQUAL_UINT8 (expected_session_rid.size,
                                   record->source_spot_rid.size);
          TEST_ASSERT_EQUAL_MEMORY (expected_session_rid.data,
                                    record->source_spot_rid.data,
                                    expected_session_rid.size);
          const uint64_t expected_binding_generation =
            record->source_binding_generation;
          TEST_ASSERT_NOT_EQUAL (0, expected_binding_generation);
          const zlink_msg_t *parts = zlink_mesh_receive_batch_parts (batch);
          TEST_ASSERT_EQUAL_UINT (1, record->part_count);
          TEST_ASSERT_EQUAL_UINT (strlen ("session-ping"),
                                  zlink_msg_size (&parts[record->part_offset]));
          TEST_ASSERT_EQUAL_MEMORY (
            "session-ping",
            zlink_msg_data (const_cast<zlink_msg_t *> (
              &parts[record->part_offset])),
            strlen ("session-ping"));
          TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK,
                                 zlink_mesh_claim_release (&claim));
          TEST_ASSERT_EQUAL_INT (
            ZLINK_CLOSE_OK, zlink_mesh_receive_batch_destroy (&batch));
          TEST_ASSERT_EQUAL_INT (1, (int) write (endpoint_fd, "S", 1));

          char race_ready = 0;
          TEST_ASSERT_EQUAL_INT (1, (int) read (endpoint_fd, &race_ready, 1));
          TEST_ASSERT_EQUAL_INT ('P', race_ready);
          zlink_msg_t raced_old_push;
          make_payload (&raced_old_push, "owner-race-stale");
          TEST_ASSERT_EQUAL_INT (
            ZLINK_SUBMIT_OK,
            zlink_mesh_node_actor_send_bound_session (
              node, &actor, expected_binding_generation, &raced_old_push, 1,
              ZLINK_SEND_FLAGS_NONE));
          zlink_msg_close (&raced_old_push);
          TEST_ASSERT_EQUAL_INT (1, (int) write (endpoint_fd, "Q", 1));

          uint64_t rebound_binding_generation = 0;
          TEST_ASSERT_TRUE (
            read_u64 (endpoint_fd, &rebound_binding_generation));
          TEST_ASSERT_NOT_EQUAL (0, rebound_binding_generation);
          TEST_ASSERT_NOT_EQUAL (expected_binding_generation,
                                 rebound_binding_generation);

          //  Answer the remote actor request.
          take_ready_claim (node, ZLINK_MESH_OWNER_ACTOR, ZLINK_MESH_READY_APPLICATION, &claim);
          batch = zlink_mesh_receive_batch_new (8, 32, 64 * 1024);
          TEST_ASSERT_NOT_NULL (batch);
          memset (&requirements, 0, sizeof (requirements));
          TEST_ASSERT_EQUAL_INT (ZLINK_RECV_OK,
                                 zlink_mesh_claim_recv_batch (&claim, batch, &requirements,
                                                              ZLINK_RECV_FLAGS_NONE));
          TEST_ASSERT_EQUAL_UINT (1, zlink_mesh_receive_batch_count (batch));
          record = zlink_mesh_receive_batch_data (batch);
          TEST_ASSERT_EQUAL_INT (ZLINK_MESH_RECORD_ACTOR_REQUEST, record->kind);
          zlink_msg_t reply;
          make_payload (&reply, "actor-pong");
          TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_OK,
                                 zlink_mesh_reply (&record->reply_token, &reply, 1,
                                                   ZLINK_SEND_FLAGS_NONE));
          TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_claim_release (&claim));
          TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_receive_batch_destroy (&batch));

          //  The request above was submitted after the child completed
          //  unbind/rebind. Its arrival is a wire-order barrier: the new
          //  binding announcement has already updated this source cache.
          zlink_msg_t stale_bound_push;
          make_payload (&stale_bound_push, "stale-bound-push");
          TEST_ASSERT_EQUAL_INT (
            ZLINK_SUBMIT_INVALID_STATE,
            zlink_mesh_node_actor_send_bound_session (
              node, &actor, expected_binding_generation, &stale_bound_push, 1,
              ZLINK_SEND_FLAGS_NONE));
          TEST_ASSERT_EQUAL_INT (ESTALE, zlink_errno ());
          zlink_msg_close (&stale_bound_push);

          zlink_msg_t rebound_push;
          make_payload (&rebound_push, "rebound-push");
          TEST_ASSERT_EQUAL_INT (
            ZLINK_SUBMIT_OK,
            zlink_mesh_node_actor_send_bound_session (
              node, &actor, rebound_binding_generation, &rebound_push, 1,
              ZLINK_SEND_FLAGS_NONE));
          zlink_msg_close (&rebound_push);

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

          void *stream = zlink_socket (ctx, ZLINK_SOCKET_STREAM);
          if (!stream)
              return 28;
          const int notify = 1;
          const int recv_timeout_ms = 5000;
          if (zlink_set_stream_option (
                stream, ZLINK_STREAM_OPT_NOTIFY, &notify, sizeof (notify))
                != ZLINK_CONFIG_OK
              || zlink_set_option (stream, ZLINK_OPT_RCVTIMEO, &recv_timeout_ms,
                                   sizeof (recv_timeout_ms))
                   != ZLINK_CONFIG_OK
              || zlink_bind (stream, "tcp://127.0.0.1:0") != ZLINK_BIND_OK)
              return 29;
          char stream_endpoint[ZLINK_MESH_ENDPOINT_MAX + 1];
          size_t stream_endpoint_size = sizeof (stream_endpoint);
          if (zlink_get_option (stream, ZLINK_OPT_LAST_ENDPOINT, stream_endpoint,
                                &stream_endpoint_size)
              != ZLINK_CONFIG_OK)
              return 30;
          stream_endpoint[sizeof (stream_endpoint) - 1] = '\0';
          void *session_service = zlink_stream_session_service_new (node, stream);
          if (!session_service
              || zlink_stream_session_service_start (session_service) != ZLINK_CONFIG_OK)
              return 31;
          const int stream_client_fd = connect_raw_stream_client (stream_endpoint);
          if (stream_client_fd < 0 || send (stream_client_fd, "!", 1, 0) != 1)
              return 32;
          zlink_msg_t connect_part;
          if (zlink_msg_init (&connect_part) != 0)
              return 33;
          const zlink_routing_id_t *source_session_rid = NULL;
          zlink_part_flag_t has_more = ZLINK_PART_MORE;
          if (zlink_recv_part (stream, &source_session_rid, &connect_part, &has_more,
                               ZLINK_RECV_FLAGS_NONE)
                != ZLINK_RECV_OK
              || !source_session_rid || has_more != ZLINK_PART_FINAL)
              return 34;
          const zlink_routing_id_t session_rid = *source_session_rid;
          zlink_msg_close (&connect_part);

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

          //  A remote bind is not a lossy route announcement. The Actor
          //  owner rejects a fabricated generation and the source commits no
          //  binding before that terminal ACK is observed.
          zlink_actor_ref_t stale_actor = location.actor;
          ++stale_actor.generation;
          zlink_mesh_operation_id_t stale_bind_op;
          memset (&stale_bind_op, 0, sizeof (stale_bind_op));
          if (zlink_stream_session_bind_actor (
                session_service, &session_rid, &stale_actor,
                &stale_bind_op, 1000)
              != ZLINK_SUBMIT_OK)
              return 52;
          if (wait_node_completion (
                node, stale_bind_op, NULL, NULL, 0,
                ZLINK_REQUEST_CONFLICT, ESTALE)
              != 0)
              return 53;
          size_t rejected_binding_count = 0;
          if (zlink_stream_session_bindings (
                session_service, &session_rid, NULL,
                &rejected_binding_count)
                != ZLINK_CONFIG_OK
              || rejected_binding_count != 0)
              return 54;

          zlink_mesh_operation_id_t bind_op;
          if (zlink_stream_session_bind_actor (
                session_service, &session_rid, &location.actor, &bind_op, 1000)
              != ZLINK_SUBMIT_OK)
              return 35;
          {
              const int rc =
                wait_node_completion_payload (node, bind_op, NULL);
              if (rc != 0)
                  return rc;
          }

          zlink_stream_session_binding_t binding;
          memset (&binding, 0, sizeof (binding));
          binding.struct_size = sizeof (binding);
          binding.version = ZLINK_STREAM_SESSION_ABI_VERSION;
          size_t binding_count = 1;
          if (zlink_stream_session_bindings (
                session_service, &session_rid, &binding, &binding_count)
                != ZLINK_CONFIG_OK
              || binding_count != 1 || binding.binding_generation == 0)
              return 41;

          if (!write_routing_id (endpoint_fd, session_rid))
              return 37;
          zlink_msg_t session_payload;
          if (zlink_msg_init_size (&session_payload, strlen ("session-ping")) != 0)
              return 38;
          memcpy (zlink_msg_data (&session_payload), "session-ping",
                  strlen ("session-ping"));
          if (zlink_stream_session_send_to_actor (
                session_service, &session_rid, &location.actor, NULL,
                &session_payload, 1, ZLINK_SEND_FLAGS_NONE)
              != ZLINK_SUBMIT_OK) {
              zlink_msg_close (&session_payload);
              return 39;
          }
          zlink_msg_close (&session_payload);
          char session_ack = 0;
          if (read (endpoint_fd, &session_ack, 1) != 1 || session_ack != 'S')
              return 40;

          zlink_test_stream_session_pause_bound_session_admit (1);
          if (write (endpoint_fd, "P", 1) != 1)
              return 42;
          char race_sent = 0;
          if (read (endpoint_fd, &race_sent, 1) != 1 || race_sent != 'Q')
              return 43;
          bool delivery_paused = false;
          for (int waited = 0; waited < poll_deadline_ms;
               waited += poll_step_ms) {
              if (zlink_test_stream_session_bound_session_admit_paused ()) {
                  delivery_paused = true;
                  break;
              }
              msleep (poll_step_ms);
          }
          if (!delivery_paused)
              return 44;

          zlink_mesh_operation_id_t unbind_op;
          memset (&unbind_op, 0, sizeof (unbind_op));
          if (zlink_stream_session_unbind_actor (
                session_service, &session_rid, &location.actor,
                binding.binding_generation, &unbind_op, 1000)
              != ZLINK_SUBMIT_OK)
              return 45;
          {
              const int rc =
                wait_node_completion_payload (node, unbind_op, NULL);
              if (rc != 0)
                  return rc;
          }

          memset (&bind_op, 0, sizeof (bind_op));
          if (zlink_stream_session_bind_actor (
                session_service, &session_rid, &location.actor, &bind_op, 1000)
              != ZLINK_SUBMIT_OK)
              return 46;
          //  Let the paused old-generation delivery revalidate before this
          //  process consumes the owner ACK for the replacement binding.
          zlink_test_stream_session_pause_bound_session_admit (0);
          {
              const int rc =
                wait_node_completion_payload (node, bind_op, NULL);
              if (rc != 0)
                  return rc;
          }
          memset (&binding, 0, sizeof (binding));
          binding.struct_size = sizeof (binding);
          binding.version = ZLINK_STREAM_SESSION_ABI_VERSION;
          binding_count = 1;
          if (zlink_stream_session_bindings (
                session_service, &session_rid, &binding, &binding_count)
                != ZLINK_CONFIG_OK
              || binding_count != 1 || binding.binding_generation == 0)
              return 47;
          if (!write_u64 (endpoint_fd, binding.binding_generation))
              return 48;

          //  Re-admit the Mesh peer while the physical STREAM binding stays
          //  active. The new peer lifetime must recover the reverse route
          //  from the binding replay rather than an Actor payload side effect.
          if (zlink_mesh_node_disconnect_peer (
                node, &entry.routing_id, entry.lifecycle_generation)
              != ZLINK_CONNECT_OK)
              return 49;
          if (submit_peer_intent (node, endpoint) == 0)
              return 50;
          zlink_mesh_peer_entry_t rebound_peer;
          if (!wait_peer_state (
                node, ZLINK_MESH_PEER_ADMITTED, &rebound_peer))
              return 51;

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
          char bound_payload[32];
          const ssize_t bound_size =
            recv (stream_client_fd, bound_payload, sizeof (bound_payload), 0);
          if (bound_size != static_cast<ssize_t> (strlen ("rebound-push"))
              || memcmp (bound_payload, "rebound-push",
                         strlen ("rebound-push")) != 0)
              return 36;

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

          zlink_stream_session_service_shutdown (session_service, 1000);
          zlink_stream_session_service_destroy (&session_service);
          zlink_close (stream);
          close (stream_client_fd);
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

          zlink_msg_t reply;
          make_payload (&reply, "join-accepted");
          TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_OK,
                                 zlink_actor_join_reply (&record->reply_token,
                                                         ZLINK_ACTOR_JOIN_ACCEPTED, &reply, 1,
                                                         ZLINK_SEND_FLAGS_NONE));
          zlink_msg_close (&reply);
          TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_claim_release (&claim));
          TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_receive_batch_destroy (&batch));

          //  The accepted admission record above is the OnActorJoin request.
          //  Commit publishes a separate infrastructure lifecycle record so
          //  the target can run OnJoinedActor without treating it as another
          //  admission.
          take_ready_claim (
            node, ZLINK_MESH_OWNER_SPOT, ZLINK_MESH_READY_INFRASTRUCTURE, &claim);
          batch = zlink_mesh_receive_batch_new (8, 32, 64 * 1024);
          TEST_ASSERT_NOT_NULL (batch);
          memset (&requirements, 0, sizeof (requirements));
          TEST_ASSERT_EQUAL_INT (
            ZLINK_RECV_OK,
            zlink_mesh_claim_recv_batch (
              &claim, batch, &requirements, ZLINK_RECV_FLAGS_NONE));
          TEST_ASSERT_EQUAL_UINT (1, zlink_mesh_receive_batch_count (batch));
          record = zlink_mesh_receive_batch_data (batch);
          TEST_ASSERT_EQUAL_INT (ZLINK_MESH_RECORD_SPOT_CONTROL, record->kind);
          TEST_ASSERT_NOT_EQUAL (ZLINK_MESH_OPERATION_ACTOR_JOIN, record->operation_kind);
          TEST_ASSERT_TRUE (record->kind_data_size >= sizeof (zlink_actor_control_record_t));
          control =
            static_cast<const zlink_actor_control_record_t *> (record->kind_data);
          TEST_ASSERT_EQUAL_INT (ZLINK_ACTOR_LIFECYCLE_JOINED, control->kind);
          TEST_ASSERT_EQUAL_UINT64 (2, control->current_membership_epoch);
          TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_claim_release (&claim));
          TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_receive_batch_destroy (&batch));
          TEST_ASSERT_EQUAL_INT (
            1, static_cast<int> (write (endpoint_fd, "J", 1)));

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
              const int rc = wait_node_completion (
                node, join_op, "join-accepted", &completion, sizeof (completion));
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

          zlink_mesh_claim_t claim;
          take_ready_claim (
            node, ZLINK_MESH_OWNER_SPOT, ZLINK_MESH_READY_INFRASTRUCTURE, &claim);
          void *batch = zlink_mesh_receive_batch_new (8, 32, 64 * 1024);
          if (!batch)
              return 19;
          zlink_mesh_receive_requirements_t requirements;
          memset (&requirements, 0, sizeof (requirements));
          if (zlink_mesh_claim_recv_batch (
                &claim, batch, &requirements, ZLINK_RECV_FLAGS_NONE)
              != ZLINK_RECV_OK)
              return 20;
          const zlink_mesh_receive_record_t *record =
            zlink_mesh_receive_batch_data (batch);
          if (zlink_mesh_receive_batch_count (batch) != 1
              || record->kind != ZLINK_MESH_RECORD_SPOT_CONTROL
              || record->operation_kind == ZLINK_MESH_OPERATION_ACTOR_JOIN
              || record->kind_data_size < sizeof (zlink_actor_control_record_t))
              return 21;
          const zlink_actor_control_record_t *control =
            static_cast<const zlink_actor_control_record_t *> (record->kind_data);
          if (control->kind != ZLINK_ACTOR_LIFECYCLE_LEFT
              || control->previous_membership_epoch != 1
              || control->current_membership_epoch != 2)
              return 22;
          zlink_mesh_claim_release (&claim);
          zlink_mesh_receive_batch_destroy (&batch);
          char joined_observed = 0;
          if (read (endpoint_fd, &joined_observed, 1) != 1
              || joined_observed != 'J')
              return 23;

          zlink_mesh_node_shutdown (node, 1000);
          if (zlink_mesh_node_destroy (&node) != ZLINK_CLOSE_OK)
              return 26;
          if (zlink_ctx_term (ctx) != 0)
              return 27;
          return 0;
      });
}
//  Transfer fence: the test process plays the deterministic location
//  authority. Exercise both a normal framework session that predates source
//  prepare and a remote session owner installed after readiness. Target
//  reverse pushes must remain retained through source commit without loss or
//  reordering in either participant shape.
void run_remote_actor_transfer_fence (bool bind_before_prepare_)
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
            char commit_line[64];
            read_endpoint (authority_pipe[0], commit_line, sizeof (commit_line));
            unsigned long long binding_generation = 0;
            unsigned long long expected_actor_messages = 0;
            unsigned long long expected_session_messages = 0;
            if (sscanf (commit_line, "C %llu %llu %llu", &binding_generation,
                        &expected_actor_messages, &expected_session_messages)
                  != 3
                || binding_generation == 0 || expected_actor_messages == 0
                || (bind_before_prepare_ && expected_session_messages == 0)) {
                rc = 33;
                break;
            }

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

            zlink_mesh_node_status_t target_status;
            node_status (node, &target_status);
            zlink_actor_ref_t current_actor = prepare.actor;
            current_actor.node_rid = target_status.routing_id;

            //  The post-fence Actor message surfaces on the target lane.
            zlink_mesh_claim_t claim;
            take_ready_claim (node, ZLINK_MESH_OWNER_ACTOR, ZLINK_MESH_READY_APPLICATION,
                              &claim);
            void *batch = zlink_mesh_receive_batch_new (
              128, 256, 1024 * 1024);
            zlink_mesh_receive_requirements_t requirements;
            memset (&requirements, 0, sizeof (requirements));
            if (!batch
                || zlink_mesh_claim_recv_batch (&claim, batch, &requirements,
                                                ZLINK_RECV_FLAGS_NONE)
                     != ZLINK_RECV_OK) { rc = 24; break; }
            const size_t expected_record_count =
              static_cast<size_t> (expected_actor_messages)
              + static_cast<size_t> (expected_session_messages);
            if (zlink_mesh_receive_batch_count (batch) != expected_record_count) {
                rc = 25;
                break;
            }
            const zlink_mesh_receive_record_t *records = zlink_mesh_receive_batch_data (batch);
            const zlink_msg_t *parts = zlink_mesh_receive_batch_parts (batch);
            size_t actor_fence_count = 0;
            size_t session_fence_count = 0;
            for (size_t i = 0; i < expected_record_count; ++i) {
                if (records[i].kind != ZLINK_MESH_RECORD_ACTOR_SEND
                    || records[i].part_count != 1) {
                    rc = 26;
                    break;
                }
                const zlink_msg_t *part = &parts[records[i].part_offset];
                const size_t part_size = zlink_msg_size (part);
                const void *part_data = zlink_msg_data (
                  const_cast<zlink_msg_t *> (part));
                if (part_size == strlen ("post-fence")
                    && memcmp (part_data, "post-fence", part_size) == 0) {
                    ++actor_fence_count;
                    continue;
                }
                if (part_size == strlen ("session-fence")
                    && memcmp (part_data, "session-fence", part_size) == 0) {
                    ++session_fence_count;
                    if (records[i].source_node_rid.size != strlen ("node-a")
                        || memcmp (records[i].source_node_rid.data, "node-a",
                                   strlen ("node-a"))
                             != 0
                        || records[i].source_spot_rid.size == 0) {
                        rc = 27;
                        break;
                    }
                    continue;
                }
                rc = 26;
                break;
            }
            if (rc != 0)
                break;
            if (actor_fence_count != expected_actor_messages
                || session_fence_count != expected_session_messages) {
                rc = 27;
                break;
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

            //  The target can address the transferred binding immediately
            //  after activation. This reverse frame reaches the source while
            //  its STREAM binding is still fenced and must be retained until
            //  the source commit releases that fence.
            zlink_msg_t precommit_bound_push;
            make_payload (&precommit_bound_push, "target-bound-precommit");
            if (zlink_mesh_node_actor_send_bound_session (
                  node, &current_actor, binding_generation,
                  &precommit_bound_push, 1,
                  ZLINK_SEND_FLAGS_NONE)
                != ZLINK_SUBMIT_OK) {
                zlink_msg_close (&precommit_bound_push);
                rc = 44;
                break;
            }
            zlink_msg_close (&precommit_bound_push);
            const char *const queued_pushes[] = {
              "target-bound-precommit-2", "target-bound-precommit-3"};
            for (size_t i = 0; i < 2; ++i) {
                zlink_msg_t queued_push;
                make_payload (&queued_push, queued_pushes[i]);
                if (zlink_mesh_node_actor_send_bound_session (
                      node, &current_actor, binding_generation,
                      &queued_push, 1,
                      ZLINK_SEND_FLAGS_NONE)
                    != ZLINK_SUBMIT_OK) {
                    zlink_msg_close (&queued_push);
                    rc = 45 + static_cast<int> (i);
                    break;
                }
                zlink_msg_close (&queued_push);
            }
            if (rc != 0)
                break;

            //  Signal activation so the source can commit and release its
            //  STREAM transfer fence.
            if (write (back_pipe[1], "A\n", 2) != 2) { rc = 29; break; }
            char source_commit_line[8];
            read_endpoint (authority_pipe[0], source_commit_line,
                           sizeof (source_commit_line));
            if (strcmp (source_commit_line, "S") != 0) { rc = 40; break; }

            //  A stale source ActorRef inside the forwarding window is
            //  relayed by Core to the committed target exactly once.
            take_ready_claim (node, ZLINK_MESH_OWNER_ACTOR, ZLINK_MESH_READY_APPLICATION,
                              &claim);
            batch = zlink_mesh_receive_batch_new (2, 8, 4096);
            memset (&requirements, 0, sizeof (requirements));
            if (!batch
                || zlink_mesh_claim_recv_batch (&claim, batch, &requirements,
                                             ZLINK_RECV_FLAGS_NONE)
                  != ZLINK_RECV_OK
                || zlink_mesh_receive_batch_count (batch) != 1) {
                rc = 42;
                break;
            }
            records = zlink_mesh_receive_batch_data (batch);
            parts = zlink_mesh_receive_batch_parts (batch);
            if (records[0].kind != ZLINK_MESH_RECORD_ACTOR_SEND
                || zlink_msg_size (&parts[records[0].part_offset])
                     != strlen ("source-straggler")
                || memcmp (zlink_msg_data (const_cast<zlink_msg_t *> (
                             &parts[records[0].part_offset])),
                           "source-straggler", strlen ("source-straggler"))
                     != 0) {
                rc = 43;
                break;
            }
            zlink_mesh_claim_release (&claim);
            zlink_mesh_receive_batch_destroy (&batch);

            //  The target actor keeps the committed STREAM binding route
            //  back to the source node and can push to the original client.
            //  Use the post-materialization ActorRef shape exposed to a
            //  framework: its node RID is the target, while the transfer
            //  prepare ActorRef still names the source.
            zlink_msg_t bound_push;
            make_payload (&bound_push, "target-bound");
            if (zlink_mesh_node_actor_send_bound_session (
                  node, &current_actor, binding_generation, &bound_push, 1,
                  ZLINK_SEND_FLAGS_NONE)
                != ZLINK_SUBMIT_OK) {
                zlink_msg_close (&bound_push);
                rc = 38;
                break;
            }
            zlink_msg_close (&bound_push);
            if (write (back_pipe[1], "B\n", 2) != 2) { rc = 39; break; }

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
    void *node = new_started_node (ctx, mesh_name, "node-a", 2);
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
    if (bind_before_prepare_) {
        TEST_ASSERT_EQUAL_INT (
          ZLINK_SUBMIT_OK,
          zlink_stream_session_bind_actor (
            session_service, &session_rid, &actor, &bind_operation, 1000));
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
    TEST_ASSERT_EQUAL_UINT (0, result.final_sequence);
    TEST_ASSERT_EQUAL_UINT (0, result.reserve_message_count);
    TEST_ASSERT_EQUAL_UINT64 (0, result.reserve_byte_count);

    //  The fence accepts bounded post-barrier traffic into the private
    //  transfer participant. It surfaces once on the target after the frozen
    //  mailbox range.
    {
        zlink_msg_t part;
        make_payload (&part, "post-fence");
        const zlink_submit_result_t rc = zlink_mesh_node_send_to_actor (
          node, &actor, NULL, &part, 1, ZLINK_SEND_FLAGS_DONTWAIT);
        zlink_msg_close (&part);
        TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_OK, rc);
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

    char ack[8];
    read_endpoint (back_pipe[0], ack, sizeof (ack));
    TEST_ASSERT_EQUAL_STRING ("R", ack);
    size_t accepted_actor_messages = 1;
    size_t accepted_session_messages = 0;
    if (!bind_before_prepare_) {
        bool actor_ingress_backpressured = false;
        for (size_t i = 1; i <= 64; ++i) {
            zlink_msg_t part;
            make_payload (&part, "post-fence");
            const zlink_submit_result_t rc = zlink_mesh_node_send_to_actor (
              node, &actor, NULL, &part, 1, ZLINK_SEND_FLAGS_DONTWAIT);
            const int submit_errno = zlink_errno ();
            zlink_msg_close (&part);
            if (rc == ZLINK_SUBMIT_OK) {
                ++accepted_actor_messages;
                continue;
            }
            TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_BACKPRESSURED, rc);
            TEST_ASSERT_EQUAL_INT (EAGAIN, submit_errno);
            actor_ingress_backpressured = true;
            break;
        }
        TEST_ASSERT_TRUE_MESSAGE (
          actor_ingress_backpressured,
          "actor transfer participant did not reach its bounded allowance");
        TEST_ASSERT_EQUAL_INT (
          ZLINK_SUBMIT_OK,
          zlink_stream_session_bind_actor (
            session_service, &session_rid, &actor, &bind_operation, 1000));
        zlink_test_stream_session_fence_actor (session_service, &actor, 1);
    }
    zlink_stream_session_binding_t active_binding;
    memset (&active_binding, 0, sizeof (active_binding));
    active_binding.struct_size = sizeof (active_binding);
    active_binding.version = ZLINK_STREAM_SESSION_ABI_VERSION;
    size_t active_binding_count = 1;
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK,
      zlink_stream_session_bindings (session_service, &session_rid,
                                     &active_binding,
                                     &active_binding_count));
    TEST_ASSERT_EQUAL_UINT64 (1, active_binding_count);
    TEST_ASSERT_NOT_EQUAL (0, active_binding.binding_generation);
    zlink_stream_session_status_t session_status;
    memset (&session_status, 0, sizeof (session_status));
    session_status.struct_size = sizeof (session_status);
    session_status.version = ZLINK_STREAM_SESSION_ABI_VERSION;
    const zlink_config_result_t status_result =
      zlink_stream_session_service_status (session_service, &session_status);
    if (bind_before_prepare_) {
        bool session_relay_backpressured = false;
        for (size_t i = 0; i <= 64; ++i) {
            zlink_msg_t part;
            make_payload (&part, "session-fence");
            const zlink_submit_result_t rc = zlink_stream_session_send_to_actor (
              session_service, &session_rid, &actor, NULL, &part, 1,
              ZLINK_SEND_FLAGS_DONTWAIT);
            const int submit_errno = zlink_errno ();
            zlink_msg_close (&part);
            if (rc == ZLINK_SUBMIT_OK) {
                ++accepted_session_messages;
                continue;
            }
            TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_BACKPRESSURED, rc);
            TEST_ASSERT_EQUAL_INT (EAGAIN, submit_errno);
            session_relay_backpressured = true;
            break;
        }
        TEST_ASSERT_TRUE_MESSAGE (
          session_relay_backpressured,
          "bound-session relay did not reach its transfer allowance");
    }
    TEST_ASSERT_EQUAL_UINT (
      0, drain_node_actor_send_ready (node, actor, 100));
    char commit_line[64];
    snprintf (commit_line, sizeof (commit_line), "C %llu %llu %llu\n",
              (unsigned long long) active_binding.binding_generation,
              (unsigned long long) accepted_actor_messages,
              (unsigned long long) accepted_session_messages);
    TEST_ASSERT_EQUAL_INT (
      (int) strlen (commit_line),
      (int) write (authority_pipe[1], commit_line, strlen (commit_line)));

    //  Wait for target activation, source-commit, then release the target's
    //  reverse bound-session push.
    read_endpoint (back_pipe[0], ack, sizeof (ack));
    TEST_ASSERT_EQUAL_STRING ("A", ack);
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK, status_result);
    TEST_ASSERT_EQUAL_UINT64 (0, session_status.pending_message_count);
    TEST_ASSERT_EQUAL_UINT64 (0, session_status.pending_byte_count);
    bool reverse_queue_full = false;
    for (int waited = 0; waited < poll_deadline_ms && !reverse_queue_full;
         waited += poll_step_ms) {
        memset (&session_status, 0, sizeof (session_status));
        session_status.struct_size = sizeof (session_status);
        session_status.version = ZLINK_STREAM_SESSION_ABI_VERSION;
        TEST_ASSERT_EQUAL_INT (
          ZLINK_CONFIG_OK,
          zlink_stream_session_service_status (
            session_service, &session_status));
        reverse_queue_full = session_status.pending_message_count
                             == (bind_before_prepare_
                                   ? accepted_session_messages
                                   : 2);
        if (!reverse_queue_full)
            msleep (poll_step_ms);
    }
    TEST_ASSERT_TRUE_MESSAGE (
      reverse_queue_full,
      "accepted precommit wire messages were not retained at source capacity");
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK, zlink_mesh_node_actor_transfer_commit (&token, 2));
    TEST_ASSERT_EQUAL_UINT (
      1, drain_node_actor_send_ready (node, actor, 2000));
    TEST_ASSERT_EQUAL_UINT (
      0, drain_node_actor_send_ready (node, actor, 0));
    {
        char pushed[32] = {0};
        TEST_ASSERT_EQUAL_INT (
          (int) strlen ("target-bound-precommit"),
          (int) recv (stream_client_fd, pushed,
                      strlen ("target-bound-precommit"), 0));
        TEST_ASSERT_EQUAL_MEMORY (
          "target-bound-precommit", pushed,
          strlen ("target-bound-precommit"));
    }
    {
        char pushed[32] = {0};
        TEST_ASSERT_EQUAL_INT (
          (int) strlen ("target-bound-precommit-2"),
          (int) recv (stream_client_fd, pushed,
                      strlen ("target-bound-precommit-2"), 0));
        TEST_ASSERT_EQUAL_MEMORY (
          "target-bound-precommit-2", pushed,
          strlen ("target-bound-precommit-2"));
    }
    {
        char pushed[32] = {0};
        TEST_ASSERT_EQUAL_INT (
          (int) strlen ("target-bound-precommit-3"),
          (int) recv (stream_client_fd, pushed,
                      strlen ("target-bound-precommit-3"), 0));
        TEST_ASSERT_EQUAL_MEMORY (
          "target-bound-precommit-3", pushed,
          strlen ("target-bound-precommit-3"));
    }
    {
        zlink_msg_t part;
        make_payload (&part, "source-straggler");
        TEST_ASSERT_EQUAL_INT (
          ZLINK_SUBMIT_OK,
          zlink_mesh_node_send_to_actor (
            node, &actor, NULL, &part, 1, ZLINK_SEND_FLAGS_DONTWAIT));
        zlink_msg_close (&part);
    }
    TEST_ASSERT_EQUAL_INT (
      2, (int) write (authority_pipe[1], "S\n", 2));
    read_endpoint (back_pipe[0], ack, sizeof (ack));
    TEST_ASSERT_EQUAL_STRING ("B", ack);
    {
        char pushed[32] = {0};
        TEST_ASSERT_EQUAL_INT (
          (int) strlen ("target-bound"),
          (int) recv (stream_client_fd, pushed, strlen ("target-bound"), 0));
        TEST_ASSERT_EQUAL_MEMORY ("target-bound", pushed, strlen ("target-bound"));
    }
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

void test_remote_actor_transfer_fence ()
{
    run_remote_actor_transfer_fence (false);
}

void test_remote_actor_transfer_prebound_session_fence ()
{
    run_remote_actor_transfer_fence (true);
}

void test_zero_membership_caller_uses_remote_channel_and_drains_request ()
{
    run_two_process_case (
      [] (int endpoint_fd, pid_t) {
          void *ctx = zlink_ctx_new ();
          TEST_ASSERT_NOT_NULL (ctx);
          void *node = new_started_node (
            ctx, "zero-channel-caller-mesh", "channel-server");
          publish_endpoint (node, endpoint_fd);

          zlink_mesh_peer_entry_t peer;
          TEST_ASSERT_TRUE_MESSAGE (
            wait_peer_state (node, ZLINK_MESH_PEER_ADMITTED, &peer),
            "channel server did not admit zero-membership caller");
          TEST_ASSERT_EQUAL_UINT32 (0, peer.channel_count);
          size_t channel_count = 1;
          TEST_ASSERT_EQUAL_INT (
            ZLINK_CONFIG_OK,
            zlink_mesh_node_peer_channels (
              node, &peer.routing_id, peer.lifecycle_generation, NULL, NULL,
              &channel_count));
          TEST_ASSERT_EQUAL_UINT64 (0, channel_count);

          char phase = 0;
          TEST_ASSERT_EQUAL_INT (1, (int) read (endpoint_fd, &phase, 1));
          TEST_ASSERT_EQUAL_INT ('S', phase);
          TEST_ASSERT_EQUAL_INT (
            0, receive_node_application_record (
                 node, ZLINK_MESH_RECORD_CHANNEL_SEND, "channel-send", NULL));
          TEST_ASSERT_EQUAL_INT (1, (int) write (endpoint_fd, "A", 1));

          TEST_ASSERT_EQUAL_INT (
            0, receive_node_application_record (
                 node, ZLINK_MESH_RECORD_CHANNEL_REQUEST, "channel-request",
                 "channel-reply"));
          TEST_ASSERT_EQUAL_INT (1, (int) read (endpoint_fd, &phase, 1));
          TEST_ASSERT_EQUAL_INT ('C', phase);

          //  The admitted caller advertises no membership, so it cannot be a
          //  Channel or Logical Multicast target for an unrelated name.
          zlink_msg_t payload;
          make_payload (&payload, "not-a-target");
          TEST_ASSERT_EQUAL_INT (
            ZLINK_SUBMIT_NOT_FOUND,
            zlink_mesh_node_send_to_channel (
              node, "client-only", NULL, &payload, 1,
              ZLINK_SEND_FLAGS_NONE));
          TEST_ASSERT_EQUAL_INT (ENOENT, zlink_errno ());
          void *publisher = zlink_mesh_node_publisher_new (node);
          TEST_ASSERT_NOT_NULL (publisher);
          zlink_mesh_publish_detail_t detail;
          memset (&detail, 0, sizeof (detail));
          detail.struct_size = sizeof (detail);
          detail.version = 1;
          TEST_ASSERT_EQUAL_INT (
            ZLINK_SUBMIT_NOT_FOUND,
            zlink_mesh_node_publisher_publish (
              publisher, "client-only", "topic", NULL, &payload, 1,
              &detail, ZLINK_SEND_FLAGS_NONE));
          TEST_ASSERT_EQUAL_UINT32 (0, detail.snapshot_remote_target_count);
          zlink_msg_close (&payload);
          TEST_ASSERT_EQUAL_INT (
            ZLINK_CLOSE_OK, zlink_mesh_node_publisher_destroy (&publisher));

          TEST_ASSERT_EQUAL_INT (1, (int) read (endpoint_fd, &phase, 1));
          TEST_ASSERT_EQUAL_INT ('D', phase);
          TEST_ASSERT_EQUAL_INT (1, (int) read (endpoint_fd, &phase, 1));
          TEST_ASSERT_EQUAL_INT ('G', phase);
          TEST_ASSERT_EQUAL_INT (
            0, receive_node_application_record (
                 node, ZLINK_MESH_RECORD_NODE_REQUEST, "drain-request",
                 "drain-reply"));
          TEST_ASSERT_EQUAL_INT (1, (int) read (endpoint_fd, &phase, 1));
          TEST_ASSERT_EQUAL_INT ('X', phase);

          TEST_ASSERT_EQUAL_INT (
            ZLINK_REQUEST_OK, zlink_mesh_node_shutdown (node, 1000));
          TEST_ASSERT_EQUAL_INT (
            ZLINK_CLOSE_OK, zlink_mesh_node_destroy (&node));
          TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_ctx_term (ctx));
      },
      [] (int endpoint_fd) -> int {
          char endpoint[ZLINK_MESH_ENDPOINT_MAX + 1];
          read_endpoint (endpoint_fd, endpoint, sizeof (endpoint));
          void *ctx = zlink_ctx_new ();
          if (!ctx)
              return 10;
          void *node = new_started_zero_membership_node (
            ctx, "zero-channel-caller-mesh", "channel-caller");
          if (submit_peer_intent (node, endpoint) == 0)
              return 11;
          zlink_mesh_peer_entry_t peer;
          if (!wait_peer_state (node, ZLINK_MESH_PEER_ADMITTED, &peer)
              || !wait_peer_channel_weight (node, "channel-server", 100,
                                            poll_deadline_ms))
              return 12;

          zlink_msg_t payload;
          make_payload (&payload, "channel-send");
          if (zlink_mesh_node_send_to_channel (
                node, channel_name, NULL, &payload, 1,
                ZLINK_SEND_FLAGS_NONE)
              != ZLINK_SUBMIT_OK)
              return 13;
          zlink_msg_close (&payload);
          if (write (endpoint_fd, "S", 1) != 1)
              return 14;
          char phase = 0;
          if (read (endpoint_fd, &phase, 1) != 1 || phase != 'A')
              return 15;

          make_payload (&payload, "channel-request");
          zlink_mesh_operation_id_t channel_operation;
          memset (&channel_operation, 0, sizeof (channel_operation));
          if (zlink_mesh_node_request_to_channel (
                node, channel_name, NULL, &payload, 1, &channel_operation,
                ZLINK_SEND_FLAGS_NONE, 2000)
              != ZLINK_SUBMIT_OK)
              return 16;
          zlink_msg_close (&payload);
          if (wait_node_completion (
                node, channel_operation, "channel-reply", NULL, 0)
              != 0)
              return 17;
          if (write (endpoint_fd, "C", 1) != 1)
              return 18;

          void *publisher = zlink_mesh_node_publisher_new (node);
          if (!publisher)
              return 19;
          make_payload (&payload, "drain-request");
          zlink_mesh_operation_id_t drain_operation;
          memset (&drain_operation, 0, sizeof (drain_operation));
          if (zlink_mesh_node_request_to_node (
                node, &peer.routing_id, NULL, &payload, 1, &drain_operation,
                ZLINK_SEND_FLAGS_NONE, 2000)
              != ZLINK_SUBMIT_OK)
              return 20;
          zlink_msg_close (&payload);
          if (write (endpoint_fd, "D", 1) != 1)
              return 21;

          std::atomic<int> completion_result (-1);
          std::atomic<int> shutdown_result (-1);
          std::thread completion_thread ([&] {
              completion_result.store (
                wait_node_completion (
                  node, drain_operation, "drain-reply", NULL, 0),
                std::memory_order_release);
          });
          std::thread shutdown_thread ([&] {
              shutdown_result.store (
                zlink_mesh_node_shutdown (node, 1000),
                std::memory_order_release);
          });

          int concurrent_result = 0;
          if (!wait_node_state (node, ZLINK_MESH_NODE_DRAINING))
              concurrent_result = 22;
          make_payload (&payload, "rejected-while-draining");
          if (concurrent_result == 0
              && (zlink_mesh_node_send_to_node (
                node, &peer.routing_id, NULL, &payload, 1,
                ZLINK_SEND_FLAGS_NONE)
                != ZLINK_SUBMIT_INVALID_STATE
                  || zlink_errno () != ESHUTDOWN))
              concurrent_result = 23;
          if (concurrent_result == 0
              && (zlink_mesh_node_send_to_channel (
                node, channel_name, NULL, &payload, 1,
                ZLINK_SEND_FLAGS_NONE)
                != ZLINK_SUBMIT_INVALID_STATE
                  || zlink_errno () != ESHUTDOWN))
              concurrent_result = 24;
          zlink_mesh_publish_detail_t detail;
          memset (&detail, 0, sizeof (detail));
          detail.struct_size = sizeof (detail);
          detail.version = 1;
          if (concurrent_result == 0
              && (zlink_mesh_node_publisher_publish (
                publisher, channel_name, "topic", NULL, &payload, 1, &detail,
                ZLINK_SEND_FLAGS_NONE)
                != ZLINK_SUBMIT_INVALID_STATE
                  || zlink_errno () != ESHUTDOWN))
              concurrent_result = 25;
          zlink_msg_close (&payload);
          if (write (endpoint_fd, "G", 1) != 1 && concurrent_result == 0)
              concurrent_result = 26;

          completion_thread.join ();
          shutdown_thread.join ();
          if (concurrent_result == 0
              && (completion_result.load (std::memory_order_acquire) != 0
                  || shutdown_result.load (std::memory_order_acquire)
                       != ZLINK_REQUEST_OK))
              concurrent_result = 27;
          if (write (endpoint_fd, "X", 1) != 1 && concurrent_result == 0)
              concurrent_result = 28;

          const int publisher_rc =
            zlink_mesh_node_publisher_destroy (&publisher);
          const int destroy_rc = zlink_mesh_node_destroy (&node);
          const int term_rc = zlink_ctx_term (ctx);
          return concurrent_result != 0 ? concurrent_result
                 : publisher_rc == ZLINK_CLOSE_OK
                     && destroy_rc == ZLINK_CLOSE_OK
                     && term_rc == ZLINK_CLOSE_OK
                   ? 0
                   : 29;
      });
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
        if (zlink_mesh_node_set_channel_weight (
              node, channel_name, child_index == 0 ? 75 : 25)
            != ZLINK_CONFIG_OK)
            std::_Exit (11);
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
            const size_t expected =
              phase == 0 ? (child_index == 0 ? 30 : 10) : (child_index == 0 ? 0 : 20);
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

    zlink_mesh_monitor_open_options_t monitor_options;
    memset (&monitor_options, 0, sizeof (monitor_options));
    monitor_options.struct_size = sizeof (monitor_options);
    monitor_options.version = 1;
    monitor_options.events =
      1ull << (ZLINK_MESH_MONITOR_CHANNEL_CHANGED - 1);
    void *monitor = zlink_mesh_node_monitor_open (source, &monitor_options);
    TEST_ASSERT_NOT_NULL (monitor);

    for (int i = 0; i < 40; ++i) {
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

    bool remote_channel_event_observed = false;
    for (int waited = 0; waited < poll_deadline_ms && !remote_channel_event_observed;
         waited += poll_step_ms) {
        zlink_mesh_monitor_event_t event;
        memset (&event, 0, sizeof (event));
        event.struct_size = sizeof (event);
        event.version = 1;
        const zlink_recv_result_t recv_result =
          zlink_mesh_node_monitor_recv (
            monitor, &event, ZLINK_RECV_FLAGS_DONTWAIT);
        if (recv_result == ZLINK_RECV_NO_DATA) {
            msleep (poll_step_ms);
            continue;
        }
        TEST_ASSERT_EQUAL_INT (ZLINK_RECV_OK, recv_result);
        remote_channel_event_observed =
          event.kind == ZLINK_MESH_MONITOR_CHANNEL_CHANGED
          && event.peer_rid.size == strlen ("rr-target-a")
          && memcmp (event.peer_rid.data, "rr-target-a", event.peer_rid.size) == 0
          && strcmp (event.channel_name, channel_name) == 0
          && event.peer_lifecycle_generation != 0
          && event.peer_descriptor_revision != 0;
    }
    TEST_ASSERT_TRUE_MESSAGE (
      remote_channel_event_observed,
      "remote channel descriptor update must emit CHANNEL_CHANGED");

    for (int i = 0; i < 20; ++i) {
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

    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_node_monitor_close (&monitor));
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

    //  Reproduce a parent whose process-local allocator is ahead of wall time.
    //  Forked children must not inherit that floor or both lifetimes can choose
    //  the same generation before the wall clock catches up.
    timeval now;
    TEST_ASSERT_EQUAL_INT (0, gettimeofday (&now, NULL));
    const uint64_t future_floor =
      static_cast<uint64_t> (now.tv_sec) * 1000000
      + static_cast<uint64_t> (now.tv_usec) + 10000000;
    zlink_test_set_lifecycle_generation_floor (future_floor);

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
        zlink_mesh_node_status_t child_node_status;
        memset (&child_node_status, 0, sizeof (child_node_status));
        child_node_status.struct_size = sizeof (child_node_status);
        child_node_status.version = 1;
        if (!child_node
            || zlink_mesh_node_status (child_node, &child_node_status)
                 != ZLINK_CONFIG_OK
            || write (ep1[0], &child_node_status.lifecycle_generation,
                      sizeof (child_node_status.lifecycle_generation))
                 != static_cast<ssize_t> (
                   sizeof (child_node_status.lifecycle_generation)))
            rc = 9;
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
        zlink_mesh_node_status_t child_node_status;
        memset (&child_node_status, 0, sizeof (child_node_status));
        child_node_status.struct_size = sizeof (child_node_status);
        child_node_status.version = 1;
        if (!child_node
            || zlink_mesh_node_status (child_node, &child_node_status)
                 != ZLINK_CONFIG_OK
            || write (ep2[0], &child_node_status.lifecycle_generation,
                      sizeof (child_node_status.lifecycle_generation))
                 != static_cast<ssize_t> (
                   sizeof (child_node_status.lifecycle_generation)))
            rc = 19;
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
    zlink_mesh_monitor_open_options_t monitor_options;
    memset (&monitor_options, 0, sizeof (monitor_options));
    monitor_options.struct_size = sizeof (monitor_options);
    monitor_options.version = 1;
    void *monitor = zlink_mesh_node_monitor_open (node, &monitor_options);
    TEST_ASSERT_NOT_NULL (monitor);
    publish_endpoint (node, ep1[1]);
    uint64_t round1_child_generation = 0;
    TEST_ASSERT_EQUAL_INT (
      static_cast<int> (sizeof (round1_child_generation)),
      static_cast<int> (
        read (ep1[1], &round1_child_generation,
              sizeof (round1_child_generation))));

    TEST_ASSERT_TRUE_MESSAGE (wait_admitted_count (node, 1), "round-1 peer not admitted");
    zlink_mesh_peer_entry_t round1_peer;
    TEST_ASSERT_TRUE_MESSAGE (
      wait_peer_rid_state (
        node, "node-b", ZLINK_MESH_PEER_ADMITTED, &round1_peer),
      "round-1 peer snapshot not admitted");
    zlink_mesh_monitor_event_t peer_event;
    TEST_ASSERT_TRUE_MESSAGE (
      wait_peer_monitor_event (
        monitor, ZLINK_MESH_MONITOR_PEER_ADMITTED, "node-b", &peer_event),
      "round-1 admitted event not observed");
    TEST_ASSERT_EQUAL_UINT64 (
      round1_peer.lifecycle_generation,
      peer_event.peer_lifecycle_generation);
    TEST_ASSERT_EQUAL_UINT64 (
      round1_child_generation, round1_peer.lifecycle_generation);
    TEST_ASSERT_EQUAL_UINT64 (
      round1_peer.descriptor_revision,
      peer_event.peer_descriptor_revision);
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
    TEST_ASSERT_TRUE_MESSAGE (
      wait_peer_monitor_event (
        monitor, ZLINK_MESH_MONITOR_PEER_CLOSED, "node-b", &peer_event),
      "round-1 closed event not observed");
    TEST_ASSERT_EQUAL_UINT64 (
      round1_peer.lifecycle_generation,
      peer_event.peer_lifecycle_generation);
    TEST_ASSERT_EQUAL_UINT64 (
      round1_peer.descriptor_revision,
      peer_event.peer_descriptor_revision);
    node_status (node, &status);
    TEST_ASSERT_EQUAL_INT (ZLINK_MESH_NODE_READY, status.state);

    //  round 2: the restarted peer with the same RID is admitted again.
    publish_endpoint (node, ep2[1]);
    uint64_t round2_child_generation = 0;
    TEST_ASSERT_EQUAL_INT (
      static_cast<int> (sizeof (round2_child_generation)),
      static_cast<int> (
        read (ep2[1], &round2_child_generation,
              sizeof (round2_child_generation))));
    TEST_ASSERT_TRUE_MESSAGE (wait_admitted_count (node, 1), "round-2 peer not admitted");
    zlink_mesh_peer_entry_t round2_peer;
    TEST_ASSERT_TRUE_MESSAGE (
      wait_peer_rid_state (
        node, "node-b", ZLINK_MESH_PEER_ADMITTED, &round2_peer),
      "round-2 peer snapshot not admitted");
    TEST_ASSERT_TRUE_MESSAGE (
      wait_peer_monitor_event (
        monitor, ZLINK_MESH_MONITOR_PEER_ADMITTED, "node-b", &peer_event),
      "round-2 admitted event not observed");
    TEST_ASSERT_EQUAL_UINT64 (
      round2_peer.lifecycle_generation,
      peer_event.peer_lifecycle_generation);
    TEST_ASSERT_EQUAL_UINT64 (
      round2_child_generation, round2_peer.lifecycle_generation);
    TEST_ASSERT_EQUAL_UINT64 (
      round2_peer.descriptor_revision,
      peer_event.peer_descriptor_revision);
    TEST_ASSERT_NOT_EQUAL (
      round1_peer.lifecycle_generation,
      round2_peer.lifecycle_generation);

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
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CLOSE_OK, zlink_mesh_node_monitor_close (&monitor));
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK, zlink_mesh_node_shutdown (node, 2000));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_node_destroy (&node));
    TEST_ASSERT_EQUAL_INT (0, zlink_ctx_term (ctx));
}

void test_mesh_max_message_size_applies_live_and_recovers ()
{
    run_two_process_case (
      [] (int endpoint_fd, pid_t) {
          void *ctx = zlink_ctx_new ();
          TEST_ASSERT_NOT_NULL (ctx);
          void *receiver =
            new_started_node (ctx, "mesh-max-message", "max-receiver");
          publish_endpoint (receiver, endpoint_fd);
          TEST_ASSERT_TRUE_MESSAGE (
            wait_admitted_count (receiver, 1), "receiver peer not admitted");

          const int64_t limited = 256;
          TEST_ASSERT_EQUAL_INT (
            ZLINK_CONFIG_OK,
            zlink_set_option (
              receiver, ZLINK_OPT_MAXMSGSIZE, &limited, sizeof (limited)));
          TEST_ASSERT_EQUAL_INT (1, (int) write (endpoint_fd, "L", 1));

          char phase = 0;
          TEST_ASSERT_EQUAL_INT (1, (int) read (endpoint_fd, &phase, 1));
          TEST_ASSERT_EQUAL_INT ('O', phase);
          TEST_ASSERT_EQUAL_INT (
            0, receive_exact_node_records (receiver, 0, 300));

          TEST_ASSERT_EQUAL_INT (1, (int) write (endpoint_fd, "S", 1));
          TEST_ASSERT_EQUAL_INT (1, (int) read (endpoint_fd, &phase, 1));
          TEST_ASSERT_EQUAL_INT ('D', phase);
          TEST_ASSERT_EQUAL_INT (
            0, receive_exact_node_records (receiver, 1, 2000));

          const int64_t unlimited = -1;
          TEST_ASSERT_EQUAL_INT (
            ZLINK_CONFIG_OK,
            zlink_set_option (
              receiver, ZLINK_OPT_MAXMSGSIZE, &unlimited, sizeof (unlimited)));
          TEST_ASSERT_EQUAL_INT (1, (int) write (endpoint_fd, "U", 1));
          TEST_ASSERT_EQUAL_INT (1, (int) read (endpoint_fd, &phase, 1));
          TEST_ASSERT_EQUAL_INT ('D', phase);
          TEST_ASSERT_EQUAL_INT (
            0, receive_exact_node_records (receiver, 1, 2000));

          TEST_ASSERT_EQUAL_INT (
            ZLINK_REQUEST_OK, zlink_mesh_node_shutdown (receiver, 1000));
          TEST_ASSERT_EQUAL_INT (
            ZLINK_CLOSE_OK, zlink_mesh_node_destroy (&receiver));
          TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_ctx_term (ctx));
      },
      [] (int endpoint_fd) -> int {
          char endpoint[ZLINK_MESH_ENDPOINT_MAX + 1];
          read_endpoint (endpoint_fd, endpoint, sizeof (endpoint));
          if (endpoint[0] == '\0')
              return 10;

          void *ctx = zlink_ctx_new ();
          if (!ctx)
              return 11;
          void *sender =
            new_started_node (ctx, "mesh-max-message", "max-sender");
          if (submit_peer_intent (sender, endpoint) == 0
              || !wait_admitted_count (sender, 1))
              return 12;

          zlink_routing_id_t receiver_rid;
          memset (&receiver_rid, 0, sizeof (receiver_rid));
          receiver_rid.size = strlen ("max-receiver");
          memcpy (receiver_rid.data, "max-receiver", receiver_rid.size);

          char phase = 0;
          if (read (endpoint_fd, &phase, 1) != 1 || phase != 'L')
              return 13;
          zlink_msg_t payload;
          if (zlink_msg_init_size (&payload, 1024) != ZLINK_CONFIG_OK)
              return 14;
          memset (zlink_msg_data (&payload), 0x5A, zlink_msg_size (&payload));
          if (zlink_mesh_node_send_to_node (
                sender, &receiver_rid, NULL, &payload, 1, ZLINK_SEND_FLAGS_NONE)
              != ZLINK_SUBMIT_OK)
              return 15;
          zlink_msg_close (&payload);
          if (write (endpoint_fd, "O", 1) != 1)
              return 16;

          if (read (endpoint_fd, &phase, 1) != 1 || phase != 'S')
              return 17;
          make_payload (&payload, "within-limit");
          if (zlink_mesh_node_send_to_node (
                sender, &receiver_rid, NULL, &payload, 1, ZLINK_SEND_FLAGS_NONE)
              != ZLINK_SUBMIT_OK)
              return 18;
          zlink_msg_close (&payload);
          if (write (endpoint_fd, "D", 1) != 1)
              return 19;

          if (read (endpoint_fd, &phase, 1) != 1 || phase != 'U')
              return 20;
          if (zlink_msg_init_size (&payload, 1024) != ZLINK_CONFIG_OK)
              return 21;
          memset (zlink_msg_data (&payload), 0xA5, zlink_msg_size (&payload));
          if (zlink_mesh_node_send_to_node (
                sender, &receiver_rid, NULL, &payload, 1, ZLINK_SEND_FLAGS_NONE)
              != ZLINK_SUBMIT_OK)
              return 22;
          zlink_msg_close (&payload);
          if (write (endpoint_fd, "D", 1) != 1)
              return 23;

          zlink_mesh_node_shutdown (sender, 1000);
          if (zlink_mesh_node_destroy (&sender) != ZLINK_CLOSE_OK)
              return 24;
          if (zlink_ctx_term (ctx) != ZLINK_CLOSE_OK)
              return 25;
          return 0;
      });
}

//  A bounded set of unique TCP peers completes two direct Spot request/reply
//  phases, queues one unread one-way message each, and then waits at a process
//  barrier. Releasing peer teardown while the hub shuts down verifies exact
//  ROUTER pipe and fair-queue bookkeeping under the perf runner's overlap.
void test_spot_peer_barrier_teardown_keeps_router_fq_empty ()
{
    const size_t peer_count = 100;
    const size_t requests_per_phase = 16;
    const size_t repeat_count = 4;

    for (size_t round = 0; round < repeat_count; ++round) {
        int controls[peer_count][2];
        for (size_t i = 0; i < peer_count; ++i)
            TEST_ASSERT_EQUAL_INT (
              0, socketpair (AF_UNIX, SOCK_STREAM, 0, controls[i]));

        fflush (NULL);
        pid_t children[peer_count];
        memset (children, 0, sizeof (children));
        for (size_t child_index = 0; child_index < peer_count; ++child_index) {
            children[child_index] = fork ();
            TEST_ASSERT_TRUE (children[child_index] >= 0);
            if (children[child_index] != 0)
                continue;

            for (size_t i = 0; i < peer_count; ++i) {
                close (controls[i][1]);
                if (i != child_index)
                    close (controls[i][0]);
            }
            setup_test_environment (60);

            char endpoint[ZLINK_MESH_ENDPOINT_MAX + 1];
            char generation_text[32];
            read_endpoint (controls[child_index][0], endpoint,
                           sizeof (endpoint));
            read_endpoint (controls[child_index][0], generation_text,
                           sizeof (generation_text));
            const uint64_t target_generation =
              strtoull (generation_text, NULL, 10);
            if (endpoint[0] == '\0' || target_generation == 0)
                std::_Exit (10);

            void *ctx = zlink_ctx_new ();
            if (!ctx)
                std::_Exit (11);
            char peer_rid[48];
            snprintf (peer_rid, sizeof (peer_rid), "teardown-peer-%zu",
                      child_index);
            void *node = new_started_node (
              ctx, "mesh-spot-teardown", peer_rid);
            if (submit_peer_intent (node, endpoint) == 0)
                std::_Exit (12);
            zlink_mesh_peer_entry_t hub_entry;
            if (!wait_peer_state (node, ZLINK_MESH_PEER_ADMITTED,
                                  &hub_entry))
                std::_Exit (13);

            void *spot = NULL;
            if (zlink_mesh_node_entry_spot (node, &spot) != ZLINK_CONFIG_OK)
                std::_Exit (14);
            for (size_t phase = 0; phase < 2; ++phase) {
                for (size_t request = 0; request < requests_per_phase;
                     ++request) {
                    zlink_msg_t payload;
                    if (zlink_msg_init_size (
                          &payload, strlen ("barrier-ping"))
                        != ZLINK_CONFIG_OK)
                        std::_Exit (15);
                    memcpy (zlink_msg_data (&payload), "barrier-ping",
                            strlen ("barrier-ping"));
                    zlink_mesh_operation_id_t operation_id;
                    memset (&operation_id, 0, sizeof (operation_id));
                    if (zlink_spot_request_to_spot (
                          spot, &hub_entry.routing_id, &hub_entry.routing_id,
                          target_generation, NULL, &payload, 1, &operation_id,
                          ZLINK_SEND_FLAGS_DONTWAIT, 2000)
                        != ZLINK_SUBMIT_OK) {
                        zlink_msg_close (&payload);
                        std::_Exit (16);
                    }
                    const int completion = wait_spot_completion (
                      node, spot, operation_id, "barrier-pong");
                    if (completion != 0)
                        std::_Exit (completion);
                }

                const char ready_marker = phase == 0 ? 'R' : 'C';
                if (write (controls[child_index][0], &ready_marker, 1) != 1)
                    std::_Exit (50);
                char release = 0;
                if (read (controls[child_index][0], &release, 1) != 1
                    || release != (phase == 0 ? 'N' : 'T'))
                    std::_Exit (51);
                if (phase == 1) {
                    zlink_msg_t tail;
                    if (zlink_msg_init_size (
                          &tail, strlen ("unread-tail"))
                        != ZLINK_CONFIG_OK)
                        std::_Exit (52);
                    memcpy (zlink_msg_data (&tail), "unread-tail",
                            strlen ("unread-tail"));
                    if (zlink_spot_send_to_spot (
                          spot, &hub_entry.routing_id,
                          &hub_entry.routing_id, target_generation, NULL,
                          &tail, 1, ZLINK_SEND_FLAGS_NONE)
                        != ZLINK_SUBMIT_OK) {
                        zlink_msg_close (&tail);
                        std::_Exit (53);
                    }
                    if (write (controls[child_index][0], "R", 1) != 1)
                        std::_Exit (54);
                    if (read (controls[child_index][0], &release, 1) != 1
                        || release != 'G')
                        std::_Exit (55);
                }
            }

            int result = 0;
            if (zlink_spot_destroy (&spot) != ZLINK_CLOSE_OK)
                result = 56;
            if (zlink_mesh_node_shutdown (node, 1000) != ZLINK_REQUEST_OK
                && result == 0)
                result = 57;
            if (zlink_mesh_node_destroy (&node) != ZLINK_CLOSE_OK
                && result == 0)
                result = 58;
            if (zlink_ctx_term (ctx) != ZLINK_CLOSE_OK && result == 0)
                result = 59;
            close (controls[child_index][0]);
            fflush (NULL);
            std::_Exit (result);
        }

        for (size_t i = 0; i < peer_count; ++i)
            close (controls[i][0]);

        void *ctx = zlink_ctx_new ();
        TEST_ASSERT_NOT_NULL (ctx);
        void *hub = new_started_node (
          ctx, "mesh-spot-teardown", "teardown-hub");
        void *hub_spot = NULL;
        TEST_ASSERT_EQUAL_INT (
          ZLINK_CONFIG_OK, zlink_mesh_node_entry_spot (hub, &hub_spot));
        zlink_spot_status_t hub_spot_status;
        memset (&hub_spot_status, 0, sizeof (hub_spot_status));
        hub_spot_status.struct_size = sizeof (hub_spot_status);
        hub_spot_status.version = 1;
        TEST_ASSERT_EQUAL_INT (
          ZLINK_CONFIG_OK, zlink_spot_status (hub_spot, &hub_spot_status));

        for (size_t i = 0; i < peer_count; ++i) {
            publish_endpoint (hub, controls[i][1]);
            char generation_line[32];
            snprintf (
              generation_line, sizeof (generation_line), "%llu\n",
              static_cast<unsigned long long> (
                hub_spot_status.lifecycle_generation));
            TEST_ASSERT_EQUAL_INT (
              static_cast<int> (strlen (generation_line)),
              static_cast<int> (write (controls[i][1], generation_line,
                                       strlen (generation_line))));
        }

        void *batch = zlink_mesh_receive_batch_new (
          peer_count, peer_count * 2, 256 * 1024);
        TEST_ASSERT_NOT_NULL (batch);
        for (size_t phase = 0; phase < 2; ++phase) {
            size_t replied = 0;
            const size_t expected_replies =
              peer_count * requests_per_phase;
            while (replied < expected_replies) {
                zlink_mesh_claim_t claim;
                take_ready_claim (
                  hub, ZLINK_MESH_OWNER_SPOT,
                  ZLINK_MESH_READY_APPLICATION, &claim);
                zlink_mesh_receive_requirements_t requirements;
                memset (&requirements, 0, sizeof (requirements));
                TEST_ASSERT_EQUAL_INT (
                  ZLINK_RECV_OK,
                  zlink_mesh_claim_recv_batch (
                    &claim, batch, &requirements, ZLINK_RECV_FLAGS_NONE));
                const size_t count = zlink_mesh_receive_batch_count (batch);
                const zlink_mesh_receive_record_t *records =
                  zlink_mesh_receive_batch_data (batch);
                for (size_t i = 0; i < count; ++i) {
                    TEST_ASSERT_EQUAL_INT (
                      ZLINK_MESH_RECORD_SPOT_REQUEST, records[i].kind);
                    zlink_msg_t reply;
                    make_payload (&reply, "barrier-pong");
                    TEST_ASSERT_EQUAL_INT (
                      ZLINK_SUBMIT_OK,
                      zlink_mesh_reply (&records[i].reply_token, &reply, 1,
                                        ZLINK_SEND_FLAGS_NONE));
                    ++replied;
                }
                TEST_ASSERT_EQUAL_INT (
                  ZLINK_CLOSE_OK, zlink_mesh_claim_release (&claim));
                TEST_ASSERT_EQUAL_INT (
                  ZLINK_CONFIG_OK, zlink_mesh_receive_batch_reset (batch));
            }
            TEST_ASSERT_EQUAL_UINT (expected_replies, replied);

            for (size_t i = 0; i < peer_count; ++i) {
                char ready = 0;
                TEST_ASSERT_EQUAL_INT (
                  1, static_cast<int> (read (controls[i][1], &ready, 1)));
                TEST_ASSERT_EQUAL_HEX8 (phase == 0 ? 'R' : 'C', ready);
            }
            if (phase == 0) {
                for (size_t i = 0; i < peer_count; ++i)
                    TEST_ASSERT_EQUAL_INT (
                      1, static_cast<int> (write (
                           controls[i][1], "N", 1)));
            } else {
                for (size_t i = 0; i < peer_count; ++i)
                    TEST_ASSERT_EQUAL_INT (
                      1, static_cast<int> (write (
                           controls[i][1], "T", 1)));
                for (size_t i = 0; i < peer_count; ++i) {
                    char tail_submitted = 0;
                    TEST_ASSERT_EQUAL_INT (
                      1, static_cast<int> (read (
                           controls[i][1], &tail_submitted, 1)));
                    TEST_ASSERT_EQUAL_HEX8 ('R', tail_submitted);
                }
            }
        }
        TEST_ASSERT_EQUAL_INT (
          ZLINK_CLOSE_OK, zlink_mesh_receive_batch_destroy (&batch));
        TEST_ASSERT_EQUAL_INT (
          ZLINK_CLOSE_OK, zlink_spot_destroy (&hub_spot));

        std::atomic<bool> release_shutdown (false);
        int hub_shutdown_result = -1;
        int hub_destroy_result = -1;
        std::thread hub_shutdown_thread ([&] () {
            while (!release_shutdown.load (std::memory_order_acquire))
                std::this_thread::yield ();
            hub_shutdown_result = zlink_mesh_node_shutdown (hub, 1000);
            hub_destroy_result = zlink_mesh_node_destroy (&hub);
        });
        release_shutdown.store (true, std::memory_order_release);
        for (size_t i = 0; i < peer_count; ++i)
            TEST_ASSERT_EQUAL_INT (
              1, static_cast<int> (write (controls[i][1], "G", 1)));
        hub_shutdown_thread.join ();
        TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK, hub_shutdown_result);
        TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, hub_destroy_result);
        TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_ctx_term (ctx));

        for (size_t i = 0; i < peer_count; ++i) {
            close (controls[i][1]);
            int status = 0;
            TEST_ASSERT_EQUAL_INT (
              children[i], waitpid (children[i], &status, 0));
            if (!WIFEXITED (status) || WEXITSTATUS (status) != 0) {
                char failure[128];
                snprintf (
                  failure, sizeof (failure),
                  "teardown peer process reported failure (index=%zu exit=%d signal=%d)",
                  i, WIFEXITED (status) ? WEXITSTATUS (status) : -1,
                  WIFSIGNALED (status) ? WTERMSIG (status) : 0);
                TEST_FAIL_MESSAGE (failure);
            }
        }
    }
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
    RUN_SELECTED (test_mesh_tls_bind_uses_configured_server_material);
    RUN_SELECTED (test_peer_admission_readiness_and_weight_update);
    RUN_SELECTED (test_stale_transport_disconnect_preserves_admitted_successor);
    RUN_SELECTED (test_outbound_disconnect_then_reconnect_same_endpoint);
    RUN_SELECTED (test_outbound_intent_auto_reconnects_restarted_peer);
    RUN_SELECTED (test_reciprocal_peer_replacement_moves_same_rid_to_new_endpoint);
    RUN_SELECTED (test_peer_mesh_name_mismatch_is_conflict);
    RUN_SELECTED (test_zero_membership_peers_exchange_node_messages_both_directions);
    RUN_SELECTED (test_remote_node_request_reply_round_trip);
    RUN_SELECTED (test_remote_spot_direct_request_reply);
    RUN_SELECTED (test_remote_multicast_publish);
    RUN_SELECTED (test_reciprocal_peer_spot_publish_exact_subscription);
    RUN_SELECTED (test_multicast_selects_inbound_eligible_peer_over_zero_weight_outbound_peers);
    RUN_SELECTED (test_shutdown_interrupts_infinite_blocking_mesh_send);
    RUN_SELECTED (test_blocking_publish_after_dontwait_backpressure_stays_connected);
    RUN_SELECTED (test_router_unreachable_target_accounting);
    RUN_SELECTED (test_remote_actor_lookup_messaging_destroy);
    RUN_SELECTED (test_remote_actor_join_entry_spot);
    RUN_SELECTED (test_remote_actor_transfer_fence);
    RUN_SELECTED (test_remote_actor_transfer_prebound_session_fence);
    RUN_SELECTED (test_zero_membership_caller_uses_remote_channel_and_drains_request);
    RUN_SELECTED (test_remote_channel_round_robin_and_zero_weight_exclusion);
    RUN_SELECTED (test_inbound_peer_merges_manual_intent_to_mixed);
    RUN_SELECTED (test_peer_drain_and_reconnect);
    RUN_SELECTED (test_mesh_max_message_size_applies_live_and_recovers);
    RUN_SELECTED (test_spot_peer_barrier_teardown_keeps_router_fq_empty);
#undef RUN_SELECTED
#endif
    return UNITY_END ();
}
