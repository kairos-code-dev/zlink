/* SPDX-License-Identifier: MPL-2.0 */

#include "../testutil.hpp"
#include "../testutil_unity.hpp"

#include <zlink/service/instance_spot_driver.h>
#include <zlink.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <atomic>
#include <thread>

#if !defined _WIN32
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

extern "C" void zlink_test_set_mesh_alloc_fault (int count_);
extern "C" void zlink_test_mesh_pause_remote_route_before_commit (
  int enabled_);
extern "C" int zlink_test_mesh_remote_route_before_commit_paused ();
extern "C" size_t zlink_test_mesh_remote_route_flights (void *node_);
extern "C" int
zlink_test_mesh_remote_route_disconnect_overflow_observed (void *node_);
extern "C" void zlink_test_set_mesh_force_reply_wire_alloc_fault (
  int enabled_);
extern "C" int zlink_test_mesh_force_reply_wire_alloc_fault_pending ();
extern "C" int zlink_test_mesh_force_reply_token (
  const zlink_mesh_reply_token_t *token_, int32_t terminal_result_,
  int32_t failure_errno_);
extern "C" int zlink_test_mesh_deferred_force_reply_token (
  const zlink_mesh_reply_token_t *token_, int32_t terminal_result_,
  int32_t failure_errno_);
extern "C" int zlink_test_mesh_reply_route_state (
  const zlink_mesh_reply_token_t *token_, int *in_flight_out_,
  int *force_pending_out_, int *consumed_out_);
extern "C" void zlink_test_mesh_pause_instance_wire_before_send (
  int enabled_);
extern "C" int zlink_test_mesh_instance_wire_before_send_paused ();
#endif

void setUp ()
{
}

void tearDown ()
{
}

#if !defined _WIN32
namespace
{
const char *const mesh_name = "instance-wire";
const char *const channel_name = "instance-contract";
const char *const spot_id = "wire-order-1";
const char *const instance_type = "wire-order-workflow";
const char *const contract_id = "WireOrderRequest";

struct node_t
{
    void *ctx;
    void *node;
    void *entry;
    zlink_mesh_node_status_t status;
};

struct bootstrap_t
{
    char endpoint[ZLINK_MESH_ENDPOINT_MAX + 1];
    zlink_routing_id_t rid;
    uint64_t generation;
};

struct topology_t
{
    bootstrap_t source;
    bootstrap_t loser;
    bootstrap_t winner;
};

struct owner_snapshot_t
{
    bootstrap_t node;
    uint64_t spot_generation;
};

struct loser_report_t
{
    int validation;
    int first_redirect;
    int second_redirect;
    int second_errno;
};

struct winner_report_t
{
    int validation;
    zlink_mesh_operation_id_t operation;
};

struct drain_report_t
{
    int validation;
    zlink_request_result_t shutdown_result;
    int shutdown_errno;
    uint64_t pending_before_shutdown;
    size_t ready_after_shutdown;
};

struct relay_disconnect_report_t
{
    zlink_connect_result_t disconnect_result;
    int disconnect_errno;
    int force_fault_consumed;
    int reconnect_ok;
};

struct stale_origin_source_report_t
{
    int validation;
    bootstrap_t source;
    zlink_mesh_operation_id_t operation;
};

struct stale_origin_completion_report_t
{
    int no_completion_before_current_reply;
    int current_completion;
    int duplicate_completion;
};

struct presend_source_report_t
{
    int validation;
    zlink_mesh_operation_id_t operation;
};

bool write_full (int fd_, const void *data_, size_t size_)
{
    const unsigned char *data = static_cast<const unsigned char *> (data_);
    size_t offset = 0;
    while (offset < size_) {
        const ssize_t result = write (fd_, data + offset, size_ - offset);
        if (result < 0 && errno == EINTR)
            continue;
        if (result <= 0)
            return false;
        offset += static_cast<size_t> (result);
    }
    return true;
}

bool read_full (int fd_, void *data_, size_t size_)
{
    unsigned char *data = static_cast<unsigned char *> (data_);
    size_t offset = 0;
    while (offset < size_) {
        const ssize_t result = read (fd_, data + offset, size_ - offset);
        if (result < 0 && errno == EINTR)
            continue;
        if (result <= 0)
            return false;
        offset += static_cast<size_t> (result);
    }
    return true;
}

void make_rid (zlink_routing_id_t *rid_, const char *value_)
{
    memset (rid_, 0, sizeof (*rid_));
    rid_->size = static_cast<uint8_t> (strlen (value_));
    memcpy (rid_->data, value_, rid_->size);
}

zlink_routing_id_t instance_spot_rid ()
{
    zlink_routing_id_t rid;
    make_rid (&rid, spot_id);
    return rid;
}

int start_node (const char *rid_, node_t *out_, uint64_t watchdog_ms_ = 0)
{
    memset (out_, 0, sizeof (*out_));
    out_->ctx = zlink_ctx_new ();
    if (!out_->ctx)
        return 1;
    zlink_mesh_node_options_t options;
    memset (&options, 0, sizeof (options));
    options.struct_size = sizeof (options);
    options.version = 1;
    options.mesh_name = mesh_name;
    options.mesh_name_size = strlen (mesh_name);
    out_->node = zlink_mesh_node_new (out_->ctx, &options);
    if (out_->node && watchdog_ms_ != 0
        && zlink_set_mesh_node_option (
             out_->node,
             ZLINK_MESH_NODE_OPT_INSTANCE_ACTIVATION_TIMEOUT_MS,
             &watchdog_ms_, sizeof (watchdog_ms_))
             != ZLINK_CONFIG_OK)
        return 2;
    if (!out_->node)
        return 2;
    if (zlink_set_routing_id (out_->node, rid_, strlen (rid_))
          != ZLINK_CONFIG_OK)
        return 20;
    if (zlink_mesh_node_set_bind (out_->node, "tcp://127.0.0.1:0")
          != ZLINK_CONFIG_OK)
        return 21;
    if (zlink_mesh_node_add_channel_name (out_->node, channel_name)
          != ZLINK_CONFIG_OK)
        return 22;
    if (zlink_mesh_node_start (out_->node) != ZLINK_CONFIG_OK)
        return 23;
    memset (&out_->status, 0, sizeof (out_->status));
    out_->status.struct_size = sizeof (out_->status);
    out_->status.version = 1;
    if (zlink_mesh_node_status (out_->node, &out_->status) != ZLINK_CONFIG_OK
        || zlink_mesh_node_entry_spot (out_->node, &out_->entry)
             != ZLINK_CONFIG_OK)
        return 3;
    return 0;
}

void stop_node (node_t *node_)
{
    if (node_->entry)
        zlink_spot_destroy (&node_->entry);
    if (node_->node) {
        zlink_mesh_node_shutdown (node_->node, 1000);
        zlink_mesh_node_destroy (&node_->node);
    }
    if (node_->ctx)
        zlink_ctx_term (node_->ctx);
}

bootstrap_t bootstrap (const node_t &node_)
{
    bootstrap_t result;
    memset (&result, 0, sizeof (result));
    memcpy (result.endpoint, node_.status.local_endpoint,
            sizeof (result.endpoint));
    result.rid = node_.status.routing_id;
    result.generation = node_.status.lifecycle_generation;
    return result;
}

bool connect_endpoint (void *node_, const char *endpoint_)
{
    zlink_mesh_peer_connection_options_t options;
    memset (&options, 0, sizeof (options));
    options.struct_size = sizeof (options);
    options.version = 1;
    options.endpoint = endpoint_;
    options.endpoint_size = strlen (endpoint_);
    uint64_t intent = 0;
    return zlink_mesh_node_connect_peer (node_, &options, &intent)
           == ZLINK_CONNECT_OK;
}

bool wait_admitted (void *node_, uint32_t count_)
{
    for (int attempt = 0; attempt < 500; ++attempt) {
        zlink_mesh_node_status_t status;
        memset (&status, 0, sizeof (status));
        status.struct_size = sizeof (status);
        status.version = 1;
        if (zlink_mesh_node_status (node_, &status) == ZLINK_CONFIG_OK
            && status.admitted_peer_count >= count_)
            return true;
        msleep (5);
    }
    return false;
}

bool wait_admitted_generation (void *node_,
                               const zlink_routing_id_t &rid_,
                               uint64_t generation_)
{
    for (int attempt = 0; attempt < 500; ++attempt) {
        zlink_mesh_peer_entry_t entries[8];
        memset (entries, 0, sizeof (entries));
        for (size_t i = 0; i < 8; ++i) {
            entries[i].struct_size = sizeof (entries[i]);
            entries[i].version = 1;
        }
        size_t count = 8;
        if (zlink_mesh_node_peers (node_, entries, &count)
            == ZLINK_CONFIG_OK) {
            for (size_t i = 0; i < count; ++i) {
                if (entries[i].state == ZLINK_MESH_PEER_ADMITTED
                    && entries[i].lifecycle_generation == generation_
                    && entries[i].routing_id.size == rid_.size
                    && memcmp (entries[i].routing_id.data, rid_.data,
                               rid_.size)
                         == 0)
                    return true;
            }
        }
        msleep (5);
    }
    return false;
}

bool wait_instance_wire_before_send_paused ()
{
    for (int attempt = 0; attempt < 1000; ++attempt) {
        if (zlink_test_mesh_instance_wire_before_send_paused () != 0)
            return true;
        msleep (1);
    }
    return false;
}

bool take_claim (void *node_,
                 zlink_mesh_owner_kind_t owner_,
                 zlink_mesh_ready_domain_mask_t domain_,
                 zlink_mesh_claim_t *claim_)
{
    void *ready = zlink_mesh_ready_batch_new (8);
    if (!ready)
        return false;
    bool found = false;
    for (int attempt = 0; attempt < 500 && !found; ++attempt) {
        uint32_t residue = 0;
        const zlink_recv_result_t result = zlink_mesh_node_drain_ready (
          node_, domain_, ready, &residue, ZLINK_RECV_FLAGS_DONTWAIT);
        if (result == ZLINK_RECV_OK) {
            const zlink_mesh_ready_record_t *records =
              zlink_mesh_ready_batch_data (ready);
            for (size_t i = 0; i < zlink_mesh_ready_batch_count (ready); ++i) {
                if (records[i].owner_kind == owner_
                    && records[i].domain == domain_
                    && zlink_mesh_ready_batch_take_claim (ready, i, claim_)
                         == ZLINK_CONFIG_OK) {
                    found = true;
                    break;
                }
            }
        }
        if (!found) {
            zlink_mesh_ready_batch_reset (ready);
            msleep (5);
        }
    }
    zlink_mesh_ready_batch_destroy (&ready);
    return found;
}

bool take_activation (void *node_, zlink_instance_spot_activation_data_t *out_)
{
    for (int attempt = 0; attempt < 32; ++attempt) {
        zlink_mesh_claim_t claim;
        if (!take_claim (node_, ZLINK_MESH_OWNER_NODE,
                         ZLINK_MESH_READY_INFRASTRUCTURE, &claim))
            return false;
        void *batch = zlink_mesh_receive_batch_new (1, 1, 4096);
        zlink_mesh_receive_requirements_t required;
        memset (&required, 0, sizeof (required));
        required.struct_size = sizeof (required);
        required.version = 1;
        const bool received =
          batch
          && zlink_mesh_claim_recv_batch (&claim, batch, &required,
                                           ZLINK_RECV_FLAGS_NONE)
               == ZLINK_RECV_OK
          && zlink_mesh_receive_batch_count (batch) == 1;
        bool matched = false;
        if (received) {
            const zlink_mesh_receive_record_t *record =
              zlink_mesh_receive_batch_data (batch);
            matched =
              record->kind == ZLINK_MESH_RECORD_INSTANCE_SPOT_ACTIVATION
              && record->kind_data_size
                   == sizeof (zlink_instance_spot_activation_data_t);
            if (matched)
                *out_ =
                  *static_cast<const zlink_instance_spot_activation_data_t *> (
                    record->kind_data);
        }
        zlink_mesh_claim_release (&claim);
        if (batch)
            zlink_mesh_receive_batch_destroy (&batch);
        if (matched)
            return true;
    }
    return false;
}

zlink_instance_spot_placement_t target_for (const bootstrap_t &node_)
{
    zlink_instance_spot_placement_t target;
    memset (&target, 0, sizeof (target));
    target.node_rid = node_.rid;
    target.node_generation = node_.generation;
    make_rid (&target.spot_rid, spot_id);
    target.instance_spot_type = instance_type;
    target.instance_spot_type_size = strlen (instance_type);
    target.message_contract_id = contract_id;
    target.message_contract_id_size = strlen (contract_id);
    return target;
}

bool activate_ready_instance (node_t *node_,
                              owner_snapshot_t *owner_out_,
                              void **spot_out_)
{
    const bootstrap_t self = bootstrap (*node_);
    zlink_instance_spot_placement_t placement =
      target_for (self);
    zlink_msg_t seed;
    if (zlink_msg_init_size (&seed, 4) != ZLINK_CONFIG_OK)
        return false;
    memcpy (zlink_msg_data (&seed), "seed", 4);
    const zlink_submit_result_t submit = zlink_spot_send_to_instance_placement (
      node_->entry, &placement, NULL, &seed, 1, ZLINK_SEND_FLAGS_NONE);
    zlink_msg_close (&seed);
    zlink_instance_spot_activation_data_t activation;
    if (submit != ZLINK_SUBMIT_OK
        || !take_activation (node_->node, &activation))
        return false;
    zlink_instance_spot_claim_result_t claim_result;
    memset (&claim_result, 0, sizeof (claim_result));
    if (zlink_instance_spot_activation_claim_owner (
          &activation.token, "wire-owner", strlen ("wire-owner"),
          &claim_result)
          != ZLINK_CONFIG_OK
        || claim_result.role != ZLINK_INSTANCE_SPOT_CLAIM_LEADER
        || zlink_instance_spot_activation_mark_ready (&activation.token, 5000)
             != ZLINK_CONFIG_OK)
        return false;

    zlink_mesh_claim_t claim;
    if (!take_claim (node_->node, ZLINK_MESH_OWNER_SPOT,
                     ZLINK_MESH_READY_APPLICATION, &claim))
        return false;
    void *batch = zlink_mesh_receive_batch_new (1, 1, 32);
    zlink_mesh_receive_requirements_t required;
    memset (&required, 0, sizeof (required));
    required.struct_size = sizeof (required);
    required.version = 1;
    const bool drained =
      batch
      && zlink_mesh_claim_recv_batch (&claim, batch, &required,
                                      ZLINK_RECV_FLAGS_NONE)
           == ZLINK_RECV_OK
      && zlink_mesh_receive_batch_count (batch) == 1;
    zlink_mesh_claim_release (&claim);
    if (batch)
        zlink_mesh_receive_batch_destroy (&batch);
    if (!drained)
        return false;
    memset (owner_out_, 0, sizeof (*owner_out_));
    owner_out_->node = self;
    owner_out_->spot_generation = claim_result.leader_spot_generation;
    *spot_out_ = claim_result.leader_spot;
    return true;
}

bool wait_pending_application_message (void *spot_, uint64_t *pending_out_)
{
    for (int attempt = 0; attempt < 500; ++attempt) {
        zlink_spot_status_t status;
        memset (&status, 0, sizeof (status));
        status.struct_size = sizeof (status);
        status.version = ZLINK_SPOT_ABI_VERSION;
        if (zlink_spot_status (spot_, &status) == ZLINK_CONFIG_OK
            && status.pending_application_messages > 0) {
            *pending_out_ = status.pending_application_messages;
            return true;
        }
        msleep (5);
    }
    return false;
}

size_t ready_application_count_now (void *node_)
{
    void *ready = zlink_mesh_ready_batch_new (8);
    if (!ready)
        return SIZE_MAX;
    uint32_t residue = 0;
    const zlink_recv_result_t result = zlink_mesh_node_drain_ready (
      node_, ZLINK_MESH_READY_APPLICATION, ready, &residue,
      ZLINK_RECV_FLAGS_DONTWAIT);
    const size_t count = result == ZLINK_RECV_OK
                           ? zlink_mesh_ready_batch_count (ready)
                           : 0;
    zlink_mesh_ready_batch_destroy (&ready);
    return count;
}

winner_report_t receive_request_and_reply (void *node_,
                                           const char *expected_payload_,
                                           const char *reply_payload_)
{
    winner_report_t report;
    memset (&report, 0, sizeof (report));
    report.validation = 1;
    for (int pass = 0; pass < 2 && report.validation != 0; ++pass) {
        zlink_mesh_claim_t claim;
        if (!take_claim (node_, ZLINK_MESH_OWNER_SPOT,
                         ZLINK_MESH_READY_APPLICATION, &claim))
            return report;
        void *batch = zlink_mesh_receive_batch_new (8, 8, 128);
        zlink_mesh_receive_requirements_t required;
        memset (&required, 0, sizeof (required));
        required.struct_size = sizeof (required);
        required.version = 1;
        if (batch
            && zlink_mesh_claim_recv_batch (&claim, batch, &required,
                                            ZLINK_RECV_FLAGS_NONE)
                 == ZLINK_RECV_OK) {
            const zlink_mesh_receive_record_t *records =
              zlink_mesh_receive_batch_data (batch);
            const zlink_msg_t *parts = zlink_mesh_receive_batch_parts (batch);
            for (size_t i = 0; i < zlink_mesh_receive_batch_count (batch); ++i) {
                if (records[i].kind != ZLINK_MESH_RECORD_SPOT_REQUEST
                    || records[i].part_count != 1)
                    continue;
                const zlink_msg_t *part = &parts[records[i].part_offset];
                if (zlink_msg_size (part) != strlen (expected_payload_)
                    || memcmp (
                         zlink_msg_data (const_cast<zlink_msg_t *> (part)),
                         expected_payload_, strlen (expected_payload_))
                         != 0)
                    continue;
                report.operation = records[i].operation_id;
                zlink_msg_t reply;
                if (zlink_msg_init_size (&reply, strlen (reply_payload_))
                    == ZLINK_CONFIG_OK) {
                    memcpy (zlink_msg_data (&reply), reply_payload_,
                            strlen (reply_payload_));
                    if (zlink_mesh_reply (&records[i].reply_token, &reply, 1,
                                         ZLINK_SEND_FLAGS_NONE)
                        == ZLINK_SUBMIT_OK)
                        report.validation = 0;
                    zlink_msg_close (&reply);
                }
                break;
            }
        }
        zlink_mesh_claim_release (&claim);
        if (batch)
            zlink_mesh_receive_batch_destroy (&batch);
    }
    return report;
}

int winner_process (int fd_)
{
    node_t node;
    int result = start_node ("wire-winner", &node);
    if (result != 0)
        return 10 + result;
    const bootstrap_t self = bootstrap (node);
    if (!write_full (fd_, &self, sizeof (self)))
        return 14;
    topology_t topology;
    if (!read_full (fd_, &topology, sizeof (topology))
        || !connect_endpoint (node.node, topology.source.endpoint)
        || !connect_endpoint (node.node, topology.loser.endpoint)
        || !wait_admitted (node.node, 2))
        return 15;

    zlink_instance_spot_placement_t placement =
      target_for (self);
    zlink_msg_t seed;
    if (zlink_msg_init_size (&seed, 4) != ZLINK_CONFIG_OK)
        return 16;
    memcpy (zlink_msg_data (&seed), "seed", 4);
    if (zlink_spot_send_to_instance_placement (node.entry, &placement, NULL, &seed,
                                          1, ZLINK_SEND_FLAGS_NONE)
        != ZLINK_SUBMIT_OK)
        return 17;
    zlink_msg_close (&seed);
    zlink_instance_spot_activation_data_t activation;
    if (!take_activation (node.node, &activation))
        return 18;
    zlink_instance_spot_claim_result_t claim_result;
    memset (&claim_result, 0, sizeof (claim_result));
    if (zlink_instance_spot_activation_claim_owner (
          &activation.token, "wire-owner", strlen ("wire-owner"),
          &claim_result)
          != ZLINK_CONFIG_OK
        || claim_result.role != ZLINK_INSTANCE_SPOT_CLAIM_LEADER
        || zlink_instance_spot_activation_mark_ready (&activation.token, 5000)
             != ZLINK_CONFIG_OK)
        return 19;

    //  Remove the local seed operation before advertising the Ready owner so
    //  the next application claim is necessarily the redirected request.
    zlink_mesh_claim_t seed_claim;
    if (!take_claim (node.node, ZLINK_MESH_OWNER_SPOT,
                     ZLINK_MESH_READY_APPLICATION, &seed_claim))
        return 20;
    void *seed_batch = zlink_mesh_receive_batch_new (1, 1, 32);
    zlink_mesh_receive_requirements_t seed_required;
    memset (&seed_required, 0, sizeof (seed_required));
    seed_required.struct_size = sizeof (seed_required);
    seed_required.version = 1;
    if (!seed_batch
        || zlink_mesh_claim_recv_batch (&seed_claim, seed_batch,
                                        &seed_required,
                                        ZLINK_RECV_FLAGS_NONE)
             != ZLINK_RECV_OK
        || zlink_mesh_receive_batch_count (seed_batch) != 1
        || zlink_mesh_receive_batch_data (seed_batch)[0].kind
             != ZLINK_MESH_RECORD_SPOT_SEND)
        return 21;
    zlink_mesh_claim_release (&seed_claim);
    zlink_mesh_receive_batch_destroy (&seed_batch);
    owner_snapshot_t owner;
    memset (&owner, 0, sizeof (owner));
    owner.node = self;
    owner.spot_generation = claim_result.leader_spot_generation;
    if (!write_full (fd_, &owner, sizeof (owner)))
        return 22;

    unsigned char command = 0;
    if (!read_full (fd_, &command, 1) || command != 0xB1)
        return 23;
    winner_report_t report = receive_request_and_reply (
      node.node, "redirect-me", "redirect-ok");
    if (!write_full (fd_, &report, sizeof (report)))
        return 24;

    if (!read_full (fd_, &command, 1) || command != 0xC2)
        return 25;
    winner_report_t late_report = receive_request_and_reply (
      node.node, "deadline-redirect", "late-reply");
    if (!write_full (fd_, &late_report, sizeof (late_report)))
        return 26;

    if (!read_full (fd_, &command, 1) || command != 0xC5)
        return 27;
    const size_t unexpected_ready = ready_application_count_now (node.node);
    if (!write_full (fd_, &unexpected_ready, sizeof (unexpected_ready)))
        return 28;

    if (!read_full (fd_, &command, 1) || command != 0xE4)
        return 29;
    winner_report_t reconnect_report = receive_request_and_reply (
      node.node, "relay-reconnect", "relay-reconnect-ok");
    if (!write_full (fd_, &reconnect_report, sizeof (reconnect_report)))
        return 30;

    if (!read_full (fd_, &command, 1) || command != 0xD1)
        return 31;
    const unsigned char armed = 0xD2;
    if (!write_full (fd_, &armed, 1))
        return 32;
    if (!read_full (fd_, &command, 1) || command != 0xD3)
        return 33;

    drain_report_t drain_report;
    memset (&drain_report, 0, sizeof (drain_report));
    drain_report.validation = 34;
    if (wait_pending_application_message (
          claim_result.leader_spot,
          &drain_report.pending_before_shutdown)) {
        drain_report.shutdown_result =
          zlink_mesh_node_shutdown (node.node, 100);
        drain_report.shutdown_errno = zlink_errno ();
        drain_report.ready_after_shutdown =
          ready_application_count_now (node.node);
        drain_report.validation = 0;
    }
    if (!write_full (fd_, &drain_report, sizeof (drain_report)))
        return 35;
    unsigned char stop = 0;
    read_full (fd_, &stop, 1);
    stop_node (&node);
    return report.validation;
}

loser_report_t redirect_report (
  zlink_instance_spot_activation_data_t *activation_,
  const owner_snapshot_t &owner_, bool retry_)
{
    loser_report_t report;
    memset (&report, 0, sizeof (report));
    report.validation =
      strcmp (activation_->instance_spot_type, instance_type) == 0
          && strcmp (activation_->message_contract_id, contract_id) == 0
          && activation_->operation_kind
               == ZLINK_INSTANCE_SPOT_OPERATION_REQUEST
        ? 0
        : 1;
    report.first_redirect =
      zlink_instance_spot_activation_redirect (
        &activation_->token, &owner_.node.rid, &activation_->spot_rid,
        owner_.spot_generation);
    if (retry_) {
        report.second_redirect = zlink_instance_spot_activation_redirect (
          &activation_->token, &owner_.node.rid, &activation_->spot_rid,
          owner_.spot_generation);
        report.second_errno = zlink_errno ();
    }
    return report;
}

int loser_process (int fd_)
{
    node_t node;
    int result = start_node ("wire-loser", &node, 20);
    if (result != 0)
        return 30 + result;
    const bootstrap_t self = bootstrap (node);
    if (!write_full (fd_, &self, sizeof (self)))
        return 34;
    topology_t topology;
    if (!read_full (fd_, &topology, sizeof (topology))
        || !connect_endpoint (node.node, topology.winner.endpoint)
        || !wait_admitted (node.node, 2))
        return 35;
    const unsigned char ready = 0xA5;
    if (!write_full (fd_, &ready, 1))
        return 36;
    owner_snapshot_t owner;
    if (!read_full (fd_, &owner, sizeof (owner)))
        return 37;
    zlink_instance_spot_activation_data_t activation;
    if (!take_activation (node.node, &activation))
        return 38;
    loser_report_t report = redirect_report (&activation, owner, true);
    if (!write_full (fd_, &report, sizeof (report)))
        return 39;

    unsigned char command = 0;
    if (!read_full (fd_, &command, 1) || command != 0xC1
        || !take_activation (node.node, &activation))
        return 40;
    loser_report_t deadline_report =
      redirect_report (&activation, owner, true);
    if (!write_full (fd_, &deadline_report, sizeof (deadline_report)))
        return 41;

    if (!read_full (fd_, &command, 1) || command != 0xC3
        || !take_activation (node.node, &activation))
        return 42;
    const unsigned char watchdog_armed = 0xC4;
    if (!write_full (fd_, &watchdog_armed, 1)
        || !read_full (fd_, &command, 1) || command != 0xC6)
        return 43;
    loser_report_t watchdog_report =
      redirect_report (&activation, owner, false);
    watchdog_report.second_errno = zlink_errno ();
    if (!write_full (fd_, &watchdog_report, sizeof (watchdog_report)))
        return 44;

    if (!read_full (fd_, &command, 1) || command != 0xE1
        || !take_activation (node.node, &activation))
        return 45;
    loser_report_t relay_report =
      redirect_report (&activation, owner, true);
    if (!write_full (fd_, &relay_report, sizeof (relay_report)))
        return 46;

    if (!read_full (fd_, &command, 1) || command != 0xE2)
        return 47;
    relay_disconnect_report_t disconnect_report;
    memset (&disconnect_report, 0, sizeof (disconnect_report));
    zlink_test_set_mesh_force_reply_wire_alloc_fault (1);
    disconnect_report.disconnect_result = zlink_mesh_node_disconnect_peer (
      node.node, &topology.winner.rid, topology.winner.generation);
    disconnect_report.disconnect_errno = zlink_errno ();
    for (int attempt = 0; attempt < 500; ++attempt) {
        if (zlink_test_mesh_force_reply_wire_alloc_fault_pending () == 0) {
            disconnect_report.force_fault_consumed = 1;
            break;
        }
        msleep (5);
    }
    if (!write_full (fd_, &disconnect_report, sizeof (disconnect_report)))
        return 48;

    if (!read_full (fd_, &command, 1) || command != 0xE5)
        return 49;
    const int reconnect_ok =
      connect_endpoint (node.node, topology.winner.endpoint)
        && wait_admitted (node.node, 2)
      ? 1
      : 0;
    if (!write_full (fd_, &reconnect_ok, sizeof (reconnect_ok)))
        return 50;

    if (!read_full (fd_, &command, 1) || command != 0xE3
        || !take_activation (node.node, &activation))
        return 51;
    loser_report_t reconnect_report =
      redirect_report (&activation, owner, true);
    if (!write_full (fd_, &reconnect_report, sizeof (reconnect_report)))
        return 52;

    if (!read_full (fd_, &command, 1) || command != 0xE6
        || !take_activation (node.node, &activation))
        return 53;
    zlink_instance_spot_claim_result_t local_claim;
    memset (&local_claim, 0, sizeof (local_claim));
    const int local_ready =
      zlink_instance_spot_activation_claim_owner (
        &activation.token, "wire-local-owner", strlen ("wire-local-owner"),
        &local_claim)
          == ZLINK_CONFIG_OK
          && local_claim.role == ZLINK_INSTANCE_SPOT_CLAIM_LEADER
          && zlink_instance_spot_activation_mark_ready (
               &activation.token, 20000)
               == ZLINK_CONFIG_OK
        ? 1
        : 0;
    if (!write_full (fd_, &local_ready, sizeof (local_ready)))
        return 54;

    unsigned char stop = 0;
    read_full (fd_, &stop, 1);
    stop_node (&node);
    return report.validation != 0 || deadline_report.validation != 0
             || watchdog_report.validation != 0
             || relay_report.validation != 0
             || reconnect_report.validation != 0;
}

bool source_completion (void *node_,
                        zlink_mesh_operation_id_t operation_,
                        zlink_request_result_t terminal_result_,
                        int failure_errno_,
                        const char *payload_)
{
    zlink_mesh_claim_t claim;
    if (!take_claim (node_, ZLINK_MESH_OWNER_SPOT,
                     ZLINK_MESH_READY_INFRASTRUCTURE, &claim))
        return false;
    void *batch = zlink_mesh_receive_batch_new (4, 4, 128);
    zlink_mesh_receive_requirements_t required;
    memset (&required, 0, sizeof (required));
    required.struct_size = sizeof (required);
    required.version = 1;
    bool ok = batch
              && zlink_mesh_claim_recv_batch (&claim, batch, &required,
                                               ZLINK_RECV_FLAGS_NONE)
                   == ZLINK_RECV_OK;
    if (ok) {
        const zlink_mesh_receive_record_t *record =
          zlink_mesh_receive_batch_data (batch);
        const zlink_msg_t *parts = zlink_mesh_receive_batch_parts (batch);
        ok = zlink_mesh_receive_batch_count (batch) == 1
             && record[0].kind == ZLINK_MESH_RECORD_COMPLETION
             && record[0].operation_id.high == operation_.high
             && record[0].operation_id.low == operation_.low
             && record[0].terminal_result == terminal_result_
             && record[0].failure_errno == failure_errno_;
        if (!ok)
            fprintf (
              stderr,
              "source completion mismatch: count=%zu kind=%d op=%llu:%llu "
              "expected=%llu:%llu terminal=%d/%d errno=%d/%d\n",
              zlink_mesh_receive_batch_count (batch),
              static_cast<int> (record[0].kind),
              static_cast<unsigned long long> (record[0].operation_id.high),
              static_cast<unsigned long long> (record[0].operation_id.low),
              static_cast<unsigned long long> (operation_.high),
              static_cast<unsigned long long> (operation_.low),
              static_cast<int> (record[0].terminal_result),
              static_cast<int> (terminal_result_), record[0].failure_errno,
              failure_errno_);
        if (ok && payload_ != NULL)
            ok = record[0].part_count == 1
                 && zlink_msg_size (&parts[record[0].part_offset])
                      == strlen (payload_)
                 && memcmp (zlink_msg_data (const_cast<zlink_msg_t *> (
                              &parts[record[0].part_offset])),
                            payload_, strlen (payload_))
                      == 0;
        if (ok && payload_ == NULL)
            ok = record[0].part_count == 0;
    }
    zlink_mesh_claim_release (&claim);
    if (batch)
        zlink_mesh_receive_batch_destroy (&batch);
    return ok;
}

bool source_has_duplicate_completion (
  void *node_, zlink_mesh_operation_id_t operation_)
{
    for (int attempt = 0; attempt < 20; ++attempt) {
        void *ready = zlink_mesh_ready_batch_new (4);
        if (!ready)
            return true;
        uint32_t residue = 0;
        const zlink_recv_result_t ready_result = zlink_mesh_node_drain_ready (
          node_, ZLINK_MESH_READY_INFRASTRUCTURE, ready, &residue,
          ZLINK_RECV_FLAGS_DONTWAIT);
        if (ready_result != ZLINK_RECV_OK
            || zlink_mesh_ready_batch_count (ready) == 0) {
            zlink_mesh_ready_batch_destroy (&ready);
            msleep (5);
            continue;
        }

        bool duplicate = false;
        const zlink_mesh_ready_record_t *records =
          zlink_mesh_ready_batch_data (ready);
        for (size_t i = 0; i < zlink_mesh_ready_batch_count (ready); ++i) {
            if (records[i].owner_kind != ZLINK_MESH_OWNER_SPOT
                || records[i].domain != ZLINK_MESH_READY_INFRASTRUCTURE)
                continue;
            zlink_mesh_claim_t claim;
            memset (&claim, 0, sizeof (claim));
            if (zlink_mesh_ready_batch_take_claim (ready, i, &claim)
                != ZLINK_CONFIG_OK)
                continue;
            void *batch = zlink_mesh_receive_batch_new (4, 4, 128);
            zlink_mesh_receive_requirements_t required;
            memset (&required, 0, sizeof (required));
            required.struct_size = sizeof (required);
            required.version = 1;
            if (batch
                && zlink_mesh_claim_recv_batch (&claim, batch, &required,
                                                ZLINK_RECV_FLAGS_NONE)
                     == ZLINK_RECV_OK) {
                const zlink_mesh_receive_record_t *received =
                  zlink_mesh_receive_batch_data (batch);
                for (size_t j = 0;
                     j < zlink_mesh_receive_batch_count (batch); ++j)
                    duplicate = duplicate
                                || (received[j].kind
                                      == ZLINK_MESH_RECORD_COMPLETION
                                    && received[j].operation_id.high
                                         == operation_.high
                                    && received[j].operation_id.low
                                         == operation_.low);
            }
            zlink_mesh_claim_release (&claim);
            if (batch)
                zlink_mesh_receive_batch_destroy (&batch);
        }
        zlink_mesh_ready_batch_destroy (&ready);
        if (duplicate)
            return true;
    }
    return false;
}

bool take_request_token (void *node_,
                         const char *payload_,
                         zlink_mesh_reply_token_t *token_out_,
                         zlink_mesh_operation_id_t *operation_out_)
{
    zlink_mesh_claim_t claim;
    if (!take_claim (node_, ZLINK_MESH_OWNER_SPOT,
                     ZLINK_MESH_READY_APPLICATION, &claim))
        return false;
    void *batch = zlink_mesh_receive_batch_new (1, 1, 64);
    zlink_mesh_receive_requirements_t required;
    memset (&required, 0, sizeof (required));
    required.struct_size = sizeof (required);
    required.version = 1;
    bool ok = batch
              && zlink_mesh_claim_recv_batch (&claim, batch, &required,
                                               ZLINK_RECV_FLAGS_NONE)
                   == ZLINK_RECV_OK
              && zlink_mesh_receive_batch_count (batch) == 1;
    if (ok) {
        const zlink_mesh_receive_record_t *record =
          zlink_mesh_receive_batch_data (batch);
        const zlink_msg_t *parts = zlink_mesh_receive_batch_parts (batch);
        ok = record[0].kind == ZLINK_MESH_RECORD_SPOT_REQUEST
             && record[0].part_count == 1
             && zlink_msg_size (&parts[record[0].part_offset])
                  == strlen (payload_)
             && memcmp (zlink_msg_data (const_cast<zlink_msg_t *> (
                          &parts[record[0].part_offset])),
                        payload_, strlen (payload_))
                  == 0;
        if (ok) {
            *token_out_ = record[0].reply_token;
            *operation_out_ = record[0].operation_id;
        }
    }
    zlink_mesh_claim_release (&claim);
    if (batch)
        zlink_mesh_receive_batch_destroy (&batch);
    return ok;
}

int stale_origin_source_process (int fd_)
{
    owner_snapshot_t owner;
    uint64_t minimum_generation = 0;
    char payload[16];
    memset (payload, 0, sizeof (payload));
    if (!read_full (fd_, &owner, sizeof (owner))
        || !read_full (fd_, &minimum_generation,
                       sizeof (minimum_generation))
        || !read_full (fd_, payload, sizeof (payload)))
        return 60;

    node_t source;
    int start_result = start_node ("wire-origin", &source);
    if (start_result != 0)
        return 61 + start_result;
    while (source.status.lifecycle_generation <= minimum_generation) {
        stop_node (&source);
        start_result = start_node ("wire-origin", &source);
        if (start_result != 0)
            return 64 + start_result;
    }
    stale_origin_source_report_t report;
    memset (&report, 0, sizeof (report));
    report.validation = 1;
    report.source = bootstrap (source);
    const bool connected = connect_endpoint (source.node, owner.node.endpoint)
                           && wait_admitted (source.node, 1);
    if (!write_full (fd_, &report.source, sizeof (report.source)))
        return 67;
    unsigned char command = 0;
    if (!read_full (fd_, &command, 1) || command != 0x91)
        return 68;
    if (connected) {
        const zlink_routing_id_t target_spot = instance_spot_rid ();
        zlink_msg_t request;
        if (zlink_msg_init_size (&request, strlen (payload))
            == ZLINK_CONFIG_OK) {
            memcpy (zlink_msg_data (&request), payload, strlen (payload));
            if (zlink_spot_request_to_spot (
                  source.entry, &owner.node.rid, &target_spot,
                  owner.spot_generation, NULL, &request, 1,
                  &report.operation, ZLINK_SEND_FLAGS_NONE, 0)
                == ZLINK_SUBMIT_OK)
                report.validation = 0;
            zlink_msg_close (&request);
        }
    }
    if (!write_full (fd_, &report, sizeof (report)))
        return 69;

    if (!read_full (fd_, &command, 1))
        return 70;
    if (command == 0xA1) {
        stop_node (&source);
        return report.validation;
    }
    if (command == 0xC1) {
        stale_origin_completion_report_t forced;
        memset (&forced, 0, sizeof (forced));
        forced.current_completion = source_completion (
          source.node, report.operation, ZLINK_REQUEST_NOT_CONNECTED,
          ENOTCONN, NULL)
                                      ? 1
                                      : 0;
        forced.duplicate_completion =
          source_has_duplicate_completion (source.node, report.operation)
            ? 1
            : 0;
        if (!write_full (fd_, &forced, sizeof (forced)))
            return 71;
        stop_node (&source);
        return report.validation;
    }
    if (command != 0xB1)
        return 72;
    stale_origin_completion_report_t completion;
    memset (&completion, 0, sizeof (completion));
    completion.no_completion_before_current_reply =
      source_has_duplicate_completion (source.node, report.operation) ? 0 : 1;
    const unsigned char checked = 0xB2;
    if (!write_full (fd_, &checked, 1)
        || !read_full (fd_, &command, 1) || command != 0xB3)
        return 73;
    completion.current_completion = source_completion (
      source.node, report.operation, ZLINK_REQUEST_OK, 0, "current-reply")
                                      ? 1
                                      : 0;
    completion.duplicate_completion =
      source_has_duplicate_completion (source.node, report.operation) ? 1 : 0;
    if (!write_full (fd_, &completion, sizeof (completion)))
        return 74;
    stop_node (&source);
    return report.validation;
}

int presend_direct_target_process (int fd_)
{
    node_t target;
    const int start_result =
      start_node ("wire-presend-direct-target", &target);
    if (start_result != 0)
        return 80 + start_result;

    owner_snapshot_t owner;
    void *instance_spot = NULL;
    if (!activate_ready_instance (&target, &owner, &instance_spot)
        || zlink_instance_spot_renew_owner_admission (instance_spot, 10000)
             != ZLINK_CONFIG_OK
        || !write_full (fd_, &owner, sizeof (owner)))
        return 84;

    unsigned char command = 0;
    if (!read_full (fd_, &command, 1) || command != 0xD1)
        return 85;
    const size_t ready_count = ready_application_count_now (target.node);
    if (!write_full (fd_, &ready_count, sizeof (ready_count)))
        return 86;

    if (!read_full (fd_, &command, 1) || command != 0xD2)
        return 87;
    const winner_report_t report = receive_request_and_reply (
      target.node, "direct-presend", "direct-presend-ok");
    if (!write_full (fd_, &report, sizeof (report)))
        return 88;

    read_full (fd_, &command, 1);
    stop_node (&target);
    return report.validation;
}

int presend_redirect_source_process (int fd_)
{
    node_t source;
    const int start_result =
      start_node ("wire-presend-redirect-source", &source);
    if (start_result != 0)
        return 90 + start_result;

    const bootstrap_t self = bootstrap (source);
    bootstrap_t redirector;
    if (!write_full (fd_, &self, sizeof (self))
        || !read_full (fd_, &redirector, sizeof (redirector))
        || !connect_endpoint (source.node, redirector.endpoint)
        || !wait_admitted_generation (
          source.node, redirector.rid, redirector.generation))
        return 94;

    presend_source_report_t report;
    memset (&report, 0, sizeof (report));
    report.validation = 1;
    zlink_instance_spot_placement_t placement = target_for (redirector);
    zlink_msg_t request;
    if (zlink_msg_init_size (&request, strlen ("redirect-presend"))
        == ZLINK_CONFIG_OK) {
        memcpy (zlink_msg_data (&request), "redirect-presend",
                strlen ("redirect-presend"));
        if (zlink_spot_request_to_instance_placement (
              source.entry, &placement, NULL, &request, 1,
              &report.operation, ZLINK_SEND_FLAGS_NONE, 10000)
            == ZLINK_SUBMIT_OK)
            report.validation = 0;
        zlink_msg_close (&request);
    }
    if (!write_full (fd_, &report, sizeof (report)))
        return 95;

    unsigned char command = 0;
    if (!read_full (fd_, &command, 1) || command != 0xA3)
        return 96;
    stale_origin_completion_report_t completion;
    memset (&completion, 0, sizeof (completion));
    completion.current_completion = source_completion (
      source.node, report.operation, ZLINK_REQUEST_OK, 0,
      "redirect-presend-ok")
                                      ? 1
                                      : 0;
    completion.duplicate_completion =
      source_has_duplicate_completion (source.node, report.operation) ? 1 : 0;
    if (!write_full (fd_, &completion, sizeof (completion)))
        return 97;
    stop_node (&source);
    return report.validation;
}

int presend_redirect_winner_process (int fd_)
{
    node_t winner;
    const int start_result =
      start_node ("wire-presend-redirect-winner", &winner);
    if (start_result != 0)
        return 100 + start_result;

    owner_snapshot_t owner;
    void *instance_spot = NULL;
    if (!activate_ready_instance (&winner, &owner, &instance_spot)
        || zlink_instance_spot_renew_owner_admission (instance_spot, 20000)
             != ZLINK_CONFIG_OK
        || !write_full (fd_, &owner, sizeof (owner)))
        return 104;

    unsigned char command = 0;
    if (!read_full (fd_, &command, 1) || command != 0xB1)
        return 105;
    const size_t ready_count = ready_application_count_now (winner.node);
    if (!write_full (fd_, &ready_count, sizeof (ready_count)))
        return 106;

    if (!read_full (fd_, &command, 1) || command != 0xB2)
        return 107;
    const winner_report_t report = receive_request_and_reply (
      winner.node, "redirect-presend", "redirect-presend-ok");
    if (!write_full (fd_, &report, sizeof (report)))
        return 108;

    read_full (fd_, &command, 1);
    stop_node (&winner);
    return report.validation;
}
}
#endif

void test_remote_placement_and_redirect_preserve_request ()
{
#if defined _WIN32
    TEST_IGNORE_MESSAGE ("fork-based Instance wire contract is POSIX-only");
#else
    int loser_socket[2];
    int winner_socket[2];
    TEST_ASSERT_EQUAL_INT (0, socketpair (AF_UNIX, SOCK_STREAM, 0,
                                          loser_socket));
    TEST_ASSERT_EQUAL_INT (0, socketpair (AF_UNIX, SOCK_STREAM, 0,
                                          winner_socket));
    const pid_t loser_pid = fork ();
    TEST_ASSERT_TRUE (loser_pid >= 0);
    if (loser_pid == 0) {
        close (loser_socket[0]);
        close (winner_socket[0]);
        close (winner_socket[1]);
        const int child_result = loser_process (loser_socket[1]);
        if (child_result != 0)
            fprintf (stderr, "instance loser child failed: %d\n", child_result);
        _exit (child_result);
    }
    const pid_t winner_pid = fork ();
    TEST_ASSERT_TRUE (winner_pid >= 0);
    if (winner_pid == 0) {
        close (winner_socket[0]);
        close (loser_socket[0]);
        close (loser_socket[1]);
        const int child_result = winner_process (winner_socket[1]);
        if (child_result != 0)
            fprintf (stderr, "instance winner child failed: %d\n", child_result);
        _exit (child_result);
    }
    close (loser_socket[1]);
    close (winner_socket[1]);

    node_t source;
    TEST_ASSERT_EQUAL_INT (0, start_node ("wire-source", &source));
    topology_t topology;
    memset (&topology, 0, sizeof (topology));
    topology.source = bootstrap (source);
    TEST_ASSERT_TRUE (read_full (loser_socket[0], &topology.loser,
                                 sizeof (topology.loser)));
    TEST_ASSERT_TRUE (read_full (winner_socket[0], &topology.winner,
                                 sizeof (topology.winner)));
    TEST_ASSERT_TRUE (write_full (loser_socket[0], &topology,
                                  sizeof (topology)));
    TEST_ASSERT_TRUE (write_full (winner_socket[0], &topology,
                                  sizeof (topology)));
    TEST_ASSERT_TRUE (connect_endpoint (source.node, topology.loser.endpoint));
    TEST_ASSERT_TRUE (connect_endpoint (source.node, topology.winner.endpoint));
    TEST_ASSERT_TRUE (wait_admitted (source.node, 2));

    unsigned char loser_ready = 0;
    owner_snapshot_t owner;
    TEST_ASSERT_TRUE (read_full (loser_socket[0], &loser_ready, 1));
    TEST_ASSERT_EQUAL_HEX8 (0xA5, loser_ready);
    TEST_ASSERT_TRUE (read_full (winner_socket[0], &owner, sizeof (owner)));
    TEST_ASSERT_TRUE (write_full (loser_socket[0], &owner, sizeof (owner)));

    zlink_instance_spot_placement_t placement =
      target_for (topology.loser);
    zlink_msg_t request;
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK,
                           zlink_msg_init_size (&request,
                                                strlen ("redirect-me")));
    memcpy (zlink_msg_data (&request), "redirect-me", strlen ("redirect-me"));
    zlink_mesh_operation_id_t operation;
    memset (&operation, 0, sizeof (operation));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_OK,
      zlink_spot_request_to_instance_placement (
        source.entry, &placement, NULL, &request, 1, &operation,
        ZLINK_SEND_FLAGS_NONE, 5000));
    zlink_msg_close (&request);

    loser_report_t loser_report;
    winner_report_t winner_report;
    TEST_ASSERT_TRUE (read_full (loser_socket[0], &loser_report,
                                 sizeof (loser_report)));
    //  Wait past the redirector's 20 ms activation watchdog. The winner must
    //  still receive and complete the redirected request because redirect
    //  success cancels the old target watchdog task.
    msleep (60);
    const unsigned char release_first_reply = 0xB1;
    TEST_ASSERT_TRUE (
      write_full (winner_socket[0], &release_first_reply, 1));
    TEST_ASSERT_TRUE (read_full (winner_socket[0], &winner_report,
                                 sizeof (winner_report)));
    TEST_ASSERT_EQUAL_INT (0, loser_report.validation);
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK, loser_report.first_redirect);
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_INVALID_STATE,
                           loser_report.second_redirect);
    TEST_ASSERT_EQUAL_INT (ESTALE, loser_report.second_errno);
    TEST_ASSERT_EQUAL_INT (0, winner_report.validation);
    TEST_ASSERT_EQUAL_UINT64 (operation.high, winner_report.operation.high);
    TEST_ASSERT_EQUAL_UINT64 (operation.low, winner_report.operation.low);
    TEST_ASSERT_TRUE (source_completion (source.node, operation,
                                         ZLINK_REQUEST_OK, 0,
                                         "redirect-ok"));

    //  Redirect preserves the original call deadline. The source times out
    //  once even though the winning handler claims and replies afterwards.
    zlink_msg_t deadline_request;
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK,
      zlink_msg_init_size (&deadline_request,
                            strlen ("deadline-redirect")));
    memcpy (zlink_msg_data (&deadline_request), "deadline-redirect",
            strlen ("deadline-redirect"));
    zlink_mesh_operation_id_t deadline_operation;
    memset (&deadline_operation, 0, sizeof (deadline_operation));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_OK,
      zlink_spot_request_to_instance_placement (
        source.entry, &placement, NULL, &deadline_request, 1,
        &deadline_operation, ZLINK_SEND_FLAGS_NONE, 100));
    zlink_msg_close (&deadline_request);
    const unsigned char redirect_deadline = 0xC1;
    TEST_ASSERT_TRUE (
      write_full (loser_socket[0], &redirect_deadline, 1));
    loser_report_t deadline_redirect_report;
    TEST_ASSERT_TRUE (read_full (loser_socket[0], &deadline_redirect_report,
                                 sizeof (deadline_redirect_report)));
    TEST_ASSERT_EQUAL_INT (0, deadline_redirect_report.validation);
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK,
                           deadline_redirect_report.first_redirect);
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_INVALID_STATE,
                           deadline_redirect_report.second_redirect);
    TEST_ASSERT_EQUAL_INT (ESTALE, deadline_redirect_report.second_errno);
    TEST_ASSERT_TRUE (source_completion (
      source.node, deadline_operation, ZLINK_REQUEST_TIMED_OUT, ETIMEDOUT,
      NULL));

    const unsigned char release_late_reply = 0xC2;
    TEST_ASSERT_TRUE (
      write_full (winner_socket[0], &release_late_reply, 1));
    winner_report_t late_reply_report;
    TEST_ASSERT_TRUE (read_full (winner_socket[0], &late_reply_report,
                                 sizeof (late_reply_report)));
    TEST_ASSERT_EQUAL_INT (0, late_reply_report.validation);
    TEST_ASSERT_EQUAL_UINT64 (deadline_operation.high,
                              late_reply_report.operation.high);
    TEST_ASSERT_EQUAL_UINT64 (deadline_operation.low,
                              late_reply_report.operation.low);
    TEST_ASSERT_FALSE (
      source_has_duplicate_completion (source.node, deadline_operation));

    //  Once the redirector watchdog has completed the placement, a copied
    //  late activation token cannot redirect and cannot admit work to the
    //  winning Ready Instance.
    zlink_msg_t watchdog_request;
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK,
      zlink_msg_init_size (&watchdog_request,
                            strlen ("watchdog-redirect")));
    memcpy (zlink_msg_data (&watchdog_request), "watchdog-redirect",
            strlen ("watchdog-redirect"));
    zlink_mesh_operation_id_t watchdog_operation;
    memset (&watchdog_operation, 0, sizeof (watchdog_operation));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_OK,
      zlink_spot_request_to_instance_placement (
        source.entry, &placement, NULL, &watchdog_request, 1,
        &watchdog_operation, ZLINK_SEND_FLAGS_NONE, 1000));
    zlink_msg_close (&watchdog_request);
    const unsigned char arm_watchdog = 0xC3;
    TEST_ASSERT_TRUE (write_full (loser_socket[0], &arm_watchdog, 1));
    unsigned char watchdog_armed = 0;
    TEST_ASSERT_TRUE (read_full (loser_socket[0], &watchdog_armed, 1));
    TEST_ASSERT_EQUAL_HEX8 (0xC4, watchdog_armed);
    TEST_ASSERT_TRUE (source_completion (
      source.node, watchdog_operation, ZLINK_REQUEST_TIMED_OUT, ETIMEDOUT,
      NULL));
    const unsigned char attempt_late_redirect = 0xC6;
    TEST_ASSERT_TRUE (
      write_full (loser_socket[0], &attempt_late_redirect, 1));
    loser_report_t watchdog_redirect_report;
    TEST_ASSERT_TRUE (read_full (loser_socket[0], &watchdog_redirect_report,
                                 sizeof (watchdog_redirect_report)));
    TEST_ASSERT_EQUAL_INT (0, watchdog_redirect_report.validation);
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_INVALID_STATE,
                           watchdog_redirect_report.first_redirect);
    TEST_ASSERT_EQUAL_INT (ESTALE, watchdog_redirect_report.second_errno);
    TEST_ASSERT_FALSE (
      source_has_duplicate_completion (source.node, watchdog_operation));
    const unsigned char verify_no_late_admission = 0xC5;
    TEST_ASSERT_TRUE (
      write_full (winner_socket[0], &verify_no_late_admission, 1));
    size_t unexpected_ready = SIZE_MAX;
    TEST_ASSERT_TRUE (read_full (winner_socket[0], &unexpected_ready,
                                 sizeof (unexpected_ready)));
    TEST_ASSERT_EQUAL_UINT64 (0, unexpected_ready);

    //  A redirect creates a relay route on the loser. Losing only the exact
    //  winner transport completes the timeout-free source operation while
    //  the independent source-to-loser connection remains admitted.
    zlink_msg_t relay_request;
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK,
      zlink_msg_init_size (&relay_request, strlen ("relay-peer-down")));
    memcpy (zlink_msg_data (&relay_request), "relay-peer-down",
            strlen ("relay-peer-down"));
    zlink_mesh_operation_id_t relay_operation;
    memset (&relay_operation, 0, sizeof (relay_operation));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_OK,
      zlink_spot_request_to_instance_placement (
        source.entry, &placement, NULL, &relay_request, 1,
        &relay_operation, ZLINK_SEND_FLAGS_NONE, 0));
    zlink_msg_close (&relay_request);
    const unsigned char redirect_for_relay = 0xE1;
    TEST_ASSERT_TRUE (
      write_full (loser_socket[0], &redirect_for_relay, 1));
    loser_report_t relay_redirect_report;
    TEST_ASSERT_TRUE (read_full (loser_socket[0], &relay_redirect_report,
                                 sizeof (relay_redirect_report)));
    TEST_ASSERT_EQUAL_INT (0, relay_redirect_report.validation);
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK,
                           relay_redirect_report.first_redirect);
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_INVALID_STATE,
                           relay_redirect_report.second_redirect);
    TEST_ASSERT_EQUAL_INT (ESTALE, relay_redirect_report.second_errno);
    const unsigned char disconnect_winner = 0xE2;
    TEST_ASSERT_TRUE (
      write_full (loser_socket[0], &disconnect_winner, 1));
    relay_disconnect_report_t relay_disconnect_report;
    TEST_ASSERT_TRUE (read_full (loser_socket[0], &relay_disconnect_report,
                                 sizeof (relay_disconnect_report)));
    TEST_ASSERT_EQUAL_INT (ZLINK_CONNECT_OK,
                           relay_disconnect_report.disconnect_result);
    TEST_ASSERT_EQUAL_INT (1,
                           relay_disconnect_report.force_fault_consumed);
    TEST_ASSERT_FALSE (
      source_has_duplicate_completion (source.node, relay_operation));

    //  Re-admitting a peer supplies the send-ready edge that retries the
    //  durable forced terminal. The requester observes exactly one result.
    const unsigned char reconnect_winner = 0xE5;
    TEST_ASSERT_TRUE (
      write_full (loser_socket[0], &reconnect_winner, 1));
    int reconnect_ok = 0;
    TEST_ASSERT_TRUE (
      read_full (loser_socket[0], &reconnect_ok, sizeof (reconnect_ok)));
    TEST_ASSERT_EQUAL_INT (1, reconnect_ok);
    TEST_ASSERT_TRUE (source_completion (
      source.node, relay_operation, ZLINK_REQUEST_NOT_CONNECTED, ENOTCONN,
      NULL));
    TEST_ASSERT_FALSE (
      source_has_duplicate_completion (source.node, relay_operation));

    //  A delayed disconnect from the old physical connection must not
    //  terminate the new relay operation on the replacement connection.
    zlink_msg_t reconnect_request;
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK,
      zlink_msg_init_size (&reconnect_request, strlen ("relay-reconnect")));
    memcpy (zlink_msg_data (&reconnect_request), "relay-reconnect",
            strlen ("relay-reconnect"));
    zlink_mesh_operation_id_t reconnect_operation;
    memset (&reconnect_operation, 0, sizeof (reconnect_operation));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_OK,
      zlink_spot_request_to_instance_placement (
        source.entry, &placement, NULL, &reconnect_request, 1,
        &reconnect_operation, ZLINK_SEND_FLAGS_NONE, 1000));
    zlink_msg_close (&reconnect_request);
    const unsigned char redirect_after_reconnect = 0xE3;
    TEST_ASSERT_TRUE (
      write_full (loser_socket[0], &redirect_after_reconnect, 1));
    loser_report_t reconnect_redirect_report;
    TEST_ASSERT_TRUE (read_full (loser_socket[0], &reconnect_redirect_report,
                                 sizeof (reconnect_redirect_report)));
    TEST_ASSERT_EQUAL_INT (0, reconnect_redirect_report.validation);
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK,
                           reconnect_redirect_report.first_redirect);
    const unsigned char reply_after_reconnect = 0xE4;
    TEST_ASSERT_TRUE (
      write_full (winner_socket[0], &reply_after_reconnect, 1));
    winner_report_t reconnect_reply_report;
    TEST_ASSERT_TRUE (read_full (winner_socket[0], &reconnect_reply_report,
                                 sizeof (reconnect_reply_report)));
    TEST_ASSERT_EQUAL_INT (0, reconnect_reply_report.validation);
    TEST_ASSERT_EQUAL_UINT64 (reconnect_operation.high,
                              reconnect_reply_report.operation.high);
    TEST_ASSERT_EQUAL_UINT64 (reconnect_operation.low,
                              reconnect_reply_report.operation.low);
    TEST_ASSERT_TRUE (source_completion (
      source.node, reconnect_operation, ZLINK_REQUEST_OK, 0,
      "relay-reconnect-ok"));
    TEST_ASSERT_FALSE (
      source_has_duplicate_completion (source.node, reconnect_operation));

    //  A timeout-free remote Instance operation is owned by the exact peer
    //  lifetime selected at submit. Wrong-generation disconnect requests do
    //  not disturb it. If recording the exact disconnect tombstone runs out
    //  of memory between wire send and route commit, conservative overflow
    //  fencing still completes the operation once and releases the flight.
    zlink_msg_t expected_connection_request;
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK,
      zlink_msg_init_size (&expected_connection_request,
                            strlen ("expected-connection")));
    memcpy (zlink_msg_data (&expected_connection_request),
            "expected-connection", strlen ("expected-connection"));
    zlink_mesh_operation_id_t expected_connection_operation;
    memset (&expected_connection_operation, 0,
            sizeof (expected_connection_operation));
    std::atomic<int> expected_connection_submit (-1);
    zlink_test_mesh_pause_remote_route_before_commit (1);
    std::thread expected_connection_submitter ([&] {
        expected_connection_submit.store (
          zlink_spot_request_to_instance_placement (
            source.entry, &placement, NULL, &expected_connection_request, 1,
            &expected_connection_operation, ZLINK_SEND_FLAGS_NONE, 0),
          std::memory_order_release);
    });
    bool expected_connection_paused = false;
    for (int attempt = 0;
         attempt < 1000 && !expected_connection_paused; ++attempt) {
        expected_connection_paused =
          zlink_test_mesh_remote_route_before_commit_paused () != 0;
        if (!expected_connection_paused)
            msleep (1);
    }
    zlink_connect_result_t old_connection_disconnect =
      ZLINK_CONNECT_INTERNAL_ERROR;
    bool replacement_connection_admitted = false;
    if (expected_connection_paused) {
        const unsigned char make_target_ready = 0xE6;
        int target_ready = 0;
        if (!write_full (loser_socket[0], &make_target_ready, 1)
            || !read_full (loser_socket[0], &target_ready,
                           sizeof (target_ready))
            || target_ready != 1)
            expected_connection_paused = false;
        old_connection_disconnect = zlink_mesh_node_disconnect_peer (
          source.node, &topology.loser.rid, topology.loser.generation);
        replacement_connection_admitted =
          old_connection_disconnect == ZLINK_CONNECT_OK
          && connect_endpoint (source.node, topology.loser.endpoint)
          && wait_admitted (source.node, 2);
    }
    zlink_test_mesh_pause_remote_route_before_commit (0);
    expected_connection_submitter.join ();
    zlink_msg_close (&expected_connection_request);
    TEST_ASSERT_TRUE (expected_connection_paused);
    TEST_ASSERT_EQUAL_INT (ZLINK_CONNECT_OK, old_connection_disconnect);
    TEST_ASSERT_TRUE (replacement_connection_admitted);
    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_OK,
      expected_connection_submit.load (std::memory_order_acquire));
    TEST_ASSERT_TRUE (source_completion (
      source.node, expected_connection_operation,
      ZLINK_REQUEST_NOT_CONNECTED, ENOTCONN, NULL));
    TEST_ASSERT_FALSE (source_has_duplicate_completion (
      source.node, expected_connection_operation));

    zlink_msg_t peer_down_request;
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK,
      zlink_msg_init_size (&peer_down_request, strlen ("peer-down")));
    memcpy (zlink_msg_data (&peer_down_request), "peer-down",
            strlen ("peer-down"));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONNECT_CONFLICT,
      zlink_mesh_node_disconnect_peer (
        source.node, &topology.loser.rid, topology.loser.generation - 1));
    TEST_ASSERT_EQUAL_INT (ESTALE, zlink_errno ());
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONNECT_CONFLICT,
      zlink_mesh_node_disconnect_peer (
        source.node, &topology.loser.rid, topology.loser.generation + 1));
    TEST_ASSERT_EQUAL_INT (ESTALE, zlink_errno ());

    zlink_mesh_operation_id_t peer_down_operation;
    memset (&peer_down_operation, 0, sizeof (peer_down_operation));
    std::atomic<int> peer_down_submit_result (-1);
    zlink_test_mesh_pause_remote_route_before_commit (1);
    std::thread peer_down_submitter ([&] {
        peer_down_submit_result.store (
          zlink_spot_request_to_instance_placement (
            source.entry, &placement, NULL, &peer_down_request, 1,
            &peer_down_operation, ZLINK_SEND_FLAGS_NONE, 0),
          std::memory_order_release);
    });
    bool route_paused = false;
    for (int attempt = 0; attempt < 1000 && !route_paused; ++attempt) {
        route_paused =
          zlink_test_mesh_remote_route_before_commit_paused () != 0;
        if (!route_paused)
            msleep (1);
    }
    const size_t flights_while_paused =
      zlink_test_mesh_remote_route_flights (source.node);
    zlink_test_set_mesh_alloc_fault (1);
    zlink_connect_result_t exact_disconnect = ZLINK_CONNECT_INTERNAL_ERROR;
    if (route_paused) {
        exact_disconnect = zlink_mesh_node_disconnect_peer (
          source.node, &topology.loser.rid, topology.loser.generation);
    }
    bool overflow_observed = false;
    for (int attempt = 0; attempt < 1000 && !overflow_observed; ++attempt) {
        overflow_observed =
          zlink_test_mesh_remote_route_disconnect_overflow_observed (
            source.node)
          != 0;
        if (!overflow_observed)
            msleep (1);
    }
    zlink_test_mesh_pause_remote_route_before_commit (0);
    peer_down_submitter.join ();
    zlink_test_set_mesh_alloc_fault (0);
    zlink_msg_close (&peer_down_request);

    TEST_ASSERT_TRUE (route_paused);
    TEST_ASSERT_EQUAL_UINT64 (1, flights_while_paused);
    TEST_ASSERT_EQUAL_INT (ZLINK_CONNECT_OK, exact_disconnect);
    TEST_ASSERT_TRUE (overflow_observed);
    TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_OK,
                           peer_down_submit_result.load (
                             std::memory_order_acquire));
    TEST_ASSERT_EQUAL_UINT64 (
      0, zlink_test_mesh_remote_route_flights (source.node));
    TEST_ASSERT_TRUE (source_completion (
      source.node, peer_down_operation, ZLINK_REQUEST_NOT_CONNECTED,
      ENOTCONN, NULL));
    TEST_ASSERT_FALSE (
      source_has_duplicate_completion (source.node, peer_down_operation));

    //  The same Ready authority with a stale node generation is never
    //  resolved to the current peer generation implicitly.
    zlink_routing_id_t stale_node_rid = owner.node.rid;
    stale_node_rid.data[0] ^= 0x01;
    const zlink_routing_id_t target_spot = instance_spot_rid ();
    zlink_msg_t stale_request;
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK,
      zlink_msg_init_size (&stale_request, strlen ("stale-node")));
    memcpy (zlink_msg_data (&stale_request), "stale-node",
            strlen ("stale-node"));
    zlink_mesh_operation_id_t stale_operation;
    memset (&stale_operation, 0, sizeof (stale_operation));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_NOT_CONNECTED,
      zlink_spot_request_to_spot (
        source.entry, &stale_node_rid, &target_spot,
        owner.spot_generation, NULL, &stale_request, 1,
        &stale_operation, ZLINK_SEND_FLAGS_NONE, 1000));
    TEST_ASSERT_EQUAL_INT (ENOTCONN, zlink_errno ());
    zlink_msg_close (&stale_request);
    TEST_ASSERT_EQUAL_UINT64 (0, stale_operation.high);
    TEST_ASSERT_EQUAL_UINT64 (0, stale_operation.low);

    //  Leave a remote request unclaimed in the Ready Instance mailbox. A
    //  bounded target shutdown must terminate the source operation exactly
    //  once and must not publish the pending application record again.
    const unsigned char arm_drain = 0xD1;
    TEST_ASSERT_TRUE (write_full (winner_socket[0], &arm_drain, 1));
    unsigned char drain_armed = 0;
    TEST_ASSERT_TRUE (read_full (winner_socket[0], &drain_armed, 1));
    TEST_ASSERT_EQUAL_HEX8 (0xD2, drain_armed);

    zlink_msg_t force_request;
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK,
      zlink_msg_init_size (&force_request, strlen ("force-pending")));
    memcpy (zlink_msg_data (&force_request), "force-pending",
            strlen ("force-pending"));
    zlink_mesh_operation_id_t force_operation;
    memset (&force_operation, 0, sizeof (force_operation));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_OK,
      zlink_spot_request_to_spot (
        source.entry, &owner.node.rid, &target_spot,
        owner.spot_generation, NULL, &force_request, 1,
        &force_operation, ZLINK_SEND_FLAGS_NONE, 0));
    zlink_msg_close (&force_request);
    const unsigned char begin_drain = 0xD3;
    TEST_ASSERT_TRUE (write_full (winner_socket[0], &begin_drain, 1));

    drain_report_t drain_report;
    TEST_ASSERT_TRUE (read_full (winner_socket[0], &drain_report,
                                 sizeof (drain_report)));
    TEST_ASSERT_EQUAL_INT (0, drain_report.validation);
    TEST_ASSERT_TRUE (drain_report.pending_before_shutdown >= 1);
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_TIMED_OUT,
                           drain_report.shutdown_result);
    TEST_ASSERT_EQUAL_INT (ETIMEDOUT, drain_report.shutdown_errno);
    TEST_ASSERT_EQUAL_UINT64 (0, drain_report.ready_after_shutdown);
    TEST_ASSERT_TRUE (source_completion (
      source.node, force_operation, ZLINK_REQUEST_TERMINATED, ESHUTDOWN,
      NULL));
    TEST_ASSERT_FALSE (
      source_has_duplicate_completion (source.node, force_operation));

    const unsigned char stop = 0x5A;
    TEST_ASSERT_TRUE (write_full (loser_socket[0], &stop, 1));
    TEST_ASSERT_TRUE (write_full (winner_socket[0], &stop, 1));
    close (loser_socket[0]);
    close (winner_socket[0]);
    int loser_status = 0;
    int winner_status = 0;
    TEST_ASSERT_EQUAL_INT (loser_pid, waitpid (loser_pid, &loser_status, 0));
    TEST_ASSERT_EQUAL_INT (winner_pid,
                           waitpid (winner_pid, &winner_status, 0));
    TEST_ASSERT_TRUE (WIFEXITED (loser_status));
    TEST_ASSERT_EQUAL_INT (0, WEXITSTATUS (loser_status));
    TEST_ASSERT_TRUE (WIFEXITED (winner_status));
    TEST_ASSERT_EQUAL_INT (0, WEXITSTATUS (winner_status));
    stop_node (&source);
#endif
}

void test_stale_origin_reply_cannot_complete_restarted_generation ()
{
#if defined _WIN32
    TEST_IGNORE_MESSAGE ("fork-based Instance wire contract is POSIX-only");
#else
    int old_control[2];
    int current_control[2];
    TEST_ASSERT_EQUAL_INT (
      0, socketpair (AF_UNIX, SOCK_STREAM, 0, old_control));
    TEST_ASSERT_EQUAL_INT (
      0, socketpair (AF_UNIX, SOCK_STREAM, 0, current_control));
    const pid_t old_pid = fork ();
    TEST_ASSERT_TRUE (old_pid >= 0);
    if (old_pid == 0) {
        close (old_control[0]);
        close (current_control[0]);
        close (current_control[1]);
        const int result = stale_origin_source_process (old_control[1]);
        _exit (result);
    }
    const pid_t current_pid = fork ();
    TEST_ASSERT_TRUE (current_pid >= 0);
    if (current_pid == 0) {
        close (current_control[0]);
        close (old_control[0]);
        close (old_control[1]);
        const int result = stale_origin_source_process (current_control[1]);
        _exit (result);
    }
    close (old_control[1]);
    close (current_control[1]);

    //  Fork every source before Core starts background threads in the target
    //  process. Each child blocks on its control socket until its generation
    //  becomes the active origin.
    node_t target;
    TEST_ASSERT_EQUAL_INT (0, start_node ("wire-target", &target));
    owner_snapshot_t owner;
    void *instance_spot = NULL;
    TEST_ASSERT_TRUE (
      activate_ready_instance (&target, &owner, &instance_spot));

    const uint64_t no_minimum_generation = 0;
    char old_payload[16];
    memset (old_payload, 0, sizeof (old_payload));
    memcpy (old_payload, "origin-old", strlen ("origin-old"));
    TEST_ASSERT_TRUE (write_full (old_control[0], &owner, sizeof (owner)));
    TEST_ASSERT_TRUE (write_full (old_control[0], &no_minimum_generation,
                                  sizeof (no_minimum_generation)));
    TEST_ASSERT_TRUE (write_full (old_control[0], old_payload,
                                  sizeof (old_payload)));
    bootstrap_t old_source;
    TEST_ASSERT_TRUE (read_full (old_control[0], &old_source,
                                 sizeof (old_source)));
    TEST_ASSERT_TRUE (wait_admitted_generation (
      target.node, old_source.rid, old_source.generation));
    const unsigned char submit_after_admission = 0x91;
    TEST_ASSERT_TRUE (write_full (old_control[0], &submit_after_admission, 1));
    stale_origin_source_report_t old_report;
    TEST_ASSERT_TRUE (read_full (old_control[0], &old_report,
                                 sizeof (old_report)));
    TEST_ASSERT_EQUAL_INT (0, old_report.validation);
    zlink_mesh_reply_token_t old_token;
    zlink_mesh_operation_id_t old_received_operation;
    TEST_ASSERT_TRUE (take_request_token (
      target.node, "origin-old", &old_token, &old_received_operation));

    const unsigned char stop_old = 0xA1;
    TEST_ASSERT_TRUE (write_full (old_control[0], &stop_old, 1));
    int child_status = 0;
    TEST_ASSERT_EQUAL_INT (old_pid, waitpid (old_pid, &child_status, 0));
    TEST_ASSERT_TRUE (WIFEXITED (child_status)
                      && WEXITSTATUS (child_status) == 0);
    close (old_control[0]);

    char current_payload[16];
    memset (current_payload, 0, sizeof (current_payload));
    memcpy (current_payload, "origin-new", strlen ("origin-new"));
    TEST_ASSERT_TRUE (write_full (current_control[0], &owner,
                                  sizeof (owner)));
    TEST_ASSERT_TRUE (write_full (current_control[0],
                                  &old_report.source.generation,
                                  sizeof (old_report.source.generation)));
    TEST_ASSERT_TRUE (write_full (current_control[0], current_payload,
                                  sizeof (current_payload)));
    bootstrap_t current_source;
    TEST_ASSERT_TRUE (read_full (current_control[0], &current_source,
                                 sizeof (current_source)));
    TEST_ASSERT_TRUE (wait_admitted_generation (
      target.node, current_source.rid, current_source.generation));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK,
      zlink_instance_spot_renew_owner_admission (instance_spot, 5000));
    TEST_ASSERT_TRUE (
      write_full (current_control[0], &submit_after_admission, 1));
    stale_origin_source_report_t current_report;
    TEST_ASSERT_TRUE (read_full (current_control[0], &current_report,
                                 sizeof (current_report)));
    TEST_ASSERT_EQUAL_INT (0, current_report.validation);
    TEST_ASSERT_TRUE (current_report.source.generation
                      > old_report.source.generation);
    zlink_mesh_reply_token_t current_token;
    zlink_mesh_operation_id_t current_received_operation;
    TEST_ASSERT_TRUE (take_request_token (
      target.node, "origin-new", &current_token, &current_received_operation));
    TEST_ASSERT_EQUAL_UINT64 (old_report.operation.low,
                              current_report.operation.low);

    zlink_msg_t stale_reply;
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK,
      zlink_msg_init_size (&stale_reply, strlen ("stale-reply")));
    memcpy (zlink_msg_data (&stale_reply), "stale-reply",
            strlen ("stale-reply"));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_OK,
      zlink_mesh_reply (&old_token, &stale_reply, 1, ZLINK_SEND_FLAGS_NONE));
    zlink_msg_close (&stale_reply);

    const unsigned char check_stale = 0xB1;
    TEST_ASSERT_TRUE (write_full (current_control[0], &check_stale, 1));
    unsigned char checked = 0;
    TEST_ASSERT_TRUE (read_full (current_control[0], &checked, 1));
    TEST_ASSERT_EQUAL_HEX8 (0xB2, checked);

    zlink_msg_t current_reply;
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK,
      zlink_msg_init_size (&current_reply, strlen ("current-reply")));
    memcpy (zlink_msg_data (&current_reply), "current-reply",
            strlen ("current-reply"));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_OK,
      zlink_mesh_reply (
        &current_token, &current_reply, 1, ZLINK_SEND_FLAGS_NONE));
    zlink_msg_close (&current_reply);
    const unsigned char receive_current = 0xB3;
    TEST_ASSERT_TRUE (
      write_full (current_control[0], &receive_current, 1));
    stale_origin_completion_report_t completion_report;
    TEST_ASSERT_TRUE (read_full (current_control[0], &completion_report,
                                 sizeof (completion_report)));
    TEST_ASSERT_EQUAL_INT (
      1, completion_report.no_completion_before_current_reply);
    TEST_ASSERT_EQUAL_INT (1, completion_report.current_completion);
    TEST_ASSERT_EQUAL_INT (0, completion_report.duplicate_completion);
    child_status = 0;
    TEST_ASSERT_EQUAL_INT (
      current_pid, waitpid (current_pid, &child_status, 0));
    TEST_ASSERT_TRUE (WIFEXITED (child_status)
                      && WEXITSTATUS (child_status) == 0);
    close (current_control[0]);
    stop_node (&target);
#endif
}

void test_direct_instance_send_rejects_replaced_presend_connection ()
{
#if defined _WIN32
    TEST_IGNORE_MESSAGE ("Instance wire pause hook is POSIX-only");
#else
    int control[2];
    TEST_ASSERT_EQUAL_INT (
      0, socketpair (AF_UNIX, SOCK_STREAM, 0, control));
    const pid_t target_pid = fork ();
    TEST_ASSERT_TRUE (target_pid >= 0);
    if (target_pid == 0) {
        close (control[0]);
        const int result = presend_direct_target_process (control[1]);
        _exit (result);
    }
    close (control[1]);

    //  Each MeshNode uses a separate process because a process admits one
    //  node for a MeshName. Fork before the source starts Core threads.
    node_t source;
    TEST_ASSERT_EQUAL_INT (0, start_node ("wire-presend-direct-source", &source));

    owner_snapshot_t owner;
    TEST_ASSERT_TRUE (read_full (control[0], &owner, sizeof (owner)));
    TEST_ASSERT_TRUE (connect_endpoint (source.node, owner.node.endpoint));
    TEST_ASSERT_TRUE (wait_admitted_generation (
      source.node, owner.node.rid, owner.node.generation));

    const zlink_routing_id_t direct_target = instance_spot_rid ();
    zlink_msg_t request;
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK,
      zlink_msg_init_size (&request, strlen ("direct-presend")));
    memcpy (zlink_msg_data (&request), "direct-presend",
            strlen ("direct-presend"));
    zlink_mesh_operation_id_t rejected_operation;
    memset (&rejected_operation, 0, sizeof (rejected_operation));
    std::atomic<int> submit_result (-1);
    std::atomic<int> submit_errno (0);

    zlink_test_mesh_pause_instance_wire_before_send (1);
    std::thread submitter ([&] {
        submit_result.store (
          zlink_spot_request_to_spot (
            source.entry, &owner.node.rid, &direct_target,
            owner.spot_generation, NULL, &request, 1,
            &rejected_operation, ZLINK_SEND_FLAGS_NONE, 0),
          std::memory_order_release);
        submit_errno.store (zlink_errno (), std::memory_order_release);
    });

    const bool paused = wait_instance_wire_before_send_paused ();
    zlink_connect_result_t disconnect_result = ZLINK_CONNECT_INTERNAL_ERROR;
    bool replacement_admitted = false;
    if (paused) {
        disconnect_result = zlink_mesh_node_disconnect_peer (
          source.node, &owner.node.rid, owner.node.generation);
        replacement_admitted =
          disconnect_result == ZLINK_CONNECT_OK
          && connect_endpoint (source.node, owner.node.endpoint)
          && wait_admitted_generation (
            source.node, owner.node.rid, owner.node.generation);
    }
    zlink_test_mesh_pause_instance_wire_before_send (0);
    submitter.join ();

    TEST_ASSERT_TRUE (paused);
    TEST_ASSERT_EQUAL_INT (ZLINK_CONNECT_OK, disconnect_result);
    TEST_ASSERT_TRUE (replacement_admitted);
    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_NOT_CONNECTED,
      submit_result.load (std::memory_order_acquire));
    TEST_ASSERT_TRUE (
      submit_errno.load (std::memory_order_acquire) == EHOSTUNREACH
      || submit_errno.load (std::memory_order_acquire) == ENOTCONN);
    TEST_ASSERT_EQUAL_UINT64 (0, rejected_operation.high);
    TEST_ASSERT_EQUAL_UINT64 (0, rejected_operation.low);
    const unsigned char check_ready = 0xD1;
    TEST_ASSERT_TRUE (write_full (control[0], &check_ready, 1));
    size_t ready_count = 1;
    TEST_ASSERT_TRUE (
      read_full (control[0], &ready_count, sizeof (ready_count)));
    TEST_ASSERT_EQUAL_UINT64 (0, ready_count);

    zlink_mesh_operation_id_t retry_operation;
    memset (&retry_operation, 0, sizeof (retry_operation));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_OK,
      zlink_spot_request_to_spot (
        source.entry, &owner.node.rid, &direct_target,
        owner.spot_generation, NULL, &request, 1, &retry_operation,
        ZLINK_SEND_FLAGS_NONE, 5000));
    zlink_msg_close (&request);
    const unsigned char receive_retry = 0xD2;
    TEST_ASSERT_TRUE (write_full (control[0], &receive_retry, 1));
    winner_report_t reply;
    TEST_ASSERT_TRUE (read_full (control[0], &reply, sizeof (reply)));
    TEST_ASSERT_EQUAL_INT (0, reply.validation);
    TEST_ASSERT_TRUE (source_completion (
      source.node, retry_operation, ZLINK_REQUEST_OK, 0,
      "direct-presend-ok"));
    TEST_ASSERT_FALSE (
      source_has_duplicate_completion (source.node, retry_operation));

    const unsigned char stop = 0x5A;
    TEST_ASSERT_TRUE (write_full (control[0], &stop, 1));
    close (control[0]);
    int child_status = 0;
    TEST_ASSERT_EQUAL_INT (
      target_pid, waitpid (target_pid, &child_status, 0));
    TEST_ASSERT_TRUE (WIFEXITED (child_status));
    TEST_ASSERT_EQUAL_INT (0, WEXITSTATUS (child_status));
    stop_node (&source);
#endif
}

void test_redirect_instance_rejects_replaced_presend_connection ()
{
#if defined _WIN32
    TEST_IGNORE_MESSAGE ("Instance wire pause hook is POSIX-only");
#else
    int source_control[2];
    int winner_control[2];
    TEST_ASSERT_EQUAL_INT (
      0, socketpair (AF_UNIX, SOCK_STREAM, 0, source_control));
    TEST_ASSERT_EQUAL_INT (
      0, socketpair (AF_UNIX, SOCK_STREAM, 0, winner_control));

    const pid_t source_pid = fork ();
    TEST_ASSERT_TRUE (source_pid >= 0);
    if (source_pid == 0) {
        close (source_control[0]);
        close (winner_control[0]);
        close (winner_control[1]);
        const int result =
          presend_redirect_source_process (source_control[1]);
        _exit (result);
    }
    const pid_t winner_pid = fork ();
    TEST_ASSERT_TRUE (winner_pid >= 0);
    if (winner_pid == 0) {
        close (winner_control[0]);
        close (source_control[0]);
        close (source_control[1]);
        const int result =
          presend_redirect_winner_process (winner_control[1]);
        _exit (result);
    }
    close (source_control[1]);
    close (winner_control[1]);

    //  Source, redirector, and winner each run in a distinct process so the
    //  test respects the one-node-per-MeshName process invariant.
    node_t redirector;
    TEST_ASSERT_EQUAL_INT (
      0, start_node ("wire-presend-redirector", &redirector, 20000));

    bootstrap_t source;
    owner_snapshot_t owner;
    TEST_ASSERT_TRUE (
      read_full (source_control[0], &source, sizeof (source)));
    TEST_ASSERT_TRUE (
      read_full (winner_control[0], &owner, sizeof (owner)));
    const bootstrap_t redirector_node = bootstrap (redirector);
    TEST_ASSERT_TRUE (
      connect_endpoint (redirector.node, owner.node.endpoint));
    TEST_ASSERT_TRUE (wait_admitted_generation (
      redirector.node, owner.node.rid, owner.node.generation));
    TEST_ASSERT_TRUE (write_full (source_control[0], &redirector_node,
                                  sizeof (redirector_node)));

    presend_source_report_t source_report;
    TEST_ASSERT_TRUE (read_full (source_control[0], &source_report,
                                 sizeof (source_report)));
    TEST_ASSERT_EQUAL_INT (0, source_report.validation);

    zlink_instance_spot_activation_data_t activation;
    TEST_ASSERT_TRUE (take_activation (redirector.node, &activation));
    std::atomic<int> redirect_result (-1);
    std::atomic<int> redirect_errno (0);
    zlink_test_mesh_pause_instance_wire_before_send (1);
    std::thread redirect_thread ([&] {
        redirect_result.store (
          zlink_instance_spot_activation_redirect (
            &activation.token, &owner.node.rid, &activation.spot_rid,
            owner.spot_generation),
          std::memory_order_release);
        redirect_errno.store (zlink_errno (), std::memory_order_release);
    });

    const bool paused = wait_instance_wire_before_send_paused ();
    zlink_connect_result_t disconnect_result = ZLINK_CONNECT_INTERNAL_ERROR;
    bool replacement_admitted = false;
    if (paused) {
        disconnect_result = zlink_mesh_node_disconnect_peer (
          redirector.node, &owner.node.rid, owner.node.generation);
        replacement_admitted =
          disconnect_result == ZLINK_CONNECT_OK
          && connect_endpoint (redirector.node, owner.node.endpoint)
          && wait_admitted_generation (
            redirector.node, owner.node.rid, owner.node.generation);
    }
    zlink_test_mesh_pause_instance_wire_before_send (0);
    redirect_thread.join ();

    TEST_ASSERT_TRUE (paused);
    TEST_ASSERT_EQUAL_INT (ZLINK_CONNECT_OK, disconnect_result);
    TEST_ASSERT_TRUE (replacement_admitted);
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_INVALID_STATE,
      redirect_result.load (std::memory_order_acquire));
    TEST_ASSERT_TRUE (
      redirect_errno.load (std::memory_order_acquire) == EHOSTUNREACH
      || redirect_errno.load (std::memory_order_acquire) == ENOTCONN);
    const unsigned char check_ready = 0xB1;
    TEST_ASSERT_TRUE (write_full (winner_control[0], &check_ready, 1));
    size_t ready_count = 1;
    TEST_ASSERT_TRUE (
      read_full (winner_control[0], &ready_count, sizeof (ready_count)));
    TEST_ASSERT_EQUAL_UINT64 (0, ready_count);

    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK,
      zlink_instance_spot_activation_redirect (
        &activation.token, &owner.node.rid, &activation.spot_rid,
        owner.spot_generation));
    const unsigned char receive_redirect = 0xB2;
    TEST_ASSERT_TRUE (
      write_full (winner_control[0], &receive_redirect, 1));
    winner_report_t reply;
    TEST_ASSERT_TRUE (
      read_full (winner_control[0], &reply, sizeof (reply)));
    TEST_ASSERT_EQUAL_INT (0, reply.validation);
    const unsigned char receive_completion = 0xA3;
    TEST_ASSERT_TRUE (
      write_full (source_control[0], &receive_completion, 1));
    stale_origin_completion_report_t completion;
    TEST_ASSERT_TRUE (read_full (source_control[0], &completion,
                                 sizeof (completion)));
    TEST_ASSERT_EQUAL_INT (1, completion.current_completion);
    TEST_ASSERT_EQUAL_INT (0, completion.duplicate_completion);

    const unsigned char stop = 0x5A;
    TEST_ASSERT_TRUE (write_full (winner_control[0], &stop, 1));
    close (source_control[0]);
    close (winner_control[0]);
    int source_status = 0;
    int winner_status = 0;
    TEST_ASSERT_EQUAL_INT (
      source_pid, waitpid (source_pid, &source_status, 0));
    TEST_ASSERT_EQUAL_INT (
      winner_pid, waitpid (winner_pid, &winner_status, 0));
    TEST_ASSERT_TRUE (WIFEXITED (source_status));
    TEST_ASSERT_EQUAL_INT (0, WEXITSTATUS (source_status));
    TEST_ASSERT_TRUE (WIFEXITED (winner_status));
    TEST_ASSERT_EQUAL_INT (0, WEXITSTATUS (winner_status));
    stop_node (&redirector);
#endif
}

void test_forced_remote_terminal_survives_direct_and_deferred_oom ()
{
#if defined _WIN32
    TEST_IGNORE_MESSAGE ("fork-based Instance wire contract is POSIX-only");
#else
    int control[2];
    TEST_ASSERT_EQUAL_INT (
      0, socketpair (AF_UNIX, SOCK_STREAM, 0, control));
    const pid_t source_pid = fork ();
    TEST_ASSERT_TRUE (source_pid >= 0);
    if (source_pid == 0) {
        close (control[0]);
        const int result = stale_origin_source_process (control[1]);
        _exit (result);
    }
    close (control[1]);

    //  Start Core threads only after fork. The child supplies one remote
    //  request whose reply route remains available for deterministic retries.
    node_t target;
    TEST_ASSERT_EQUAL_INT (0, start_node ("wire-force-target", &target));
    owner_snapshot_t owner;
    void *instance_spot = NULL;
    TEST_ASSERT_TRUE (
      activate_ready_instance (&target, &owner, &instance_spot));

    const uint64_t no_minimum_generation = 0;
    char payload[16];
    memset (payload, 0, sizeof (payload));
    memcpy (payload, "force-oom", strlen ("force-oom"));
    TEST_ASSERT_TRUE (write_full (control[0], &owner, sizeof (owner)));
    TEST_ASSERT_TRUE (write_full (control[0], &no_minimum_generation,
                                  sizeof (no_minimum_generation)));
    TEST_ASSERT_TRUE (write_full (control[0], payload, sizeof (payload)));
    bootstrap_t source;
    TEST_ASSERT_TRUE (read_full (control[0], &source, sizeof (source)));
    TEST_ASSERT_TRUE (wait_admitted_generation (
      target.node, source.rid, source.generation));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK,
      zlink_instance_spot_renew_owner_admission (instance_spot, 5000));
    const unsigned char submit_after_admission = 0x91;
    TEST_ASSERT_TRUE (
      write_full (control[0], &submit_after_admission, 1));
    stale_origin_source_report_t report;
    TEST_ASSERT_TRUE (read_full (control[0], &report, sizeof (report)));
    TEST_ASSERT_EQUAL_INT (0, report.validation);

    zlink_mesh_reply_token_t token;
    zlink_mesh_operation_id_t received_operation;
    TEST_ASSERT_TRUE (take_request_token (
      target.node, "force-oom", &token, &received_operation));

    //  A direct forced terminal that cannot allocate its wire envelope keeps
    //  the route retryable and records the terminal outcome durably.
    zlink_test_set_mesh_force_reply_wire_alloc_fault (1);
    errno = 0;
    TEST_ASSERT_EQUAL_INT (
      -1, zlink_test_mesh_force_reply_token (
            &token, ZLINK_REQUEST_NOT_CONNECTED, ENOTCONN));
    TEST_ASSERT_EQUAL_INT (ENOMEM, zlink_errno ());
    int in_flight = -1;
    int force_pending = -1;
    int consumed = -1;
    TEST_ASSERT_EQUAL_INT (
      0, zlink_test_mesh_reply_route_state (
           &token, &in_flight, &force_pending, &consumed));
    TEST_ASSERT_EQUAL_INT (0, in_flight);
    TEST_ASSERT_EQUAL_INT (1, force_pending);
    TEST_ASSERT_EQUAL_INT (0, consumed);

    //  The deferred path uses the same recovery rule. Its internal attempt is
    //  allowed to fail, but the pending terminal and route ownership remain.
    zlink_test_set_mesh_force_reply_wire_alloc_fault (1);
    TEST_ASSERT_EQUAL_INT (
      0, zlink_test_mesh_deferred_force_reply_token (
           &token, ZLINK_REQUEST_NOT_CONNECTED, ENOTCONN));
    TEST_ASSERT_EQUAL_INT (
      0, zlink_test_mesh_reply_route_state (
           &token, &in_flight, &force_pending, &consumed));
    TEST_ASSERT_EQUAL_INT (0, in_flight);
    TEST_ASSERT_EQUAL_INT (1, force_pending);
    TEST_ASSERT_EQUAL_INT (0, consumed);

    zlink_test_set_mesh_force_reply_wire_alloc_fault (0);
    TEST_ASSERT_EQUAL_INT (
      0, zlink_test_mesh_force_reply_token (
           &token, ZLINK_REQUEST_NOT_CONNECTED, ENOTCONN));
    TEST_ASSERT_EQUAL_INT (
      0, zlink_test_mesh_reply_route_state (
           &token, &in_flight, &force_pending, &consumed));
    TEST_ASSERT_EQUAL_INT (0, in_flight);
    TEST_ASSERT_EQUAL_INT (0, force_pending);
    TEST_ASSERT_EQUAL_INT (1, consumed);

    const unsigned char receive_forced = 0xC1;
    TEST_ASSERT_TRUE (write_full (control[0], &receive_forced, 1));
    stale_origin_completion_report_t completion;
    TEST_ASSERT_TRUE (
      read_full (control[0], &completion, sizeof (completion)));
    TEST_ASSERT_EQUAL_INT (1, completion.current_completion);
    TEST_ASSERT_EQUAL_INT (0, completion.duplicate_completion);

    int child_status = 0;
    TEST_ASSERT_EQUAL_INT (
      source_pid, waitpid (source_pid, &child_status, 0));
    TEST_ASSERT_TRUE (WIFEXITED (child_status)
                      && WEXITSTATUS (child_status) == 0);
    close (control[0]);
    stop_node (&target);
#endif
}

int main ()
{
    setup_test_environment ();
    UNITY_BEGIN ();
    RUN_TEST (test_remote_placement_and_redirect_preserve_request);
    RUN_TEST (test_stale_origin_reply_cannot_complete_restarted_generation);
    RUN_TEST (test_direct_instance_send_rejects_replaced_presend_connection);
    RUN_TEST (test_redirect_instance_rejects_replaced_presend_connection);
    RUN_TEST (test_forced_remote_terminal_survives_direct_and_deferred_oom);
    return UNITY_END ();
}
