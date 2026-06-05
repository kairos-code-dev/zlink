/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "spot.hpp"

namespace zlink
{
namespace service
{

/// @brief A handle to a local actor: join/leave spots, receive messages, and send to its bound session.
class actor_t
{
  public:
    ~actor_t ();

    actor_t (actor_t &&other) noexcept;

    actor_t &operator= (actor_t &&other) noexcept;

    actor_t (const actor_t &) = delete;
    actor_t &operator= (const actor_t &) = delete;

    bool valid () const noexcept { return _active; }

    actor_ref_t ref () const { return _ref; }

    void close (std::chrono::milliseconds timeout_ = {});

    actor_join_operation_t join (spot_t &spot_)
    {
        return _node->join_actor (_ref, _node->routing_id (), spot_.routing_id ());
    }

    actor_leave_operation_t leave (spot_t &spot_) { return _node->leave_actor (_ref, spot_.routing_id ()); }

    std::optional<actor_part_t> recv_part (recv_flags_t flags_ = recv_flags_t::none)
    {
        return _node->recv_actor_part (_ref, flags_);
    }

    send_operation_t send_bound_session () { return _node->send_bound_session_msg (_ref); }

    void close_bound_session (std::chrono::milliseconds timeout_ = {});

  private:
    actor_t (spot_node_t &node_, const std::string &actor_id_);

    friend class spot_node_t;

    spot_node_t *_node;
    actor_ref_t _ref;
    bool _active;
    int _last_error;
};

} // namespace service
} // namespace zlink
