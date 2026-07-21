/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Core/routing_id.hpp"
#include "../Errors/results.hpp"
#include "../Messaging/message.hpp"
#include "../Sockets/results.hpp"
#include "actor_models.hpp"
#include "dispatch.hpp"
#include "mesh_node_models.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace zlink
{
namespace service
{

class mesh_node_t;

namespace detail
{
struct actor_transfer_access_t;
} // namespace detail

/// @brief The admission outcome of an actor join.
enum class actor_join_result_t : int
{
    accepted = 0,
    rejected = 1
};

/// @brief Which side of a cross-node actor transfer a fence operation drives.
enum class actor_transfer_role_t : int
{
    source = 1,
    target = 2
};

/// @brief The phase a transfer control record reports.
enum class actor_transfer_phase_t : int
{
    preparing = 1,
    fenced    = 2,
    committed = 3,
    activated = 4,
    aborted   = 5
};

/// @brief Identifies one in-flight cross-node actor transfer.
struct actor_transfer_id_t
{
    uint64_t high = 0;
    uint64_t low = 0;
};

/// @brief Inputs to the prepare phase of a cross-node actor transfer fence.
struct actor_transfer_prepare_t
{
    actor_transfer_prepare_t () : actor (), peer_node_rid (zlink::detail::unchecked_empty_routing_id ()) {}

    actor_transfer_role_t role = actor_transfer_role_t::source;
    actor_transfer_id_t transfer_id;
    actor_ref_t actor;
    uint64_t expected_membership_epoch = 0;
    routing_id_t peer_node_rid;
    uint64_t final_sequence = 0;
    uint64_t reserve_message_count = 0;
    uint64_t reserve_byte_count = 0;
};

/// @brief The negotiated result reported by the prepare phase.
struct actor_transfer_prepare_result_t
{
    actor_transfer_prepare_result_t () : actor () {}

    actor_transfer_role_t role = actor_transfer_role_t::source;
    actor_transfer_id_t transfer_id;
    actor_ref_t actor;
    uint64_t final_sequence = 0;
    uint64_t reserve_message_count = 0;
    uint64_t reserve_byte_count = 0;
};

/// @brief RAII handle to a fenced transfer. Obtained from actor_transfer_prepare();
///        drive it through commit()+activate() to finish, or abort() to release.
///        A still-pending token is aborted on destruction so a fence is never
///        left dangling.
class actor_transfer_token_t
{
  public:
    actor_transfer_token_t () noexcept;
    ~actor_transfer_token_t ();

    actor_transfer_token_t (actor_transfer_token_t &&other_) noexcept;
    actor_transfer_token_t &operator= (actor_transfer_token_t &&other_) noexcept;

    actor_transfer_token_t (const actor_transfer_token_t &) = delete;
    actor_transfer_token_t &operator= (const actor_transfer_token_t &) = delete;

    bool valid () const noexcept { return _valid; }

    /// @brief Commits the fenced transfer at @p new_membership_epoch_.
    config_result_t commit (uint64_t new_membership_epoch_);
    /// @brief Activates the committed transfer; the token is spent on success.
    config_result_t activate ();
    /// @brief Aborts the transfer and releases the fence; the token is spent.
    config_result_t abort ();

  private:
    std::array<uint64_t, 8> _opaque;
    bool _valid;
    friend struct detail::actor_transfer_access_t;
};

/// @brief Prepares (fences) a cross-node actor transfer. On success @p token_out_
///        holds the fence and @p result_out_ the negotiated reservation.
request_result_t actor_transfer_prepare (mesh_node_t &node_,
                                         const actor_transfer_prepare_t &prepare_,
                                         std::chrono::milliseconds timeout_,
                                         actor_transfer_token_t &token_out_,
                                         actor_transfer_prepare_result_t &result_out_);

/// @brief A handle to a local actor: join/leave spots, message peers, and manage
///        its bound STREAM session. Non-owning view onto its hosting mesh node.
class actor_t
{
  public:
    actor_t () noexcept;
    ~actor_t ();

    actor_t (actor_t &&other_) noexcept;
    actor_t &operator= (actor_t &&other_) noexcept;

    actor_t (const actor_t &) = delete;
    actor_t &operator= (const actor_t &) = delete;

    bool valid () const noexcept { return _active; }

    const actor_ref_t &ref () const noexcept { return _ref; }

    // Spot membership.
    submit_result_t join_spot (const routing_id_t &target_node_rid_,
                               const routing_id_t &target_spot_rid_,
                               uint64_t target_spot_generation_,
                               const std::vector<message_t> &creation_parts_,
                               operation_id_t &operation_id_out_,
                               std::chrono::milliseconds timeout_ = {});
    submit_result_t join_entry_spot (const routing_id_t &target_node_rid_,
                                     const std::vector<message_t> &creation_parts_,
                                     operation_id_t &operation_id_out_,
                                     std::chrono::milliseconds timeout_ = {});
    submit_result_t leave_spot (uint64_t expected_membership_epoch_,
                                operation_id_t &operation_id_out_,
                                std::chrono::milliseconds timeout_ = {});

    // Peer messaging (this actor as source). Parts are borrowed read-only.
    submit_result_t send_to (const actor_ref_t &target_actor_,
                             const std::vector<message_t> &parts_,
                             send_flags_t flags_ = send_flags_t::none,
                             mesh_metadata_t metadata_ = {});
    submit_result_t request_to (const actor_ref_t &target_actor_,
                                const std::vector<message_t> &parts_,
                                operation_id_t &operation_id_out_,
                                send_flags_t flags_ = send_flags_t::none,
                                std::chrono::milliseconds timeout_ = {},
                                mesh_metadata_t metadata_ = {});

    // Bound STREAM session.
    /// @brief Sends only to the specified nonzero binding generation. A stale
    ///        generation is rejected instead of selecting a replacement binding.
    submit_result_t send_bound_session (uint64_t expected_binding_generation_,
                                        const std::vector<message_t> &parts_,
                                        send_flags_t flags_ = send_flags_t::none);
    submit_result_t close_bound_session (uint64_t expected_binding_generation_,
                                         operation_id_t &operation_id_out_,
                                         std::chrono::milliseconds timeout_ = {});

    // Lifecycle.
    submit_result_t destroy (operation_id_t &operation_id_out_,
                             std::chrono::milliseconds timeout_ = {});

  private:
    actor_t (mesh_node_t &node_, const actor_ref_t &ref_) noexcept;

    friend class mesh_node_t;

    mesh_node_t *_node;
    actor_ref_t _ref;
    bool _active;
    int _last_error;
};

/// @brief Replies to an actor-join request received via dispatch.
submit_result_t actor_join_reply (const reply_token_t &token_,
                                  actor_join_result_t join_result_,
                                  const std::vector<message_t> &parts_,
                                  send_flags_t flags_ = send_flags_t::none);

} // namespace service
} // namespace zlink
