/* SPDX-License-Identifier: MPL-2.0 */

#include "../testutil.hpp"
#include "../testutil_unity.hpp"

#include <zlink/service/dispatch.h>
#include <zlink/service/mesh_node.h>
#include <zlink/service/spot.h>
#include <zlink/service/instance_spot_driver.h>

extern "C" void zlink_test_set_mesh_alloc_fault (int count_);
extern "C" int zlink_test_mesh_set_operation_committing (
  void *node_, const zlink_mesh_operation_id_t *operation_id_, int enabled_);

#include <stddef.h>
#include <string.h>

#include <atomic>
#include <thread>

#if !defined _WIN32
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

void setUp ()
{
}

void tearDown ()
{
}

namespace
{
//  Mirrors the frozen Spot status v1 prefix. The public v2 structure may
//  append fields, but it must not move or reinterpret this prefix.
struct spot_status_v1_prefix_t
{
    uint32_t struct_size;
    uint32_t version;
    zlink_routing_id_t spot_rid;
    zlink_spot_kind_t spot_kind;
    uint64_t lifecycle_generation;
    uint64_t pending_application_messages;
    uint64_t pending_infrastructure_messages;
    uint64_t pending_bytes;
    uint32_t active_actor_count;
    uint32_t draining;
    int32_t last_error;
    uint64_t last_changed_ms;
};

struct status_v1_canary_t
{
    spot_status_v1_prefix_t prefix;
    uint64_t canary[2];
};

struct instance_fixture_t
{
    void *ctx;
    void *node;
    void *entry;
    zlink_mesh_node_status_t node_status;
};

void make_rid (zlink_routing_id_t *rid_, const char *value_)
{
    memset (rid_, 0, sizeof (*rid_));
    rid_->size = static_cast<uint8_t> (strlen (value_));
    memcpy (rid_->data, value_, rid_->size);
}

void make_payload (zlink_msg_t *part_, const char *value_)
{
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK,
                           zlink_msg_init_size (part_, strlen (value_)));
    memcpy (zlink_msg_data (part_), value_, strlen (value_));
}

instance_fixture_t new_fixture (const char *mesh_name_,
                                uint64_t message_budget_ = 0,
                                uint64_t byte_budget_ = 0,
                                uint64_t watchdog_ms_ = 0)
{
    instance_fixture_t fixture;
    memset (&fixture, 0, sizeof (fixture));
    fixture.ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (fixture.ctx);

    zlink_mesh_node_options_t options;
    memset (&options, 0, sizeof (options));
    options.struct_size = sizeof (options);
    options.version = ZLINK_MESH_NODE_ABI_VERSION;
    options.mesh_name = mesh_name_;
    options.mesh_name_size = strlen (mesh_name_);
    fixture.node = zlink_mesh_node_new (fixture.ctx, &options);
    TEST_ASSERT_NOT_NULL (fixture.node);
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK,
      zlink_set_routing_id (fixture.node, mesh_name_, strlen (mesh_name_)));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK,
      zlink_mesh_node_set_bind (fixture.node, "tcp://127.0.0.1:0"));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK,
      zlink_mesh_node_add_channel_name (fixture.node, "instance-contract"));

    if (message_budget_ != 0)
        TEST_ASSERT_EQUAL_INT (
          ZLINK_CONFIG_OK,
          zlink_set_mesh_node_option (
            fixture.node,
            ZLINK_MESH_NODE_OPT_INSTANCE_ACTIVATION_MESSAGE_BUDGET,
            &message_budget_, sizeof (message_budget_)));
    if (byte_budget_ != 0)
        TEST_ASSERT_EQUAL_INT (
          ZLINK_CONFIG_OK,
          zlink_set_mesh_node_option (
            fixture.node,
            ZLINK_MESH_NODE_OPT_INSTANCE_ACTIVATION_BYTE_BUDGET,
            &byte_budget_, sizeof (byte_budget_)));
    if (watchdog_ms_ != 0)
        TEST_ASSERT_EQUAL_INT (
          ZLINK_CONFIG_OK,
          zlink_set_mesh_node_option (
            fixture.node, ZLINK_MESH_NODE_OPT_INSTANCE_ACTIVATION_TIMEOUT_MS,
            &watchdog_ms_, sizeof (watchdog_ms_)));

    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK,
                           zlink_mesh_node_start (fixture.node));
    memset (&fixture.node_status, 0, sizeof (fixture.node_status));
    fixture.node_status.struct_size = sizeof (fixture.node_status);
    fixture.node_status.version = ZLINK_MESH_NODE_ABI_VERSION;
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK,
      zlink_mesh_node_status (fixture.node, &fixture.node_status));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK,
      zlink_mesh_node_entry_spot (fixture.node, &fixture.entry));
    TEST_ASSERT_NOT_NULL (fixture.entry);
    return fixture;
}

void destroy_fixture (instance_fixture_t *fixture_)
{
    if (fixture_->entry != NULL)
        TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK,
                               zlink_spot_destroy (&fixture_->entry));
    if (fixture_->node != NULL) {
        const zlink_request_result_t shutdown_result =
          zlink_mesh_node_shutdown (fixture_->node, 1000);
        TEST_ASSERT_TRUE (shutdown_result == ZLINK_REQUEST_OK
                          || shutdown_result == ZLINK_REQUEST_INVALID_STATE);
        TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK,
                               zlink_mesh_node_destroy (&fixture_->node));
    }
    if (fixture_->ctx != NULL)
        TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK,
                               zlink_ctx_term (fixture_->ctx));
}

zlink_instance_spot_placement_t placement_target (
  const instance_fixture_t &fixture_,
  const char *spot_rid_,
  const char *instance_type_,
  const char *contract_id_)
{
    zlink_instance_spot_placement_t target;
    memset (&target, 0, sizeof (target));
    target.node_rid = fixture_.node_status.routing_id;
    target.node_generation = fixture_.node_status.lifecycle_generation;
    make_rid (&target.spot_rid, spot_rid_);
    target.instance_spot_type = instance_type_;
    target.instance_spot_type_size = strlen (instance_type_);
    target.message_contract_id = contract_id_;
    target.message_contract_id_size = strlen (contract_id_);
    return target;
}

struct direct_target_t
{
    zlink_routing_id_t node_rid;
    zlink_routing_id_t spot_rid;
    uint64_t spot_generation;
};

direct_target_t direct_target (
  const zlink_instance_spot_placement_t &placement_,
  uint64_t spot_generation_)
{
    direct_target_t target;
    target.node_rid = placement_.node_rid;
    target.spot_rid = placement_.spot_rid;
    target.spot_generation = spot_generation_;
    return target;
}

zlink_submit_result_t request_to_ready_instance (
  void *spot_,
  const direct_target_t *target_,
  const zlink_mesh_metadata_view_t *metadata_,
  const zlink_msg_t *parts_,
  size_t part_count_,
  zlink_mesh_operation_id_t *operation_id_out_,
  zlink_send_flags_t flags_,
  uint32_t timeout_ms_)
{
    return zlink_spot_request_to_spot (
      spot_, &target_->node_rid, &target_->spot_rid,
      target_->spot_generation, metadata_, parts_, part_count_,
      operation_id_out_, flags_, timeout_ms_);
}

bool try_take_claim (void *node_,
                     zlink_mesh_owner_kind_t owner_kind_,
                     zlink_mesh_ready_domain_mask_t domain_,
                     zlink_mesh_claim_t *claim_out_)
{
    void *ready_batch = zlink_mesh_ready_batch_new (8);
    TEST_ASSERT_NOT_NULL (ready_batch);
    uint32_t residue = 0;
    const zlink_recv_result_t result = zlink_mesh_node_drain_ready (
      node_, domain_, ready_batch, &residue, ZLINK_RECV_FLAGS_DONTWAIT);
    bool found = false;
    if (result == ZLINK_RECV_OK) {
        const zlink_mesh_ready_record_t *records =
          zlink_mesh_ready_batch_data (ready_batch);
        for (size_t i = 0; i < zlink_mesh_ready_batch_count (ready_batch); ++i) {
            if (records[i].owner_kind == owner_kind_
                && records[i].domain == domain_) {
                TEST_ASSERT_EQUAL_INT (
                  ZLINK_CONFIG_OK,
                  zlink_mesh_ready_batch_take_claim (ready_batch, i,
                                                      claim_out_));
                found = true;
                break;
            }
        }
    } else {
        TEST_ASSERT_EQUAL_INT (ZLINK_RECV_NO_DATA, result);
    }
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK,
                           zlink_mesh_ready_batch_destroy (&ready_batch));
    return found;
}

void take_claim (void *node_,
                 zlink_mesh_owner_kind_t owner_kind_,
                 zlink_mesh_ready_domain_mask_t domain_,
                 zlink_mesh_claim_t *claim_out_)
{
    bool found = false;
    for (int attempt = 0; attempt < 400 && !found; ++attempt) {
        found = try_take_claim (node_, owner_kind_, domain_, claim_out_);
        if (!found)
            msleep (5);
    }
    TEST_ASSERT_TRUE_MESSAGE (found, "expected ready claim did not arrive");
}

zlink_instance_spot_activation_data_t take_activation (void *node_)
{
    zlink_mesh_claim_t claim;
    memset (&claim, 0, sizeof (claim));
    take_claim (node_, ZLINK_MESH_OWNER_NODE,
                ZLINK_MESH_READY_INFRASTRUCTURE, &claim);
    void *batch = zlink_mesh_receive_batch_new (1, 1, 4096);
    TEST_ASSERT_NOT_NULL (batch);
    zlink_mesh_receive_requirements_t required;
    memset (&required, 0, sizeof (required));
    required.struct_size = sizeof (required);
    required.version = 1;
    TEST_ASSERT_EQUAL_INT (
      ZLINK_RECV_OK,
      zlink_mesh_claim_recv_batch (&claim, batch, &required,
                                   ZLINK_RECV_FLAGS_NONE));
    TEST_ASSERT_EQUAL_UINT64 (1, zlink_mesh_receive_batch_count (batch));
    const zlink_mesh_receive_record_t *record =
      zlink_mesh_receive_batch_data (batch);
    TEST_ASSERT_EQUAL_INT (ZLINK_MESH_RECORD_INSTANCE_SPOT_ACTIVATION,
                           record->kind);
    TEST_ASSERT_EQUAL_UINT64 (sizeof (zlink_instance_spot_activation_data_t),
                              record->kind_data_size);
    zlink_instance_spot_activation_data_t data =
      *static_cast<const zlink_instance_spot_activation_data_t *> (
        record->kind_data);
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_claim_release (&claim));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK,
                           zlink_mesh_receive_batch_destroy (&batch));
    return data;
}

void assert_no_application_claim (void *node_)
{
    zlink_mesh_claim_t claim;
    memset (&claim, 0, sizeof (claim));
    TEST_ASSERT_FALSE (try_take_claim (node_, ZLINK_MESH_OWNER_SPOT,
                                       ZLINK_MESH_READY_APPLICATION, &claim));
}

zlink_mesh_receive_record_t take_completion (void *node_,
                                             zlink_mesh_operation_id_t operation_)
{
    for (int attempt = 0; attempt < 400; ++attempt) {
        zlink_mesh_claim_t claim;
        memset (&claim, 0, sizeof (claim));
        if (!try_take_claim (node_, ZLINK_MESH_OWNER_SPOT,
                             ZLINK_MESH_READY_INFRASTRUCTURE, &claim)) {
            msleep (5);
            continue;
        }
        void *batch = zlink_mesh_receive_batch_new (1, 1, 64);
        TEST_ASSERT_NOT_NULL (batch);
        zlink_mesh_receive_requirements_t required;
        memset (&required, 0, sizeof (required));
        required.struct_size = sizeof (required);
        required.version = 1;
        TEST_ASSERT_EQUAL_INT (
          ZLINK_RECV_OK,
          zlink_mesh_claim_recv_batch (&claim, batch, &required,
                                       ZLINK_RECV_FLAGS_NONE));
        const zlink_mesh_receive_record_t record =
          zlink_mesh_receive_batch_data (batch)[0];
        TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK,
                               zlink_mesh_claim_release (&claim));
        TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK,
                               zlink_mesh_receive_batch_destroy (&batch));
        if (record.kind == ZLINK_MESH_RECORD_COMPLETION
            && record.operation_id.high == operation_.high
            && record.operation_id.low == operation_.low)
            return record;
    }
    TEST_FAIL_MESSAGE ("expected operation completion did not arrive");
    zlink_mesh_receive_record_t unreachable;
    memset (&unreachable, 0, sizeof (unreachable));
    return unreachable;
}

void assert_no_duplicate_completion (void *node_,
                                     zlink_mesh_operation_id_t operation_)
{
    for (int attempt = 0; attempt < 20; ++attempt) {
        zlink_mesh_claim_t claim;
        memset (&claim, 0, sizeof (claim));
        if (!try_take_claim (node_, ZLINK_MESH_OWNER_SPOT,
                             ZLINK_MESH_READY_INFRASTRUCTURE, &claim)) {
            msleep (5);
            continue;
        }
        void *batch = zlink_mesh_receive_batch_new (1, 1, 64);
        TEST_ASSERT_NOT_NULL (batch);
        zlink_mesh_receive_requirements_t required;
        memset (&required, 0, sizeof (required));
        required.struct_size = sizeof (required);
        required.version = 1;
        TEST_ASSERT_EQUAL_INT (
          ZLINK_RECV_OK,
          zlink_mesh_claim_recv_batch (&claim, batch, &required,
                                       ZLINK_RECV_FLAGS_NONE));
        const zlink_mesh_receive_record_t *record =
          zlink_mesh_receive_batch_data (batch);
        TEST_ASSERT_FALSE_MESSAGE (
          record[0].kind == ZLINK_MESH_RECORD_COMPLETION
            && record[0].operation_id.high == operation_.high
            && record[0].operation_id.low == operation_.low,
          "operation completed more than once");
        TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK,
                               zlink_mesh_claim_release (&claim));
        TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK,
                               zlink_mesh_receive_batch_destroy (&batch));
    }
}

void counting_timer_handler (void *, uint64_t fire_count_, void *userdata_)
{
    std::atomic<uint64_t> *count =
      static_cast<std::atomic<uint64_t> *> (userdata_);
    count->fetch_add (fire_count_, std::memory_order_relaxed);
}

struct ready_instance_t
{
    direct_target_t direct_route;
    void *spot;
    uint64_t generation;
};

ready_instance_t activate_one_send (instance_fixture_t *fixture_,
                                    const char *spot_rid_,
                                    const char *instance_type_,
                                    const char *owner_id_,
                                    uint32_t owner_lease_ms_)
{
    zlink_instance_spot_placement_t placement = placement_target (
      *fixture_, spot_rid_, instance_type_, "ActivateCommand");
    zlink_msg_t part;
    make_payload (&part, "activate");
    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_OK,
      zlink_spot_send_to_instance_placement (
        fixture_->entry, &placement, NULL, &part, 1,
        ZLINK_SEND_FLAGS_NONE));
    zlink_msg_close (&part);
    zlink_instance_spot_activation_data_t activation =
      take_activation (fixture_->node);
    ready_instance_t result;
    memset (&result, 0, sizeof (result));
    zlink_instance_spot_claim_result_t claim_result;
    memset (&claim_result, 0, sizeof (claim_result));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK,
      zlink_instance_spot_activation_claim_owner (
        &activation.token, owner_id_, strlen (owner_id_), &claim_result));
    TEST_ASSERT_EQUAL_INT (ZLINK_INSTANCE_SPOT_CLAIM_LEADER,
                           claim_result.role);
    result.spot = claim_result.leader_spot;
    result.generation = claim_result.leader_spot_generation;
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK,
      zlink_instance_spot_activation_mark_ready (&activation.token,
                                                owner_lease_ms_));
    result.direct_route = direct_target (placement, result.generation);

    //  Drain the cold first send so later fencing assertions observe only
    //  operations submitted after activation became Ready.
    zlink_mesh_claim_t claim;
    memset (&claim, 0, sizeof (claim));
    take_claim (fixture_->node, ZLINK_MESH_OWNER_SPOT,
                ZLINK_MESH_READY_APPLICATION, &claim);
    void *batch = zlink_mesh_receive_batch_new (1, 1, 32);
    TEST_ASSERT_NOT_NULL (batch);
    zlink_mesh_receive_requirements_t required;
    memset (&required, 0, sizeof (required));
    required.struct_size = sizeof (required);
    required.version = 1;
    TEST_ASSERT_EQUAL_INT (
      ZLINK_RECV_OK,
      zlink_mesh_claim_recv_batch (&claim, batch, &required,
                                   ZLINK_RECV_FLAGS_NONE));
    TEST_ASSERT_EQUAL_UINT64 (1, zlink_mesh_receive_batch_count (batch));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_MESH_RECORD_SPOT_SEND,
      zlink_mesh_receive_batch_data (batch)[0].kind);
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_claim_release (&claim));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK,
                           zlink_mesh_receive_batch_destroy (&batch));
    return result;
}
}

void test_instance_spot_public_constants_are_frozen ()
{
    TEST_ASSERT_EQUAL_UINT32 (2, ZLINK_SPOT_ABI_VERSION);
    TEST_ASSERT_EQUAL_UINT32 (2, ZLINK_MESH_DISPATCH_ABI_VERSION);
    TEST_ASSERT_EQUAL_UINT32 (255, ZLINK_INSTANCE_SPOT_TYPE_MAX);
    TEST_ASSERT_EQUAL_UINT32 (255, ZLINK_INSTANCE_SPOT_OWNER_ID_MAX);
    TEST_ASSERT_EQUAL_UINT32 (255, ZLINK_INSTANCE_SPOT_CONTRACT_ID_MAX);

    TEST_ASSERT_EQUAL_INT (3, ZLINK_SPOT_KIND_INSTANCE);
    TEST_ASSERT_EQUAL_INT (1, ZLINK_SPOT_ACTIVATION_ACTIVATING);
    TEST_ASSERT_EQUAL_INT (2, ZLINK_SPOT_ACTIVATION_READY);
    TEST_ASSERT_EQUAL_INT (3, ZLINK_SPOT_ACTIVATION_CLOSING);
    TEST_ASSERT_EQUAL_INT (1, ZLINK_INSTANCE_SPOT_CLAIM_LEADER);
    TEST_ASSERT_EQUAL_INT (2, ZLINK_INSTANCE_SPOT_CLAIM_FOLLOWER);
    TEST_ASSERT_EQUAL_INT (1, ZLINK_INSTANCE_SPOT_OPERATION_SEND);
    TEST_ASSERT_EQUAL_INT (2, ZLINK_INSTANCE_SPOT_OPERATION_REQUEST);
    TEST_ASSERT_EQUAL_INT (14, ZLINK_MESH_RECORD_INSTANCE_SPOT_ACTIVATION);

    TEST_ASSERT_EQUAL_HEX32 (
      0x3624, ZLINK_MESH_NODE_OPT_INSTANCE_ACTIVATION_MESSAGE_BUDGET);
    TEST_ASSERT_EQUAL_HEX32 (
      0x3625, ZLINK_MESH_NODE_OPT_INSTANCE_ACTIVATION_BYTE_BUDGET);
    TEST_ASSERT_EQUAL_HEX32 (
      0x3626, ZLINK_MESH_NODE_OPT_INSTANCE_ACTIVATION_TIMEOUT_MS);
}

void test_spot_status_v2_preserves_v1_prefix ()
{
#define ASSERT_PREFIX_OFFSET(member_)                                           \
    TEST_ASSERT_EQUAL_UINT64 (offsetof (spot_status_v1_prefix_t, member_),       \
                              offsetof (zlink_spot_status_t, member_))
    ASSERT_PREFIX_OFFSET (struct_size);
    ASSERT_PREFIX_OFFSET (version);
    ASSERT_PREFIX_OFFSET (spot_rid);
    ASSERT_PREFIX_OFFSET (spot_kind);
    ASSERT_PREFIX_OFFSET (lifecycle_generation);
    ASSERT_PREFIX_OFFSET (pending_application_messages);
    ASSERT_PREFIX_OFFSET (pending_infrastructure_messages);
    ASSERT_PREFIX_OFFSET (pending_bytes);
    ASSERT_PREFIX_OFFSET (active_actor_count);
    ASSERT_PREFIX_OFFSET (draining);
    ASSERT_PREFIX_OFFSET (last_error);
    ASSERT_PREFIX_OFFSET (last_changed_ms);
#undef ASSERT_PREFIX_OFFSET

    TEST_ASSERT_EQUAL_UINT64 (sizeof (spot_status_v1_prefix_t),
                              offsetof (zlink_spot_status_t, activation_state));
    TEST_ASSERT_TRUE (sizeof (zlink_spot_status_t)
                      >= offsetof (zlink_spot_status_t, activation_state)
                           + sizeof (zlink_spot_activation_state_t));
}

void test_instance_spot_structs_and_exports_are_available ()
{
    TEST_ASSERT_EQUAL_UINT64 (4 * sizeof (uint64_t),
                              sizeof (zlink_instance_spot_activation_token_t));
    TEST_ASSERT_EQUAL_UINT64 (
      0, offsetof (zlink_instance_spot_placement_t, node_rid));
    TEST_ASSERT_EQUAL_UINT64 (
      0, offsetof (zlink_instance_spot_activation_data_t, spot_rid));
    TEST_ASSERT_EQUAL_UINT64 (
      0, offsetof (zlink_instance_spot_claim_result_t, role));
    TEST_ASSERT_TRUE (sizeof (zlink_instance_spot_activation_data_t)
                      > sizeof (zlink_instance_spot_activation_token_t));

    TEST_ASSERT_NOT_NULL (
      reinterpret_cast<void *> (&zlink_spot_send_to_instance_placement));
    TEST_ASSERT_NOT_NULL (
      reinterpret_cast<void *> (&zlink_spot_request_to_instance_placement));
    TEST_ASSERT_NOT_NULL (
      reinterpret_cast<void *> (&zlink_instance_spot_activation_claim_owner));
    TEST_ASSERT_NOT_NULL (
      reinterpret_cast<void *> (&zlink_instance_spot_activation_mark_ready));
    TEST_ASSERT_NOT_NULL (
      reinterpret_cast<void *> (&zlink_instance_spot_activation_redirect));
    TEST_ASSERT_NOT_NULL (
      reinterpret_cast<void *> (&zlink_instance_spot_activation_abort));
    TEST_ASSERT_NOT_NULL (
      reinterpret_cast<void *> (&zlink_instance_spot_begin_close));
    TEST_ASSERT_NOT_NULL (
      reinterpret_cast<void *> (&zlink_instance_spot_renew_owner_admission));
}

void test_spot_status_v1_writes_only_the_frozen_prefix ()
{
    instance_fixture_t fixture = new_fixture ("instance-status-v1");

    status_v1_canary_t status;
    memset (&status, 0xA5, sizeof (status));
    status.prefix.struct_size = sizeof (status.prefix);
    status.prefix.version = 1;
    const uint64_t canary0 = status.canary[0];
    const uint64_t canary1 = status.canary[1];

    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK,
      zlink_spot_status (
        fixture.entry, reinterpret_cast<zlink_spot_status_t *> (&status)));
    TEST_ASSERT_EQUAL_UINT64 (canary0, status.canary[0]);
    TEST_ASSERT_EQUAL_UINT64 (canary1, status.canary[1]);
    TEST_ASSERT_EQUAL_UINT32 (sizeof (status.prefix), status.prefix.struct_size);
    TEST_ASSERT_EQUAL_UINT32 (1, status.prefix.version);
    TEST_ASSERT_EQUAL_INT (ZLINK_SPOT_KIND_ENTRY, status.prefix.spot_kind);

    //  An undersized v1 caller is rejected without modifying its storage.
    status_v1_canary_t undersized;
    memset (&undersized, 0x6B, sizeof (undersized));
    undersized.prefix.struct_size = sizeof (undersized.prefix) - 1;
    undersized.prefix.version = 1;
    const uint64_t undersized_canary0 = undersized.canary[0];
    const uint64_t undersized_canary1 = undersized.canary[1];
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_INVALID_ARGUMENT,
      zlink_spot_status (
        fixture.entry, reinterpret_cast<zlink_spot_status_t *> (&undersized)));
    TEST_ASSERT_EQUAL_INT (EINVAL, zlink_errno ());
    TEST_ASSERT_EQUAL_UINT64 (undersized_canary0, undersized.canary[0]);
    TEST_ASSERT_EQUAL_UINT64 (undersized_canary1, undersized.canary[1]);

    destroy_fixture (&fixture);
}

void test_instance_placement_rejects_malformed_fields ()
{
    instance_fixture_t fixture = new_fixture ("instance-target-validation");
    const zlink_instance_spot_placement_t valid = placement_target (
      fixture, "validation-1", "validation-worker", "ValidationCommand");
    zlink_msg_t part;
    make_payload (&part, "invalid");

    zlink_instance_spot_placement_t malformed = valid;
    malformed.node_generation = 0;
    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_INVALID_ARGUMENT,
      zlink_spot_send_to_instance_placement (
        fixture.entry, &malformed, NULL, &part, 1,
        ZLINK_SEND_FLAGS_DONTWAIT));
    TEST_ASSERT_EQUAL_INT (EINVAL, zlink_errno ());

    const char invalid_utf8[] = {static_cast<char> (0xC0),
                                 static_cast<char> (0xAF), 0};
    malformed = valid;
    malformed.instance_spot_type = invalid_utf8;
    malformed.instance_spot_type_size = 2;
    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_INVALID_ARGUMENT,
      zlink_spot_send_to_instance_placement (
        fixture.entry, &malformed, NULL, &part, 1,
        ZLINK_SEND_FLAGS_DONTWAIT));
    TEST_ASSERT_EQUAL_INT (EINVAL, zlink_errno ());

    malformed = valid;
    malformed.message_contract_id_size = 0;
    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_INVALID_ARGUMENT,
      zlink_spot_send_to_instance_placement (
        fixture.entry, &malformed, NULL, &part, 1,
        ZLINK_SEND_FLAGS_DONTWAIT));
    TEST_ASSERT_EQUAL_INT (EINVAL, zlink_errno ());

    zlink_msg_close (&part);
    assert_no_application_claim (fixture.node);
    destroy_fixture (&fixture);
}

void test_placement_claim_barrier_and_dispatch_ordering ()
{
    instance_fixture_t fixture = new_fixture ("instance-ordering");
    zlink_instance_spot_placement_t target = placement_target (
      fixture, "order-1001", "order-workflow", "OrderStarted");

    zlink_msg_t first_part;
    zlink_msg_t second_part;
    make_payload (&first_part, "first");
    make_payload (&second_part, "second");
    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_OK,
      zlink_spot_send_to_instance_placement (
        fixture.entry, &target, NULL, &first_part, 1, ZLINK_SEND_FLAGS_NONE));
    target.message_contract_id = "OrderUpdated";
    target.message_contract_id_size = strlen (target.message_contract_id);
    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_OK,
      zlink_spot_send_to_instance_placement (
        fixture.entry, &target, NULL, &second_part, 1, ZLINK_SEND_FLAGS_NONE));
    zlink_msg_close (&first_part);
    zlink_msg_close (&second_part);

    zlink_instance_spot_activation_data_t first_activation =
      take_activation (fixture.node);
    zlink_instance_spot_activation_data_t second_activation =
      take_activation (fixture.node);
    TEST_ASSERT_TRUE (
      (first_activation.token.opaque[0] | first_activation.token.opaque[1]
       | first_activation.token.opaque[2]
       | first_activation.token.opaque[3])
      != 0);
    TEST_ASSERT_TRUE (
      (second_activation.token.opaque[0] | second_activation.token.opaque[1]
       | second_activation.token.opaque[2]
       | second_activation.token.opaque[3])
      != 0);
    TEST_ASSERT_EQUAL_STRING ("order-workflow",
                              first_activation.instance_spot_type);
    TEST_ASSERT_EQUAL_STRING ("OrderStarted",
                              first_activation.message_contract_id);
    TEST_ASSERT_EQUAL_INT (ZLINK_INSTANCE_SPOT_OPERATION_SEND,
                           first_activation.operation_kind);
    TEST_ASSERT_EQUAL_UINT8 (target.spot_rid.size,
                             first_activation.spot_rid.size);
    TEST_ASSERT_EQUAL_MEMORY (target.spot_rid.data,
                              first_activation.spot_rid.data,
                              target.spot_rid.size);
    TEST_ASSERT_EQUAL_STRING ("OrderUpdated",
                              second_activation.message_contract_id);

    //  Pending payload remains behind the Core activation barrier.
    assert_no_application_claim (fixture.node);

    struct authorize_result_t
    {
        zlink_config_result_t result;
        zlink_instance_spot_claim_result_t claim;
    } results[2];
    memset (results, 0, sizeof (results));
    std::atomic<bool> start (false);
    std::thread first_authorizer ([&] {
        while (!start.load (std::memory_order_acquire))
            std::this_thread::yield ();
        results[0].result = zlink_instance_spot_activation_claim_owner (
          &first_activation.token, "owner-a", strlen ("owner-a"),
          &results[0].claim);
    });
    std::thread second_authorizer ([&] {
        while (!start.load (std::memory_order_acquire))
            std::this_thread::yield ();
        results[1].result = zlink_instance_spot_activation_claim_owner (
          &second_activation.token, "owner-a", strlen ("owner-a"),
          &results[1].claim);
    });
    start.store (true, std::memory_order_release);
    first_authorizer.join ();
    second_authorizer.join ();

    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK, results[0].result);
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK, results[1].result);
    TEST_ASSERT_TRUE (
      (results[0].claim.role == ZLINK_INSTANCE_SPOT_CLAIM_LEADER
       && results[1].claim.role == ZLINK_INSTANCE_SPOT_CLAIM_FOLLOWER)
      || (results[1].claim.role == ZLINK_INSTANCE_SPOT_CLAIM_LEADER
          && results[0].claim.role == ZLINK_INSTANCE_SPOT_CLAIM_FOLLOWER));

    //  The MeshNode owns this borrowed handle. A defensive destroy attempt
    //  must not end the activation or clear the caller's borrowed value. The
    //  exact close error is intentionally not part of the public contract.
    const int leader_index =
      results[0].claim.role == ZLINK_INSTANCE_SPOT_CLAIM_LEADER ? 0 : 1;
    const int follower_index = 1 - leader_index;
    TEST_ASSERT_NOT_NULL (results[leader_index].claim.leader_spot);
    TEST_ASSERT_TRUE (
      results[leader_index].claim.leader_spot_generation > 0);
    TEST_ASSERT_NULL (results[follower_index].claim.leader_spot);
    TEST_ASSERT_EQUAL_UINT64 (
      0, results[follower_index].claim.leader_spot_generation);
    void *borrowed_destroy_attempt =
      results[leader_index].claim.leader_spot;
    TEST_ASSERT_NOT_EQUAL (
      ZLINK_CLOSE_OK, zlink_spot_destroy (&borrowed_destroy_attempt));
    TEST_ASSERT_EQUAL_PTR (results[leader_index].claim.leader_spot,
                           borrowed_destroy_attempt);
    zlink_instance_spot_activation_token_t *leader_token =
      leader_index == 0 ? &first_activation.token : &second_activation.token;
    zlink_instance_spot_activation_token_t *follower_token =
      follower_index == 0 ? &first_activation.token : &second_activation.token;

    //  Claim consumes follower tokens immediately.
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_INVALID_STATE,
      zlink_instance_spot_activation_abort (
        follower_token, ZLINK_REQUEST_REJECTED, EACCES));
    TEST_ASSERT_EQUAL_INT (ESTALE, zlink_errno ());

    zlink_spot_status_t status;
    memset (&status, 0, sizeof (status));
    status.struct_size = sizeof (status);
    status.version = ZLINK_SPOT_ABI_VERSION;
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK,
      zlink_spot_status (results[leader_index].claim.leader_spot, &status));
    TEST_ASSERT_EQUAL_INT (ZLINK_SPOT_KIND_INSTANCE, status.spot_kind);
    TEST_ASSERT_EQUAL_INT (ZLINK_SPOT_ACTIVATION_ACTIVATING,
                           status.activation_state);
    TEST_ASSERT_EQUAL_UINT64 (2, status.pending_application_messages);
    TEST_ASSERT_EQUAL_UINT32 (0, status.active_actor_count);
    assert_no_application_claim (fixture.node);

    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK,
      zlink_instance_spot_activation_mark_ready (leader_token, 1000));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_INVALID_STATE,
      zlink_instance_spot_activation_mark_ready (leader_token, 1000));
    TEST_ASSERT_EQUAL_INT (ESTALE, zlink_errno ());

    memset (&status, 0, sizeof (status));
    status.struct_size = sizeof (status);
    status.version = ZLINK_SPOT_ABI_VERSION;
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK,
      zlink_spot_status (results[leader_index].claim.leader_spot, &status));
    TEST_ASSERT_EQUAL_INT (ZLINK_SPOT_ACTIVATION_READY,
                           status.activation_state);
    TEST_ASSERT_EQUAL_UINT64 (2, status.pending_application_messages);

    zlink_mesh_claim_t application_claim;
    memset (&application_claim, 0, sizeof (application_claim));
    take_claim (fixture.node, ZLINK_MESH_OWNER_SPOT,
                ZLINK_MESH_READY_APPLICATION, &application_claim);
    void *batch = zlink_mesh_receive_batch_new (2, 2, 32);
    TEST_ASSERT_NOT_NULL (batch);
    zlink_mesh_receive_requirements_t required;
    memset (&required, 0, sizeof (required));
    required.struct_size = sizeof (required);
    required.version = 1;
    TEST_ASSERT_EQUAL_INT (
      ZLINK_RECV_OK,
      zlink_mesh_claim_recv_batch (&application_claim, batch, &required,
                                   ZLINK_RECV_FLAGS_NONE));
    TEST_ASSERT_EQUAL_UINT64 (2, zlink_mesh_receive_batch_count (batch));
    const zlink_mesh_receive_record_t *records =
      zlink_mesh_receive_batch_data (batch);
    const zlink_msg_t *parts = zlink_mesh_receive_batch_parts (batch);
    TEST_ASSERT_EQUAL_INT (ZLINK_MESH_RECORD_SPOT_SEND, records[0].kind);
    TEST_ASSERT_EQUAL_INT (ZLINK_MESH_RECORD_SPOT_SEND, records[1].kind);
    TEST_ASSERT_EQUAL_MEMORY (
      "first",
      zlink_msg_data (const_cast<zlink_msg_t *> (
        &parts[records[0].part_offset])),
      5);
    TEST_ASSERT_EQUAL_MEMORY (
      "second",
      zlink_msg_data (const_cast<zlink_msg_t *> (
        &parts[records[1].part_offset])),
      6);
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK,
                           zlink_mesh_claim_release (&application_claim));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK,
                           zlink_mesh_receive_batch_destroy (&batch));

    memset (&status, 0, sizeof (status));
    status.struct_size = sizeof (status);
    status.version = ZLINK_SPOT_ABI_VERSION;
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK,
      zlink_spot_status (results[leader_index].claim.leader_spot, &status));
    TEST_ASSERT_EQUAL_UINT64 (0, status.pending_application_messages);

    destroy_fixture (&fixture);
}

void test_second_token_leader_still_preserves_admission_fifo ()
{
    instance_fixture_t fixture = new_fixture ("instance-reverse-leader");
    zlink_instance_spot_placement_t target = placement_target (
      fixture, "reverse-1", "reverse-worker", "FirstCommand");
    zlink_msg_t first;
    zlink_msg_t second;
    make_payload (&first, "first");
    make_payload (&second, "second");
    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_OK,
      zlink_spot_send_to_instance_placement (
        fixture.entry, &target, NULL, &first, 1, ZLINK_SEND_FLAGS_NONE));
    target.message_contract_id = "SecondCommand";
    target.message_contract_id_size = strlen (target.message_contract_id);
    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_OK,
      zlink_spot_send_to_instance_placement (
        fixture.entry, &target, NULL, &second, 1, ZLINK_SEND_FLAGS_NONE));
    zlink_msg_close (&first);
    zlink_msg_close (&second);
    zlink_instance_spot_activation_data_t first_activation =
      take_activation (fixture.node);
    zlink_instance_spot_activation_data_t second_activation =
      take_activation (fixture.node);

    zlink_instance_spot_claim_result_t second_result;
    zlink_instance_spot_claim_result_t first_result;
    memset (&second_result, 0, sizeof (second_result));
    memset (&first_result, 0, sizeof (first_result));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK,
      zlink_instance_spot_activation_claim_owner (
        &second_activation.token, "reverse-owner", strlen ("reverse-owner"),
        &second_result));
    TEST_ASSERT_EQUAL_INT (ZLINK_INSTANCE_SPOT_CLAIM_LEADER,
                           second_result.role);
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK,
      zlink_instance_spot_activation_claim_owner (
        &first_activation.token, "reverse-owner", strlen ("reverse-owner"),
        &first_result));
    TEST_ASSERT_EQUAL_INT (ZLINK_INSTANCE_SPOT_CLAIM_FOLLOWER,
                           first_result.role);
    TEST_ASSERT_NULL (first_result.leader_spot);
    TEST_ASSERT_EQUAL_UINT64 (0, first_result.leader_spot_generation);
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK,
      zlink_instance_spot_activation_mark_ready (&second_activation.token,
                                                1000));

    zlink_mesh_claim_t claim;
    memset (&claim, 0, sizeof (claim));
    take_claim (fixture.node, ZLINK_MESH_OWNER_SPOT,
                ZLINK_MESH_READY_APPLICATION, &claim);
    void *batch = zlink_mesh_receive_batch_new (2, 2, 32);
    zlink_mesh_receive_requirements_t required;
    memset (&required, 0, sizeof (required));
    required.struct_size = sizeof (required);
    required.version = 1;
    TEST_ASSERT_EQUAL_INT (
      ZLINK_RECV_OK,
      zlink_mesh_claim_recv_batch (&claim, batch, &required,
                                   ZLINK_RECV_FLAGS_NONE));
    TEST_ASSERT_EQUAL_UINT64 (2, zlink_mesh_receive_batch_count (batch));
    const zlink_mesh_receive_record_t *records =
      zlink_mesh_receive_batch_data (batch);
    const zlink_msg_t *parts = zlink_mesh_receive_batch_parts (batch);
    TEST_ASSERT_EQUAL_MEMORY (
      "first",
      zlink_msg_data (const_cast<zlink_msg_t *> (
        &parts[records[0].part_offset])),
      5);
    TEST_ASSERT_EQUAL_MEMORY (
      "second",
      zlink_msg_data (const_cast<zlink_msg_t *> (
        &parts[records[1].part_offset])),
      6);
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_claim_release (&claim));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK,
                           zlink_mesh_receive_batch_destroy (&batch));
    destroy_fixture (&fixture);
}

void test_pending_message_budget_and_abort_complete_once ()
{
    instance_fixture_t fixture = new_fixture ("instance-message-budget", 1);
    zlink_instance_spot_placement_t target = placement_target (
      fixture, "budget-1", "budget-worker", "BudgetCommand");

    zlink_msg_t first_part;
    zlink_msg_t second_part;
    make_payload (&first_part, "one");
    make_payload (&second_part, "two");
    zlink_mesh_operation_id_t first_operation;
    zlink_mesh_operation_id_t second_operation;
    memset (&first_operation, 0, sizeof (first_operation));
    memset (&second_operation, 0, sizeof (second_operation));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_OK,
      zlink_spot_request_to_instance_placement (
        fixture.entry, &target, NULL, &first_part, 1, &first_operation,
        ZLINK_SEND_FLAGS_NONE, 1000));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_OK,
      zlink_spot_request_to_instance_placement (
        fixture.entry, &target, NULL, &second_part, 1, &second_operation,
        ZLINK_SEND_FLAGS_NONE, 1000));
    zlink_msg_close (&first_part);
    zlink_msg_close (&second_part);

    zlink_instance_spot_activation_data_t activation =
      take_activation (fixture.node);
    TEST_ASSERT_EQUAL_INT (ZLINK_INSTANCE_SPOT_OPERATION_REQUEST,
                           activation.operation_kind);
    const zlink_mesh_receive_record_t backpressured =
      take_completion (fixture.node, second_operation);
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_BACKPRESSURED,
                           backpressured.terminal_result);
    TEST_ASSERT_TRUE (backpressured.failure_errno == EAGAIN
                      || backpressured.failure_errno == ENOBUFS);

    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK,
      zlink_instance_spot_activation_abort (
        &activation.token, ZLINK_REQUEST_REJECTED, EACCES));
    const zlink_mesh_receive_record_t rejected =
      take_completion (fixture.node, first_operation);
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_REJECTED, rejected.terminal_result);
    TEST_ASSERT_EQUAL_INT (EACCES, rejected.failure_errno);
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_INVALID_STATE,
      zlink_instance_spot_activation_abort (
        &activation.token, ZLINK_REQUEST_REJECTED, EACCES));
    TEST_ASSERT_EQUAL_INT (ESTALE, zlink_errno ());
    assert_no_duplicate_completion (fixture.node, first_operation);
    assert_no_duplicate_completion (fixture.node, second_operation);
    assert_no_application_claim (fixture.node);

    destroy_fixture (&fixture);
}

void test_pending_byte_budget_backpressures_whole_message ()
{
    instance_fixture_t fixture =
      new_fixture ("instance-byte-budget", 8, 5);
    zlink_instance_spot_placement_t target = placement_target (
      fixture, "budget-2", "budget-worker", "BudgetBytes");

    zlink_msg_t first_part;
    zlink_msg_t second_part;
    make_payload (&first_part, "1234");
    make_payload (&second_part, "5678");
    zlink_mesh_operation_id_t first_operation;
    zlink_mesh_operation_id_t second_operation;
    memset (&first_operation, 0, sizeof (first_operation));
    memset (&second_operation, 0, sizeof (second_operation));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_OK,
      zlink_spot_request_to_instance_placement (
        fixture.entry, &target, NULL, &first_part, 1, &first_operation,
        ZLINK_SEND_FLAGS_NONE, 1000));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_OK,
      zlink_spot_request_to_instance_placement (
        fixture.entry, &target, NULL, &second_part, 1, &second_operation,
        ZLINK_SEND_FLAGS_NONE, 1000));
    zlink_msg_close (&first_part);
    zlink_msg_close (&second_part);

    zlink_instance_spot_activation_data_t activation =
      take_activation (fixture.node);
    const zlink_mesh_receive_record_t backpressured =
      take_completion (fixture.node, second_operation);
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_BACKPRESSURED,
                           backpressured.terminal_result);
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK,
      zlink_instance_spot_activation_abort (
        &activation.token, ZLINK_REQUEST_REJECTED, EACCES));
    const zlink_mesh_receive_record_t rejected =
      take_completion (fixture.node, first_operation);
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_REJECTED, rejected.terminal_result);
    assert_no_application_claim (fixture.node);

    destroy_fixture (&fixture);
}

void test_short_request_deadline_does_not_abort_shared_activation ()
{
    instance_fixture_t fixture = new_fixture ("instance-call-deadline");
    zlink_instance_spot_placement_t target = placement_target (
      fixture, "deadline-1", "deadline-worker", "LongRequest");

    zlink_msg_t long_part;
    zlink_msg_t short_part;
    make_payload (&long_part, "long");
    make_payload (&short_part, "short");
    zlink_mesh_operation_id_t long_operation;
    zlink_mesh_operation_id_t short_operation;
    memset (&long_operation, 0, sizeof (long_operation));
    memset (&short_operation, 0, sizeof (short_operation));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_OK,
      zlink_spot_request_to_instance_placement (
        fixture.entry, &target, NULL, &long_part, 1, &long_operation,
        ZLINK_SEND_FLAGS_NONE, 1000));
    target.message_contract_id = "ShortRequest";
    target.message_contract_id_size = strlen (target.message_contract_id);
    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_OK,
      zlink_spot_request_to_instance_placement (
        fixture.entry, &target, NULL, &short_part, 1, &short_operation,
        ZLINK_SEND_FLAGS_NONE, 30));
    zlink_msg_close (&long_part);
    zlink_msg_close (&short_part);

    zlink_instance_spot_activation_data_t long_activation =
      take_activation (fixture.node);
    zlink_instance_spot_activation_data_t short_activation =
      take_activation (fixture.node);

    zlink_instance_spot_claim_result_t long_result;
    zlink_instance_spot_claim_result_t short_result;
    memset (&long_result, 0, sizeof (long_result));
    memset (&short_result, 0, sizeof (short_result));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK,
      zlink_instance_spot_activation_claim_owner (
        &long_activation.token, "owner-deadline", strlen ("owner-deadline"),
        &long_result));
    TEST_ASSERT_EQUAL_INT (ZLINK_INSTANCE_SPOT_CLAIM_LEADER,
                           long_result.role);
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK,
      zlink_instance_spot_activation_claim_owner (
        &short_activation.token, "owner-deadline",
        strlen ("owner-deadline"), &short_result));
    TEST_ASSERT_EQUAL_INT (ZLINK_INSTANCE_SPOT_CLAIM_FOLLOWER,
                           short_result.role);
    TEST_ASSERT_NULL (short_result.leader_spot);

    //  Both placements are already part of one activation group. Expiry of
    //  the short call removes only that operation while the shared barrier
    //  and the long call remain valid.
    msleep (60);
    const zlink_mesh_receive_record_t timed_out =
      take_completion (fixture.node, short_operation);
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_TIMED_OUT,
                           timed_out.terminal_result);
    TEST_ASSERT_EQUAL_INT (ETIMEDOUT, timed_out.failure_errno);
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK,
      zlink_instance_spot_activation_mark_ready (&long_activation.token, 1000));

    //  Only the still-live long request crosses the barrier.
    zlink_mesh_claim_t claim;
    memset (&claim, 0, sizeof (claim));
    take_claim (fixture.node, ZLINK_MESH_OWNER_SPOT,
                ZLINK_MESH_READY_APPLICATION, &claim);
    void *batch = zlink_mesh_receive_batch_new (2, 2, 32);
    TEST_ASSERT_NOT_NULL (batch);
    zlink_mesh_receive_requirements_t required;
    memset (&required, 0, sizeof (required));
    required.struct_size = sizeof (required);
    required.version = 1;
    TEST_ASSERT_EQUAL_INT (
      ZLINK_RECV_OK,
      zlink_mesh_claim_recv_batch (&claim, batch, &required,
                                   ZLINK_RECV_FLAGS_NONE));
    TEST_ASSERT_EQUAL_UINT64 (1, zlink_mesh_receive_batch_count (batch));
    const zlink_mesh_receive_record_t *request =
      zlink_mesh_receive_batch_data (batch);
    TEST_ASSERT_EQUAL_INT (ZLINK_MESH_RECORD_SPOT_REQUEST, request[0].kind);
    TEST_ASSERT_EQUAL_UINT64 (long_operation.high,
                              request[0].operation_id.high);
    TEST_ASSERT_EQUAL_UINT64 (long_operation.low, request[0].operation_id.low);
    const zlink_msg_t *parts = zlink_mesh_receive_batch_parts (batch);
    TEST_ASSERT_EQUAL_MEMORY (
      "long",
      zlink_msg_data (const_cast<zlink_msg_t *> (
        &parts[request[0].part_offset])),
      4);
    zlink_msg_t reply;
    make_payload (&reply, "ok");
    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_OK,
      zlink_mesh_reply (&request[0].reply_token, &reply, 1,
                        ZLINK_SEND_FLAGS_NONE));
    zlink_msg_close (&reply);
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_claim_release (&claim));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK,
                           zlink_mesh_receive_batch_destroy (&batch));

    const zlink_mesh_receive_record_t completed =
      take_completion (fixture.node, long_operation);
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK, completed.terminal_result);
    assert_no_duplicate_completion (fixture.node, short_operation);

    destroy_fixture (&fixture);
}

void test_activation_watchdog_stales_token_and_completes_once ()
{
    instance_fixture_t fixture = new_fixture ("instance-watchdog", 8, 1024, 40);
    zlink_instance_spot_placement_t target = placement_target (
      fixture, "watchdog-1", "watchdog-worker", "WatchdogRequest");
    zlink_msg_t part;
    make_payload (&part, "wait");
    zlink_mesh_operation_id_t operation;
    memset (&operation, 0, sizeof (operation));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_OK,
      zlink_spot_request_to_instance_placement (
        fixture.entry, &target, NULL, &part, 1, &operation,
        ZLINK_SEND_FLAGS_NONE, 1000));
    zlink_msg_close (&part);
    zlink_instance_spot_activation_data_t activation =
      take_activation (fixture.node);

    msleep (80);
    const zlink_mesh_receive_record_t timed_out =
      take_completion (fixture.node, operation);
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_TIMED_OUT,
                           timed_out.terminal_result);
    TEST_ASSERT_EQUAL_INT (ETIMEDOUT, timed_out.failure_errno);

    zlink_instance_spot_claim_result_t claim_result;
    memset (&claim_result, 0xA5, sizeof (claim_result));
    const zlink_instance_spot_claim_result_t before = claim_result;
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_INVALID_STATE,
      zlink_instance_spot_activation_claim_owner (
        &activation.token, "late-owner", strlen ("late-owner"),
        &claim_result));
    TEST_ASSERT_TRUE (zlink_errno () == ESTALE || zlink_errno () == ESHUTDOWN);
    TEST_ASSERT_EQUAL_MEMORY (&before, &claim_result, sizeof (claim_result));
    assert_no_duplicate_completion (fixture.node, operation);
    assert_no_application_claim (fixture.node);

    destroy_fixture (&fixture);
}

void test_copied_activation_token_is_single_consume_in_both_directions ()
{
    instance_fixture_t fixture = new_fixture ("instance-copied-token");
    zlink_instance_spot_placement_t target = placement_target (
      fixture, "copied-token-1", "token-worker", "OriginalFirst");

    zlink_msg_t first_part;
    make_payload (&first_part, "first");
    zlink_mesh_operation_id_t first_operation;
    memset (&first_operation, 0, sizeof (first_operation));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_OK,
      zlink_spot_request_to_instance_placement (
        fixture.entry, &target, NULL, &first_part, 1, &first_operation,
        ZLINK_SEND_FLAGS_NONE, 1000));
    zlink_msg_close (&first_part);
    zlink_instance_spot_activation_data_t first_activation =
      take_activation (fixture.node);
    zlink_instance_spot_activation_token_t first_copy =
      first_activation.token;
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK,
      zlink_instance_spot_activation_abort (
        &first_activation.token, ZLINK_REQUEST_REJECTED, EACCES));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_INVALID_STATE,
      zlink_instance_spot_activation_abort (
        &first_copy, ZLINK_REQUEST_REJECTED, EACCES));
    TEST_ASSERT_EQUAL_INT (ESTALE, zlink_errno ());
    const zlink_mesh_receive_record_t first_completion =
      take_completion (fixture.node, first_operation);
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_REJECTED,
                           first_completion.terminal_result);
    assert_no_duplicate_completion (fixture.node, first_operation);

    target = placement_target (
      fixture, "copied-token-2", "token-worker", "CopyFirst");
    zlink_msg_t second_part;
    make_payload (&second_part, "second");
    zlink_mesh_operation_id_t second_operation;
    memset (&second_operation, 0, sizeof (second_operation));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_OK,
      zlink_spot_request_to_instance_placement (
        fixture.entry, &target, NULL, &second_part, 1, &second_operation,
        ZLINK_SEND_FLAGS_NONE, 1000));
    zlink_msg_close (&second_part);
    zlink_instance_spot_activation_data_t second_activation =
      take_activation (fixture.node);
    zlink_instance_spot_activation_token_t second_copy =
      second_activation.token;
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK,
      zlink_instance_spot_activation_abort (
        &second_copy, ZLINK_REQUEST_REJECTED, EACCES));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_INVALID_STATE,
      zlink_instance_spot_activation_abort (
        &second_activation.token, ZLINK_REQUEST_REJECTED, EACCES));
    TEST_ASSERT_EQUAL_INT (ESTALE, zlink_errno ());
    const zlink_mesh_receive_record_t second_completion =
      take_completion (fixture.node, second_operation);
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_REJECTED,
                           second_completion.terminal_result);
    assert_no_duplicate_completion (fixture.node, second_operation);

    destroy_fixture (&fixture);
}

void test_abort_leaves_no_phantom_pending_work_for_drain ()
{
    instance_fixture_t fixture = new_fixture ("instance-abort-drain");
    zlink_instance_spot_placement_t target = placement_target (
      fixture, "abort-drain-1", "drain-worker", "AbortDrainRequest");
    zlink_msg_t part;
    make_payload (&part, "abort");
    zlink_mesh_operation_id_t operation;
    memset (&operation, 0, sizeof (operation));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_OK,
      zlink_spot_request_to_instance_placement (
        fixture.entry, &target, NULL, &part, 1, &operation,
        ZLINK_SEND_FLAGS_NONE, 1000));
    zlink_msg_close (&part);
    zlink_instance_spot_activation_data_t activation =
      take_activation (fixture.node);
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK,
      zlink_instance_spot_activation_abort (
        &activation.token, ZLINK_REQUEST_REJECTED, EACCES));
    const zlink_mesh_receive_record_t completion =
      take_completion (fixture.node, operation);
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_REJECTED,
                           completion.terminal_result);
    assert_no_duplicate_completion (fixture.node, operation);

    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK,
                           zlink_mesh_node_shutdown (fixture.node, 20));
    destroy_fixture (&fixture);
}

void test_watchdog_leaves_no_phantom_pending_work_for_drain ()
{
    instance_fixture_t fixture =
      new_fixture ("instance-watchdog-drain", 8, 1024, 20);
    zlink_instance_spot_placement_t target = placement_target (
      fixture, "watchdog-drain-1", "drain-worker", "WatchdogDrainRequest");
    zlink_msg_t part;
    make_payload (&part, "watchdog");
    zlink_mesh_operation_id_t operation;
    memset (&operation, 0, sizeof (operation));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_OK,
      zlink_spot_request_to_instance_placement (
        fixture.entry, &target, NULL, &part, 1, &operation,
        ZLINK_SEND_FLAGS_NONE, 1000));
    zlink_msg_close (&part);
    zlink_instance_spot_activation_data_t activation =
      take_activation (fixture.node);
    const zlink_mesh_receive_record_t completion =
      take_completion (fixture.node, operation);
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_TIMED_OUT,
                           completion.terminal_result);
    TEST_ASSERT_EQUAL_INT (ETIMEDOUT, completion.failure_errno);
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_INVALID_STATE,
      zlink_instance_spot_activation_abort (
        &activation.token, ZLINK_REQUEST_REJECTED, EACCES));
    TEST_ASSERT_EQUAL_INT (ESTALE, zlink_errno ());
    assert_no_duplicate_completion (fixture.node, operation);

    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK,
                           zlink_mesh_node_shutdown (fixture.node, 20));
    destroy_fixture (&fixture);
}

void test_call_deadline_stales_placement_token_before_redirect_retry ()
{
    instance_fixture_t fixture = new_fixture ("instance-deadline-redirect");
    zlink_instance_spot_placement_t placement = placement_target (
      fixture, "deadline-redirect-1", "redirect-worker",
      "DeadlineRedirectRequest");
    zlink_msg_t part;
    make_payload (&part, "deadline");
    zlink_mesh_operation_id_t operation;
    memset (&operation, 0, sizeof (operation));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_OK,
      zlink_spot_request_to_instance_placement (
        fixture.entry, &placement, NULL, &part, 1, &operation,
        ZLINK_SEND_FLAGS_NONE, 20));
    zlink_msg_close (&part);
    zlink_instance_spot_activation_data_t activation =
      take_activation (fixture.node);
    const zlink_mesh_receive_record_t completion =
      take_completion (fixture.node, operation);
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_TIMED_OUT,
                           completion.terminal_result);
    TEST_ASSERT_EQUAL_INT (ETIMEDOUT, completion.failure_errno);

    zlink_routing_id_t unreachable_node;
    make_rid (&unreachable_node, "absent-owner");
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_INVALID_STATE,
      zlink_instance_spot_activation_redirect (
        &activation.token, &unreachable_node, &placement.spot_rid, 1));
    TEST_ASSERT_EQUAL_INT (ESTALE, zlink_errno ());
    assert_no_duplicate_completion (fixture.node, operation);
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK,
                           zlink_mesh_node_shutdown (fixture.node, 20));
    destroy_fixture (&fixture);
}

void test_monotonic_owner_lease_fences_message_and_timer_admission ()
{
    instance_fixture_t fixture = new_fixture ("instance-lease");
    ready_instance_t instance = activate_one_send (
      &fixture, "lease-1", "lease-worker", "lease-owner", 40);

    std::atomic<uint64_t> timer_fires (0);
    void *timer = zlink_spot_timer_new (instance.spot);
    TEST_ASSERT_NOT_NULL (timer);
    TEST_ASSERT_EQUAL_INT (
      ZLINK_HANDLER_OK,
      zlink_timer_handler (timer, counting_timer_handler, &timer_fires));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK, zlink_timer_start (timer, 60ull * 1000 * 1000, 1));
    msleep (90);
    TEST_ASSERT_EQUAL_UINT64 (0, timer_fires.load (std::memory_order_relaxed));

    zlink_msg_t request_part;
    make_payload (&request_part, "after-lease");
    zlink_mesh_operation_id_t operation;
    memset (&operation, 0, sizeof (operation));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_OK,
      request_to_ready_instance (
        fixture.entry, &instance.direct_route, NULL, &request_part, 1,
        &operation, ZLINK_SEND_FLAGS_NONE, 500));
    zlink_msg_close (&request_part);
    const zlink_mesh_receive_record_t completion =
      take_completion (fixture.node, operation);
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_BUSY, completion.terminal_result);
    TEST_ASSERT_EQUAL_INT (EBUSY, completion.failure_errno);
    assert_no_application_claim (fixture.node);

    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_INVALID_STATE,
      zlink_instance_spot_renew_owner_admission (instance.spot, 1000));
    TEST_ASSERT_EQUAL_INT (ESTALE, zlink_errno ());
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_timer_destroy (&timer));

    destroy_fixture (&fixture);
}

void test_begin_close_seals_admission_and_keeps_closing_visible ()
{
    instance_fixture_t fixture = new_fixture ("instance-close");
    ready_instance_t instance = activate_one_send (
      &fixture, "close-1", "close-worker", "close-owner", 1000);

    std::atomic<uint64_t> timer_fires (0);
    void *timer = zlink_spot_timer_new (instance.spot);
    TEST_ASSERT_NOT_NULL (timer);
    TEST_ASSERT_EQUAL_INT (
      ZLINK_HANDLER_OK,
      zlink_timer_handler (timer, counting_timer_handler, &timer_fires));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK, zlink_timer_start (timer, 60ull * 1000 * 1000, 1));

    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK,
      zlink_instance_spot_begin_close (instance.spot));
    zlink_spot_status_t status;
    memset (&status, 0, sizeof (status));
    status.struct_size = sizeof (status);
    status.version = ZLINK_SPOT_ABI_VERSION;
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK, zlink_spot_status (instance.spot, &status));
    TEST_ASSERT_EQUAL_INT (ZLINK_SPOT_ACTIVATION_CLOSING,
                           status.activation_state);

    zlink_msg_t request_part;
    make_payload (&request_part, "after-close");
    zlink_mesh_operation_id_t operation;
    memset (&operation, 0, sizeof (operation));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_OK,
      request_to_ready_instance (
        fixture.entry, &instance.direct_route, NULL, &request_part, 1,
        &operation, ZLINK_SEND_FLAGS_NONE, 500));
    zlink_msg_close (&request_part);
    const zlink_mesh_receive_record_t completion =
      take_completion (fixture.node, operation);
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_BUSY, completion.terminal_result);
    TEST_ASSERT_EQUAL_INT (EBUSY, completion.failure_errno);

    msleep (90);
    TEST_ASSERT_EQUAL_UINT64 (0, timer_fires.load (std::memory_order_relaxed));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_timer_destroy (&timer));
    assert_no_application_claim (fixture.node);

    destroy_fixture (&fixture);
}

void test_begin_close_races_renew_and_message_without_reopening_admission ()
{
    instance_fixture_t fixture = new_fixture ("instance-close-race");
    ready_instance_t instance = activate_one_send (
      &fixture, "close-race-1", "close-race-worker", "close-race-owner",
      1000);
    void *timer = zlink_spot_timer_new (instance.spot);
    TEST_ASSERT_NOT_NULL (timer);

    std::atomic<bool> start (false);
    zlink_config_result_t close_result = ZLINK_CONFIG_INTERNAL_ERROR;
    zlink_config_result_t renew_result = ZLINK_CONFIG_INTERNAL_ERROR;
    zlink_submit_result_t submit_result = ZLINK_SUBMIT_INTERNAL_ERROR;
    zlink_mesh_operation_id_t operation;
    memset (&operation, 0, sizeof (operation));
    zlink_msg_t part;
    make_payload (&part, "race");
    std::thread closer ([&] {
        while (!start.load (std::memory_order_acquire))
            std::this_thread::yield ();
        close_result = zlink_instance_spot_begin_close (instance.spot);
    });
    std::thread renewer ([&] {
        while (!start.load (std::memory_order_acquire))
            std::this_thread::yield ();
        renew_result = zlink_instance_spot_renew_owner_admission (instance.spot, 1000);
    });
    std::thread requester ([&] {
        while (!start.load (std::memory_order_acquire))
            std::this_thread::yield ();
        submit_result = request_to_ready_instance (
        fixture.entry, &instance.direct_route, NULL, &part, 1, &operation,
          ZLINK_SEND_FLAGS_NONE, 500);
    });
    start.store (true, std::memory_order_release);
    closer.join ();
    renewer.join ();
    requester.join ();
    zlink_msg_close (&part);

    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK, close_result);
    TEST_ASSERT_TRUE (renew_result == ZLINK_CONFIG_OK
                      || renew_result == ZLINK_CONFIG_INVALID_STATE);
    TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_OK, submit_result);

    zlink_spot_status_t status;
    memset (&status, 0, sizeof (status));
    status.struct_size = sizeof (status);
    status.version = ZLINK_SPOT_ABI_VERSION;
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK, zlink_spot_status (instance.spot, &status));
    TEST_ASSERT_EQUAL_INT (ZLINK_SPOT_ACTIVATION_CLOSING,
                           status.activation_state);

    zlink_mesh_claim_t claim;
    memset (&claim, 0, sizeof (claim));
    bool admitted_before_close = false;
    for (int attempt = 0; attempt < 20 && !admitted_before_close; ++attempt) {
        admitted_before_close = try_take_claim (
          fixture.node, ZLINK_MESH_OWNER_SPOT,
          ZLINK_MESH_READY_APPLICATION, &claim);
        if (!admitted_before_close)
            msleep (2);
    }
    if (admitted_before_close) {
        void *batch = zlink_mesh_receive_batch_new (1, 1, 32);
        zlink_mesh_receive_requirements_t required;
        memset (&required, 0, sizeof (required));
        required.struct_size = sizeof (required);
        required.version = 1;
        TEST_ASSERT_EQUAL_INT (
          ZLINK_RECV_OK,
          zlink_mesh_claim_recv_batch (&claim, batch, &required,
                                       ZLINK_RECV_FLAGS_NONE));
        const zlink_mesh_receive_record_t *request =
          zlink_mesh_receive_batch_data (batch);
        TEST_ASSERT_EQUAL_INT (ZLINK_MESH_RECORD_SPOT_REQUEST,
                               request[0].kind);
        zlink_msg_t reply;
        make_payload (&reply, "accepted-before-close");
        TEST_ASSERT_EQUAL_INT (
          ZLINK_SUBMIT_OK,
          zlink_mesh_reply (&request[0].reply_token, &reply, 1,
                            ZLINK_SEND_FLAGS_NONE));
        zlink_msg_close (&reply);
        TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK,
                               zlink_mesh_claim_release (&claim));
        TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK,
                               zlink_mesh_receive_batch_destroy (&batch));
    }
    const zlink_mesh_receive_record_t completion =
      take_completion (fixture.node, operation);
    TEST_ASSERT_TRUE (
      (admitted_before_close && completion.terminal_result == ZLINK_REQUEST_OK)
      || (!admitted_before_close
          && completion.terminal_result == ZLINK_REQUEST_BUSY));

    //  No outcome reopens a sealed Instance activation.
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_INVALID_STATE,
      zlink_instance_spot_renew_owner_admission (instance.spot, 1000));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_timer_destroy (&timer));
    destroy_fixture (&fixture);
}

void test_user_and_entry_spot_kinds_conflict_with_instance_claim ()
{
    instance_fixture_t fixture = new_fixture ("instance-kind-conflict");
    const char *const collision_rids[] = {"existing-user",
                                          "instance-kind-conflict"};
    void *user_spot = NULL;
    zlink_routing_id_t user_rid;
    make_rid (&user_rid, collision_rids[0]);
    uint32_t created = 0;
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK,
      zlink_mesh_node_spot_get_or_new (fixture.node, &user_rid, &user_spot,
                                       &created));
    TEST_ASSERT_EQUAL_UINT32 (1, created);

    for (size_t i = 0; i < 2; ++i) {
        zlink_instance_spot_placement_t target = placement_target (
          fixture, collision_rids[i], "kind-worker", "KindRequest");
        zlink_msg_t part;
        make_payload (&part, "collision");
        zlink_mesh_operation_id_t operation;
        memset (&operation, 0, sizeof (operation));
        TEST_ASSERT_EQUAL_INT (
          ZLINK_SUBMIT_OK,
          zlink_spot_request_to_instance_placement (
            fixture.entry, &target, NULL, &part, 1, &operation,
            ZLINK_SEND_FLAGS_NONE, 1000));
        zlink_msg_close (&part);
        zlink_instance_spot_activation_data_t activation =
          take_activation (fixture.node);

        zlink_instance_spot_claim_result_t claim_result;
        memset (&claim_result, 0xA5, sizeof (claim_result));
        const zlink_instance_spot_claim_result_t before = claim_result;
        TEST_ASSERT_EQUAL_INT (
          ZLINK_CONFIG_CONFLICT,
          zlink_instance_spot_activation_claim_owner (
            &activation.token, "kind-owner", strlen ("kind-owner"),
            &claim_result));
        TEST_ASSERT_EQUAL_INT (EEXIST, zlink_errno ());
        TEST_ASSERT_EQUAL_MEMORY (
          &before, &claim_result, sizeof (claim_result));

        //  A conflict does not consume the placement token; Framework can
        //  terminate the original request explicitly.
        TEST_ASSERT_EQUAL_INT (
          ZLINK_CONFIG_OK,
          zlink_instance_spot_activation_abort (
            &activation.token, ZLINK_REQUEST_CONFLICT, EEXIST));
        const zlink_mesh_receive_record_t completion =
          take_completion (fixture.node, operation);
        TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_CONFLICT,
                               completion.terminal_result);
        TEST_ASSERT_EQUAL_INT (EEXIST, completion.failure_errno);
    }

    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_spot_destroy (&user_spot));
    destroy_fixture (&fixture);
}

void test_exact_generation_and_actor_boundaries ()
{
    instance_fixture_t fixture = new_fixture ("instance-boundaries");
    ready_instance_t instance = activate_one_send (
      &fixture, "boundary-1", "boundary-worker", "boundary-owner", 1000);

    {
        direct_target_t stale = instance.direct_route;
        stale.spot_generation += 1;
        zlink_msg_t part;
        make_payload (&part, "stale");
        zlink_mesh_operation_id_t operation;
        memset (&operation, 0, sizeof (operation));
        TEST_ASSERT_EQUAL_INT (
          ZLINK_SUBMIT_OK,
          request_to_ready_instance (
            fixture.entry, &stale, NULL, &part, 1, &operation,
            ZLINK_SEND_FLAGS_NONE, 500));
        zlink_msg_close (&part);
        const zlink_mesh_receive_record_t completion =
          take_completion (fixture.node, operation);
        TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_CONFLICT,
                               completion.terminal_result);
        TEST_ASSERT_EQUAL_INT (ESTALE, completion.failure_errno);
    }

    zlink_actor_ref_t actor;
    memset (&actor, 0, sizeof (actor));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_REQUEST_OK,
      zlink_mesh_node_actor_new (fixture.node, "boundary-actor", NULL, 0,
                                 &actor, ZLINK_SEND_FLAGS_NONE, 1000));
    //  Consume the actor creation lifecycle record on the entry Spot.
    zlink_mesh_claim_t creation_claim;
    memset (&creation_claim, 0, sizeof (creation_claim));
    take_claim (fixture.node, ZLINK_MESH_OWNER_SPOT,
                ZLINK_MESH_READY_APPLICATION, &creation_claim);
    void *creation_batch = zlink_mesh_receive_batch_new (1, 1, 32);
    TEST_ASSERT_NOT_NULL (creation_batch);
    zlink_mesh_receive_requirements_t required;
    memset (&required, 0, sizeof (required));
    required.struct_size = sizeof (required);
    required.version = 1;
    TEST_ASSERT_EQUAL_INT (
      ZLINK_RECV_OK,
      zlink_mesh_claim_recv_batch (&creation_claim, creation_batch, &required,
                                   ZLINK_RECV_FLAGS_NONE));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK,
                           zlink_mesh_claim_release (&creation_claim));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK,
                           zlink_mesh_receive_batch_destroy (&creation_batch));

    zlink_mesh_operation_id_t join_operation;
    memset (&join_operation, 0, sizeof (join_operation));
    const zlink_submit_result_t join_result = zlink_mesh_node_actor_join_spot (
      fixture.node, &actor, &fixture.node_status.routing_id,
      &instance.direct_route.spot_rid, instance.generation, NULL, 0,
      &join_operation, 500);
    if (join_result == ZLINK_SUBMIT_OK) {
        const zlink_mesh_receive_record_t completion =
          take_completion (fixture.node, join_operation);
        TEST_ASSERT_NOT_EQUAL (ZLINK_REQUEST_OK, completion.terminal_result);
    }

    zlink_spot_status_t status;
    memset (&status, 0, sizeof (status));
    status.struct_size = sizeof (status);
    status.version = ZLINK_SPOT_ABI_VERSION;
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK, zlink_spot_status (instance.spot, &status));
    TEST_ASSERT_EQUAL_UINT32 (0, status.active_actor_count);
    assert_no_application_claim (fixture.node);

    zlink_mesh_operation_id_t destroy_operation;
    memset (&destroy_operation, 0, sizeof (destroy_operation));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_OK,
      zlink_mesh_node_actor_destroy (fixture.node, &actor, &destroy_operation,
                                     500));
    destroy_fixture (&fixture);
}

void test_shutdown_terminates_pending_activation_and_stales_token ()
{
    instance_fixture_t fixture = new_fixture ("instance-shutdown");
    zlink_instance_spot_placement_t target = placement_target (
      fixture, "shutdown-1", "shutdown-worker", "ShutdownRequest");
    zlink_msg_t part;
    make_payload (&part, "pending");
    zlink_mesh_operation_id_t operation;
    memset (&operation, 0, sizeof (operation));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_OK,
      zlink_spot_request_to_instance_placement (
        fixture.entry, &target, NULL, &part, 1, &operation,
        ZLINK_SEND_FLAGS_NONE, 0));
    zlink_msg_close (&part);
    zlink_instance_spot_activation_data_t activation =
      take_activation (fixture.node);

    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_TIMED_OUT,
                           zlink_mesh_node_shutdown (fixture.node, 100));
    TEST_ASSERT_EQUAL_INT (ETIMEDOUT, zlink_errno ());
    const zlink_mesh_receive_record_t completion =
      take_completion (fixture.node, operation);
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_TERMINATED,
                           completion.terminal_result);
    TEST_ASSERT_TRUE (completion.failure_errno == ESHUTDOWN
                      || completion.failure_errno == ETERM);

    zlink_instance_spot_claim_result_t claim_result;
    memset (&claim_result, 0xA5, sizeof (claim_result));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_INVALID_STATE,
      zlink_instance_spot_activation_claim_owner (
        &activation.token, "shutdown-owner", strlen ("shutdown-owner"),
        &claim_result));
    TEST_ASSERT_TRUE (zlink_errno () == ESHUTDOWN || zlink_errno () == ESTALE);
    assert_no_duplicate_completion (fixture.node, operation);

    destroy_fixture (&fixture);
}

void test_shutdown_force_terminates_ready_instance_mailbox_request_once ()
{
    instance_fixture_t fixture = new_fixture ("instance-ready-shutdown");
    ready_instance_t instance = activate_one_send (
      &fixture, "ready-shutdown-1", "shutdown-worker", "shutdown-owner",
      1000);
    zlink_msg_t part;
    make_payload (&part, "ready-pending");
    zlink_mesh_operation_id_t operation;
    memset (&operation, 0, sizeof (operation));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_OK,
      request_to_ready_instance (
        fixture.entry, &instance.direct_route, NULL, &part, 1, &operation,
        ZLINK_SEND_FLAGS_NONE, 0));
    zlink_msg_close (&part);

    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_TIMED_OUT,
                           zlink_mesh_node_shutdown (fixture.node, 100));
    TEST_ASSERT_EQUAL_INT (ETIMEDOUT, zlink_errno ());
    const zlink_mesh_receive_record_t completion =
      take_completion (fixture.node, operation);
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_TERMINATED,
                           completion.terminal_result);
    TEST_ASSERT_TRUE (completion.failure_errno == ESHUTDOWN
                      || completion.failure_errno == ETERM);
    assert_no_duplicate_completion (fixture.node, operation);
    destroy_fixture (&fixture);
}

void test_shutdown_retires_ready_borrowed_facade_without_allocation ()
{
    instance_fixture_t fixture = new_fixture ("instance-retire-no-alloc");
    ready_instance_t instance = activate_one_send (
      &fixture, "retire-no-alloc-1", "retire-worker", "retire-owner",
      1000);

    //  Ready Instance retirement must not allocate after invalidating the
    //  borrowed facade. Allocation pressure cannot leave a live facade behind
    //  after shutdown has reported success.
    zlink_test_set_mesh_alloc_fault (1);
    const zlink_request_result_t shutdown_result =
      zlink_mesh_node_shutdown (fixture.node, 1000);
    zlink_test_set_mesh_alloc_fault (0);
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK, shutdown_result);

    zlink_spot_status_t status;
    memset (&status, 0, sizeof (status));
    status.struct_size = sizeof (status);
    status.version = ZLINK_SPOT_ABI_VERSION;
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_INVALID_HANDLE,
      zlink_spot_status (instance.spot, &status));
    TEST_ASSERT_EQUAL_INT (EFAULT, zlink_errno ());
    destroy_fixture (&fixture);
}

void test_shutdown_force_terminates_claimed_unreplied_instance_request_once ()
{
    instance_fixture_t fixture = new_fixture ("instance-claimed-shutdown");
    ready_instance_t instance = activate_one_send (
      &fixture, "claimed-shutdown-1", "shutdown-worker", "shutdown-owner",
      1000);
    zlink_msg_t part;
    make_payload (&part, "claimed-pending");
    zlink_mesh_operation_id_t operation;
    memset (&operation, 0, sizeof (operation));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_OK,
      request_to_ready_instance (
        fixture.entry, &instance.direct_route, NULL, &part, 1, &operation,
        ZLINK_SEND_FLAGS_NONE, 0));
    zlink_msg_close (&part);

    zlink_mesh_claim_t claim;
    memset (&claim, 0, sizeof (claim));
    take_claim (fixture.node, ZLINK_MESH_OWNER_SPOT,
                ZLINK_MESH_READY_APPLICATION, &claim);
    void *batch = zlink_mesh_receive_batch_new (1, 1, 64);
    zlink_mesh_receive_requirements_t required;
    memset (&required, 0, sizeof (required));
    required.struct_size = sizeof (required);
    required.version = 1;
    TEST_ASSERT_EQUAL_INT (
      ZLINK_RECV_OK,
      zlink_mesh_claim_recv_batch (&claim, batch, &required,
                                   ZLINK_RECV_FLAGS_NONE));
    TEST_ASSERT_EQUAL_UINT64 (1, zlink_mesh_receive_batch_count (batch));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_MESH_RECORD_SPOT_REQUEST,
      zlink_mesh_receive_batch_data (batch)[0].kind);

    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_TIMED_OUT,
                           zlink_mesh_node_shutdown (fixture.node, 100));
    TEST_ASSERT_EQUAL_INT (ETIMEDOUT, zlink_errno ());
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_claim_release (&claim));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK,
                           zlink_mesh_receive_batch_destroy (&batch));
    const zlink_mesh_receive_record_t completion =
      take_completion (fixture.node, operation);
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_TERMINATED,
                           completion.terminal_result);
    TEST_ASSERT_TRUE (completion.failure_errno == ESHUTDOWN
                      || completion.failure_errno == ETERM);
    assert_no_duplicate_completion (fixture.node, operation);
    destroy_fixture (&fixture);
}

void test_shutdown_force_preserves_committing_instance_reply_route ()
{
    instance_fixture_t fixture = new_fixture ("instance-committing-shutdown");
    ready_instance_t instance = activate_one_send (
      &fixture, "committing-shutdown-1", "shutdown-worker",
      "shutdown-owner", 1000);
    zlink_msg_t part;
    make_payload (&part, "committing-pending");
    zlink_mesh_operation_id_t operation;
    memset (&operation, 0, sizeof (operation));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_OK,
      request_to_ready_instance (
        fixture.entry, &instance.direct_route, NULL, &part, 1, &operation,
        ZLINK_SEND_FLAGS_NONE, 0));
    zlink_msg_close (&part);

    zlink_mesh_claim_t claim;
    memset (&claim, 0, sizeof (claim));
    take_claim (fixture.node, ZLINK_MESH_OWNER_SPOT,
                ZLINK_MESH_READY_APPLICATION, &claim);
    void *batch = zlink_mesh_receive_batch_new (1, 1, 64);
    TEST_ASSERT_NOT_NULL (batch);
    zlink_mesh_receive_requirements_t required;
    memset (&required, 0, sizeof (required));
    required.struct_size = sizeof (required);
    required.version = 1;
    TEST_ASSERT_EQUAL_INT (
      ZLINK_RECV_OK,
      zlink_mesh_claim_recv_batch (&claim, batch, &required,
                                   ZLINK_RECV_FLAGS_NONE));
    TEST_ASSERT_EQUAL_UINT64 (1, zlink_mesh_receive_batch_count (batch));
    const zlink_mesh_reply_token_t reply_token =
      zlink_mesh_receive_batch_data (batch)[0].reply_token;

    //  Model a completion that has reserved its operation but has not yet
    //  committed. Forced drain must keep the reply route pending until the
    //  shutdown operation loop can complete the requester exactly once.
    TEST_ASSERT_EQUAL_INT (
      1, zlink_test_mesh_set_operation_committing (
           fixture.node, &operation, 1));
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_TIMED_OUT,
                           zlink_mesh_node_shutdown (fixture.node, 0));
    TEST_ASSERT_EQUAL_INT (ETIMEDOUT, zlink_errno ());
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_claim_release (&claim));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK,
                           zlink_mesh_receive_batch_destroy (&batch));

    const zlink_mesh_receive_record_t completion =
      take_completion (fixture.node, operation);
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_TERMINATED,
                           completion.terminal_result);
    TEST_ASSERT_EQUAL_INT (ESHUTDOWN, completion.failure_errno);

    zlink_msg_t late_reply;
    make_payload (&late_reply, "late-reply");
    const zlink_submit_result_t late_result = zlink_mesh_reply (
      &reply_token, &late_reply, 1, ZLINK_SEND_FLAGS_NONE);
    zlink_msg_close (&late_reply);
    TEST_ASSERT_TRUE (late_result == ZLINK_SUBMIT_OK
                      || late_result == ZLINK_SUBMIT_INVALID_STATE);
    assert_no_duplicate_completion (fixture.node, operation);
    destroy_fixture (&fixture);
}

void test_force_drain_and_handler_reply_race_has_one_terminal_completion ()
{
    instance_fixture_t fixture = new_fixture ("instance-reply-drain-race");
    ready_instance_t instance = activate_one_send (
      &fixture, "reply-drain-race-1", "race-worker", "race-owner", 1000);
    zlink_msg_t part;
    make_payload (&part, "race-request");
    zlink_mesh_operation_id_t operation;
    memset (&operation, 0, sizeof (operation));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_OK,
      request_to_ready_instance (
        fixture.entry, &instance.direct_route, NULL, &part, 1, &operation,
        ZLINK_SEND_FLAGS_NONE, 0));
    zlink_msg_close (&part);

    zlink_mesh_claim_t claim;
    memset (&claim, 0, sizeof (claim));
    take_claim (fixture.node, ZLINK_MESH_OWNER_SPOT,
                ZLINK_MESH_READY_APPLICATION, &claim);
    void *batch = zlink_mesh_receive_batch_new (1, 1, 64);
    TEST_ASSERT_NOT_NULL (batch);
    zlink_mesh_receive_requirements_t required;
    memset (&required, 0, sizeof (required));
    required.struct_size = sizeof (required);
    required.version = 1;
    TEST_ASSERT_EQUAL_INT (
      ZLINK_RECV_OK,
      zlink_mesh_claim_recv_batch (&claim, batch, &required,
                                   ZLINK_RECV_FLAGS_NONE));
    TEST_ASSERT_EQUAL_UINT64 (1, zlink_mesh_receive_batch_count (batch));
    const zlink_mesh_receive_record_t *request =
      zlink_mesh_receive_batch_data (batch);
    TEST_ASSERT_EQUAL_INT (ZLINK_MESH_RECORD_SPOT_REQUEST, request[0].kind);
    const zlink_mesh_reply_token_t reply_token = request[0].reply_token;

    zlink_msg_t reply;
    make_payload (&reply, "race-reply");
    std::atomic<bool> start (false);
    zlink_request_result_t shutdown_result = ZLINK_REQUEST_INTERNAL_ERROR;
    int shutdown_errno = 0;
    zlink_submit_result_t reply_result = ZLINK_SUBMIT_INTERNAL_ERROR;
    int reply_errno = 0;
    std::thread shutdown_thread ([&] {
        while (!start.load (std::memory_order_acquire))
            std::this_thread::yield ();
        shutdown_result = zlink_mesh_node_shutdown (fixture.node, 0);
        shutdown_errno = zlink_errno ();
    });
    std::thread reply_thread ([&] {
        while (!start.load (std::memory_order_acquire))
            std::this_thread::yield ();
        reply_result = zlink_mesh_reply (
          &reply_token, &reply, 1, ZLINK_SEND_FLAGS_NONE);
        reply_errno = zlink_errno ();
    });
    start.store (true, std::memory_order_release);
    shutdown_thread.join ();
    reply_thread.join ();
    zlink_msg_close (&reply);

    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_TIMED_OUT, shutdown_result);
    TEST_ASSERT_EQUAL_INT (ETIMEDOUT, shutdown_errno);
    TEST_ASSERT_TRUE (reply_result == ZLINK_SUBMIT_OK
                      || reply_result == ZLINK_SUBMIT_INVALID_STATE);
    if (reply_result == ZLINK_SUBMIT_INVALID_STATE)
        TEST_ASSERT_TRUE (reply_errno == EALREADY || reply_errno == EBUSY
                          || reply_errno == ESHUTDOWN);
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK, zlink_mesh_claim_release (&claim));
    TEST_ASSERT_EQUAL_INT (ZLINK_CLOSE_OK,
                           zlink_mesh_receive_batch_destroy (&batch));

    const zlink_mesh_receive_record_t completion =
      take_completion (fixture.node, operation);
    TEST_ASSERT_TRUE (completion.terminal_result == ZLINK_REQUEST_OK
                      || completion.terminal_result
                           == ZLINK_REQUEST_TERMINATED);
    if (completion.terminal_result == ZLINK_REQUEST_OK)
        TEST_ASSERT_EQUAL_INT (0, completion.failure_errno);
    else
        TEST_ASSERT_EQUAL_INT (ESHUTDOWN, completion.failure_errno);
    assert_no_duplicate_completion (fixture.node, operation);
    destroy_fixture (&fixture);
}

int main ()
{
    setup_test_environment ();
    UNITY_BEGIN ();
    RUN_TEST (test_instance_spot_public_constants_are_frozen);
    RUN_TEST (test_spot_status_v2_preserves_v1_prefix);
    RUN_TEST (test_instance_spot_structs_and_exports_are_available);
    RUN_TEST (test_spot_status_v1_writes_only_the_frozen_prefix);
    RUN_TEST (test_instance_placement_rejects_malformed_fields);
    RUN_TEST (test_placement_claim_barrier_and_dispatch_ordering);
    RUN_TEST (test_second_token_leader_still_preserves_admission_fifo);
    RUN_TEST (test_pending_message_budget_and_abort_complete_once);
    RUN_TEST (test_pending_byte_budget_backpressures_whole_message);
    RUN_TEST (test_short_request_deadline_does_not_abort_shared_activation);
    RUN_TEST (test_activation_watchdog_stales_token_and_completes_once);
    RUN_TEST (
      test_copied_activation_token_is_single_consume_in_both_directions);
    RUN_TEST (test_abort_leaves_no_phantom_pending_work_for_drain);
    RUN_TEST (test_watchdog_leaves_no_phantom_pending_work_for_drain);
    RUN_TEST (
      test_call_deadline_stales_placement_token_before_redirect_retry);
    RUN_TEST (test_monotonic_owner_lease_fences_message_and_timer_admission);
    RUN_TEST (test_begin_close_seals_admission_and_keeps_closing_visible);
    RUN_TEST (
      test_begin_close_races_renew_and_message_without_reopening_admission);
    RUN_TEST (test_user_and_entry_spot_kinds_conflict_with_instance_claim);
    RUN_TEST (test_exact_generation_and_actor_boundaries);
    RUN_TEST (test_shutdown_terminates_pending_activation_and_stales_token);
    RUN_TEST (
      test_shutdown_force_terminates_ready_instance_mailbox_request_once);
    RUN_TEST (
      test_shutdown_retires_ready_borrowed_facade_without_allocation);
    RUN_TEST (
      test_shutdown_force_terminates_claimed_unreplied_instance_request_once);
    RUN_TEST (
      test_shutdown_force_preserves_committing_instance_reply_route);
    RUN_TEST (
      test_force_drain_and_handler_reply_race_has_one_terminal_completion);
    return UNITY_END ();
}
