/* SPDX-License-Identifier: MPL-2.0 */

#ifndef ZLINK_API_MESH_C_INTERNAL_HPP_INCLUDED
#define ZLINK_API_MESH_C_INTERNAL_HPP_INCLUDED

#include <zlink.h>

#include "services/mesh/mesh_runtime.hpp"

//  Shared helpers for the mesh C surface: handle resolution with the public
//  result enums and versioned-struct validation per the service common
//  contract.
namespace zlink
{
namespace mesh
{
//  Versioned caller-init struct check: struct_size >= current size and
//  version == 1. Returns 0 or -1/EINVAL.
template <typename T> int check_versioned (const T *value_)
{
    if (!value_ || value_->struct_size < sizeof (T) || value_->version != 1) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

template <typename T> void init_versioned (T *value_)
{
    memset (value_, 0, sizeof (T));
    value_->struct_size = sizeof (T);
    value_->version = 1;
}

//  Name checks: 1..max bytes UTF-8 without NUL, from a NUL-terminated input.
int check_name (const char *name_, size_t max_, std::string *out_);

//  UTF-8 validity for public names and topics.
bool valid_utf8_public (const std::string &text_);

//  Node owner id constant.
owner_id_t node_owner ();
owner_id_t spot_owner (const rid_bytes_t &rid_, uint64_t generation_);
owner_id_t actor_owner (const std::string &id_, uint64_t generation_);

//  Seals (node pointer serial, entry serial) into an opaque token wordset
//  and back. The node generation guards staleness.
void seal_claim (const claim_body_t &body_, zlink_mesh_claim_t *out_);
int unseal_claim (const zlink_mesh_claim_t *claim_, claim_body_t *out_);

void seal_reply_token (mesh_node_t *node_, uint64_t serial_, zlink_mesh_reply_token_t *out_);
int unseal_reply_token (const zlink_mesh_reply_token_t *token_,
                        mesh_node_t **node_out_,
                        uint64_t *serial_out_);

//  Actor internals shared with the wire ingress (defined in
//  mesh_actor_api.cpp).
//  Destroys a live local actor: state removal + DESTROYED control record.
int actor_destroy_local (mesh_node_t *node_, const zlink_actor_ref_t *actor_);
//  Admits a remote-origin join control record into the target Spot lane.
//  entry_ selects the target node's entry Spot. Returns 0 or -1 with errno.
int actor_admit_remote_join (mesh_node_t *node_,
                             const rid_bytes_t &origin_rid_,
                             uint64_t origin_generation_,
                             uint64_t origin_correlation_,
                             const zlink_actor_ref_t &actor_,
                             bool entry_,
                             const rid_bytes_t &spot_rid_,
                             uint64_t spot_generation_,
                             std::vector<zlink_msg_t> *creation_parts_);
//  Source-side commit for a remote join reply: applies membership on accept
//  and delivers the operation completion.
void actor_apply_remote_join_reply (mesh_node_t *node_,
                                    const pending_operation_t &op_,
                                    uint32_t join_result_,
                                    const rid_bytes_t &spot_node_rid_,
                                    const rid_bytes_t &spot_rid_,
                                    uint64_t spot_generation_);
//  Fills a wire lookup reply for a local actor by id. Returns 0 with the
//  location fields or -1 with errno (ENOENT).
int actor_lookup_local (mesh_node_t *node_,
                        const std::string &actor_id_,
                        zlink_actor_ref_t *ref_out_,
                        rid_bytes_t *spot_rid_out_,
                        uint64_t *spot_generation_out_,
                        uint64_t *membership_epoch_out_);

//  Schedules the ETIMEDOUT terminal completion for a pending operation
//  (defined in mesh_messaging_api.cpp). timeout 0 schedules nothing.
int schedule_operation_timeout (mesh_node_t *node_, uint64_t operation_low_, uint32_t timeout_ms_);

//  Transfer wire handlers (defined in mesh_transfer_api.cpp). The ingress
//  thread calls these with decoded envelope fields.
void transfer_handle_ready (mesh_node_t *node_,
                            const rid_bytes_t &source_rid_,
                            const zlink_actor_transfer_id_t &transfer_id_,
                            const zlink_actor_ref_t &actor_,
                            uint64_t expected_epoch_,
                            uint64_t final_sequence_,
                            uint8_t role_);
void transfer_handle_data (mesh_node_t *node_,
                           const rid_bytes_t &source_rid_,
                           const zlink_actor_transfer_id_t &transfer_id_,
                           uint64_t sequence_,
                           uint64_t relay_serial_,
                           std::unique_ptr<queued_record_t> *record_);
void transfer_handle_ack (mesh_node_t *node_,
                          const rid_bytes_t &source_rid_,
                          const zlink_actor_transfer_id_t &transfer_id_,
                          uint64_t high_water_);
void transfer_handle_reply_relay (mesh_node_t *node_,
                                  const rid_bytes_t &source_rid_,
                                  uint64_t relay_serial_,
                                  std::vector<zlink_msg_t> *parts_);

//  Delivers reply parts through an unconsumed local reply route serial (the
//  relay path for transferred requests; defined in mesh_dispatch_api.cpp).
int deliver_reply_via_route (mesh_node_t *node_, uint64_t serial_, std::vector<zlink_msg_t> *parts_);

//  Builds a new operation id and registers the pending operation.
zlink_mesh_operation_id_t register_operation (mesh_node_t *node_,
                                              zlink_mesh_operation_kind_t kind_,
                                              const owner_id_t &requester_,
                                              uint32_t timeout_ms_);
}
}

#endif
