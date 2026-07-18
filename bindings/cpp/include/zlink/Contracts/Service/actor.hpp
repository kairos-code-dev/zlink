/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Core/routing_id.hpp"
#include "../Messaging/message.hpp"
#include "../Sockets/results.hpp"
#include "actor_models.hpp"
#include "dispatch.hpp"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace zlink
{
namespace service
{

class mesh_node_t;

/// @brief The admission outcome of an actor join.
enum class actor_join_result_t : int
{
    accepted = 0,
    rejected = 1
};

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
                               std::vector<message_t> &creation_parts_,
                               operation_id_t &operation_id_out_,
                               std::chrono::milliseconds timeout_ = {});
    submit_result_t join_entry_spot (const routing_id_t &target_node_rid_,
                                     std::vector<message_t> &creation_parts_,
                                     operation_id_t &operation_id_out_,
                                     std::chrono::milliseconds timeout_ = {});
    submit_result_t leave_spot (uint64_t expected_membership_epoch_,
                                operation_id_t &operation_id_out_,
                                std::chrono::milliseconds timeout_ = {});

    // Peer messaging (this actor as source).
    submit_result_t send_to (const actor_ref_t &target_actor_,
                             std::vector<message_t> &parts_,
                             send_flags_t flags_ = send_flags_t::none);
    submit_result_t request_to (const actor_ref_t &target_actor_,
                                std::vector<message_t> &parts_,
                                operation_id_t &operation_id_out_,
                                send_flags_t flags_ = send_flags_t::none,
                                std::chrono::milliseconds timeout_ = {});

    // Bound STREAM session.
    submit_result_t send_bound_session (std::vector<message_t> &parts_,
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
                                  std::vector<message_t> &parts_,
                                  send_flags_t flags_ = send_flags_t::none);

} // namespace service
} // namespace zlink
