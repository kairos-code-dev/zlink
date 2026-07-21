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

//  Instance activation token sealing and target-side admission are shared by
//  the public submit path and wire ingress.
void seal_instance_token (mesh_node_t *node_,
                          uint64_t serial_,
                          zlink_instance_spot_activation_token_t *out_);
int unseal_instance_token (const zlink_instance_spot_activation_token_t *token_,
                           mesh_node_t **node_out_,
                           uint64_t *serial_out_);
int admit_instance_record (mesh_node_t *node_,
                           const instance_placement_value_t &target_,
                           std::unique_ptr<queued_record_t> &record_);
int admit_instance_direct_record (mesh_node_t *node_,
                                  const rid_bytes_t &target_spot_rid_,
                                  uint64_t target_spot_generation_,
                                  std::unique_ptr<queued_record_t> &record_);
//  Arms the call-specific timeout after an Instance request has entered an
//  activation barrier. The helper removes and completes a record that has
//  already expired; records admitted directly to a Ready owner need no task.
int arm_instance_record_deadline (
  mesh_node_t *node_,
  const rid_bytes_t &source_node_rid_,
  const zlink_mesh_operation_id_t &operation_id_);
void terminate_instance_activations (mesh_node_t *node_,
                                     zlink_request_result_t terminal_result_,
                                     int failure_errno_);

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
                                    uint64_t spot_generation_,
                                    std::vector<zlink_msg_t> *reply_parts_);
//  Fills a wire lookup reply for a local actor by id. Returns 0 with the
//  location fields or -1 with errno (ENOENT).
int actor_lookup_local (mesh_node_t *node_,
                        const std::string &actor_id_,
                        zlink_actor_ref_t *ref_out_,
                        rid_bytes_t *spot_rid_out_,
                        uint64_t *spot_generation_out_,
                        uint64_t *membership_epoch_out_);

//  Transfer wire handlers (defined in mesh_transfer_api.cpp). The ingress
//  thread calls these with decoded envelope fields.
void transfer_handle_ready (mesh_node_t *node_,
                            const rid_bytes_t &source_rid_,
                            const zlink_actor_transfer_id_t &transfer_id_,
                            const zlink_actor_ref_t &actor_,
                            uint64_t expected_epoch_,
                            uint64_t final_sequence_,
                            uint8_t role_,
                            uint64_t offered_messages_,
                            uint64_t offered_bytes_,
                            const std::vector<transfer_participant_descriptor_t>
                              &participants_);
void transfer_handle_data (mesh_node_t *node_,
                           const rid_bytes_t &source_rid_,
                           const zlink_actor_transfer_id_t &transfer_id_,
                           uint64_t participant_id_,
                           uint64_t sequence_,
                           uint64_t relay_serial_,
                           std::unique_ptr<queued_record_t> *record_);
void transfer_handle_ack (mesh_node_t *node_,
                          const rid_bytes_t &source_rid_,
                          const zlink_actor_transfer_id_t &transfer_id_,
                          uint64_t participant_id_,
                          uint64_t high_water_);
void transfer_handle_seal (
  mesh_node_t *node_,
  const rid_bytes_t &source_rid_,
  const zlink_actor_transfer_id_t &transfer_id_,
  bool response_,
  const std::vector<transfer_participant_terminal_t> &terminals_);
void transfer_handle_complete (mesh_node_t *node_,
                               const rid_bytes_t &source_rid_,
                               const zlink_actor_transfer_id_t &transfer_id_);
void transfer_handle_reply_relay (mesh_node_t *node_,
                                  const rid_bytes_t &source_rid_,
                                  uint64_t relay_serial_,
                                  int32_t terminal_result_,
                                  int32_t failure_errno_,
                                  std::vector<zlink_msg_t> *parts_);

//  STREAM session transfer hooks. The session service owns binding barriers;
//  the Actor transfer lifecycle only announces authority transitions.
void stream_sessions_fence_actor (mesh_node_t *node_,
                                  const zlink_actor_ref_t &actor_,
                                  uint64_t transfer_serial_);
void stream_sessions_negotiate_actor (
  mesh_node_t *node_,
  const zlink_actor_ref_t &actor_,
  uint64_t transfer_serial_,
  const zlink_actor_transfer_id_t &transfer_id_,
  const rid_bytes_t &target_node_rid_,
  uint64_t offered_messages_,
  uint64_t offered_bytes_,
  std::vector<transfer_participant_descriptor_t> *participants_out_);
void stream_sessions_ack_actor (mesh_node_t *node_,
                                const zlink_actor_ref_t &actor_,
                                uint64_t transfer_serial_,
                                uint64_t participant_id_,
                                uint64_t high_water_);
void stream_sessions_seal_actor (
  mesh_node_t *node_,
  const zlink_actor_ref_t &actor_,
  uint64_t transfer_serial_,
  std::vector<transfer_participant_terminal_t> *terminals_out_);
//  Delivers an accepted remote Actor-to-session frame. During the source
//  transfer fence the binding takes ownership of parts_ in its bounded
//  reverse FIFO; otherwise delivery is immediate.
zlink_submit_result_t stream_sessions_deliver_bound_session (
  mesh_node_t *node_,
  const zlink_actor_ref_t &actor_,
  uint64_t expected_binding_generation_,
  std::vector<zlink_msg_t> *parts_);
//  Replays current physical STREAM bindings to one newly admitted peer.
void stream_sessions_replay_bound_session_bindings (
  mesh_node_t *node_, const rid_bytes_t &peer_rid_);
//  Applies the terminal owner-validation reply for a remote STREAM bind.
//  Success commits the source binding and its completion together; every
//  failure leaves the source binding absent.
void stream_sessions_apply_remote_bind_reply (
  mesh_node_t *node_,
  const pending_operation_t &op_,
  const rid_bytes_t &source_rid_,
  int32_t terminal_result_,
  int32_t failure_errno_,
  uint64_t binding_generation_,
  uint64_t membership_epoch_);
//  Flushes the reverse FIFO before releasing the source binding barrier.
//  Returns 0 or -1 with errno; a failure leaves the barrier and queue intact
//  so source commit can be retried.
int stream_sessions_commit_actor (mesh_node_t *node_,
                                  const zlink_actor_ref_t &actor_,
                                  uint64_t transfer_serial_,
                                  const rid_bytes_t &target_node_rid_,
                                  uint64_t membership_epoch_);
void stream_sessions_abort_actor (mesh_node_t *node_,
                                  const zlink_actor_ref_t &actor_,
                                  uint64_t transfer_serial_);

//  Submits Actor data on behalf of one raw STREAM session while retaining
//  the session routing id in the destination receive record.
zlink_submit_result_t submit_stream_session_to_actor (
  mesh_node_t *node_,
  const rid_bytes_t &source_session_rid_,
  uint64_t source_binding_generation_,
  const zlink_actor_ref_t *target_actor_,
  const zlink_mesh_metadata_view_t *metadata_,
  const zlink_msg_t *parts_,
  size_t part_count_,
  zlink_mesh_operation_id_t *operation_id_out_,
  zlink_send_flags_t flags_,
  uint32_t timeout_ms_);

//  Delivers reply parts through an unconsumed local reply route serial (the
//  relay path for transferred requests; defined in mesh_dispatch_api.cpp).
int deliver_reply_via_route (mesh_node_t *node_,
                             uint64_t serial_,
                             int32_t terminal_result_,
                             int32_t failure_errno_,
                             std::vector<zlink_msg_t> *parts_);
//  Atomically installs a forced terminal on an Instance reply route. If a
//  handler reply already owns the route, success wins and failure transfers
//  that reservation directly to the forced terminal attempt.
int force_reply_via_route (mesh_node_t *node_,
                           uint64_t serial_,
                           int32_t terminal_result_,
                           int32_t failure_errno_);

//  Keeps physical disconnects observed during an unlocked ROUTER send until
//  the sender can bind its operation or relay route to the selected pipe.
bool begin_remote_route_flight (mesh_node_t *node_);
void cancel_remote_route_flight (mesh_node_t *node_);
bool validate_remote_route_flight (mesh_node_t *node_,
                                   const rid_bytes_t &target_rid_,
                                   uint64_t target_generation_,
                                   uint64_t *connection_id_out_);
bool commit_remote_operation_route (
  mesh_node_t *node_,
  const zlink_mesh_operation_id_t &operation_id_,
  uint64_t connection_id_);
bool prepare_remote_reply_route (mesh_node_t *node_,
                                 uint64_t reply_serial_,
                                 const rid_bytes_t &target_rid_);
bool commit_remote_reply_route (mesh_node_t *node_,
                                uint64_t reply_serial_,
                                uint64_t connection_id_);

class remote_route_flight_guard_t
{
  public:
    remote_route_flight_guard_t (mesh_node_t *node_, bool active_) :
        _node (node_),
        _requested (active_),
        _active (active_ && begin_remote_route_flight (node_))
    {
    }

    ~remote_route_flight_guard_t ()
    {
        if (_active)
            cancel_remote_route_flight (_node);
    }

    bool valid () const { return !_requested || _active; }
    void release () { _active = false; }

  private:
    mesh_node_t *_node;
    bool _requested;
    bool _active;

    remote_route_flight_guard_t (const remote_route_flight_guard_t &);
    remote_route_flight_guard_t &operator= (
      const remote_route_flight_guard_t &);
};

//  Owns request bookkeeping until delivery commits. Every early return
//  removes the operation and optional reply route and cancels the prepared
//  timeout; callers therefore cannot leave a partial request behind.
class operation_submission_t
{
  public:
    operation_submission_t (mesh_node_t *node_,
                            bool active_,
                            zlink_mesh_operation_kind_t kind_,
                            const owner_id_t &requester_,
                            uint32_t timeout_ms_);
    ~operation_submission_t ();

    bool valid () const;
    const zlink_mesh_operation_id_t &operation_id () const;
    bool add_reply_route (reply_route_t route_, uint64_t *serial_out_);
    bool set_instance_remote_route (const rid_bytes_t &target_node_rid_);
    void commit_instance_remote_route (uint64_t connection_id_);
    void commit ();

  private:
    mesh_node_t *_node;
    zlink_mesh_operation_id_t _operation_id;
    uint64_t _reply_serial;
    operation_timeout_guard_t *_timeout;
    bool _active;
    bool _valid;
    bool _committed;

    operation_submission_t (const operation_submission_t &);
    operation_submission_t &operator= (const operation_submission_t &);
};
}
}

#endif
