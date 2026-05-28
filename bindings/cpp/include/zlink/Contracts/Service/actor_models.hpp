/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Core/routing_id.hpp"
#include "../Messaging/message.hpp"
#include "../Messaging/request_result.hpp"

namespace zlink
{

namespace detail
{
struct actor_model_access_t;
} // namespace detail

class actor_ref_t
{
  public:
    actor_ref_t () noexcept
        : _node_rid (detail::unchecked_empty_routing_id ()),
          _actor_id (),
          _generation (0)
    {
    }

    routing_id_t node_rid () const
    {
        return _node_rid;
    }

    std::string actor_id () const
    {
        return _actor_id;
    }

    std::string actor_id_string () const
    {
        return actor_id ();
    }

    uint64_t generation () const noexcept { return _generation; }

    bool unchecked () const noexcept { return _generation == 0u; }

  private:
    actor_ref_t (routing_id_t node_rid_,
                 std::string actor_id_,
                 uint64_t generation_) noexcept
        : _node_rid (std::move (node_rid_)),
          _actor_id (std::move (actor_id_)),
          _generation (generation_)
    {
    }

    routing_id_t _node_rid;
    std::string _actor_id;
    uint64_t _generation;

    friend struct detail::actor_model_access_t;
};

struct actor_recv_info_t
{
    actor_recv_info_t ()
        : actor (),
          source_node_rid (detail::unchecked_empty_routing_id ()),
          source_session_rid (detail::unchecked_empty_routing_id ()),
          flags (0)
    {
    }

    actor_ref_t actor;
    routing_id_t source_node_rid;
    routing_id_t source_session_rid;
    uint32_t flags;

  private:
    friend struct detail::actor_model_access_t;
};

struct actor_join_info_t
{
    actor_join_info_t ()
        : source_actor (),
          target_actor (),
          source_node_rid (detail::unchecked_empty_routing_id ()),
          source_spot_rid (detail::unchecked_empty_routing_id ()),
          target_node_rid (detail::unchecked_empty_routing_id ()),
          target_spot_rid (detail::unchecked_empty_routing_id ()),
          join_epoch (0),
          flags (0),
          _request_token (0)
    {
    }

    actor_ref_t source_actor;
    actor_ref_t target_actor;
    routing_id_t source_node_rid;
    routing_id_t source_spot_rid;
    routing_id_t target_node_rid;
    routing_id_t target_spot_rid;
    uint64_t join_epoch;
    uint32_t flags;

  private:
    std::uintptr_t _request_token;
    friend class service::spot_t;
    friend class service::actor_join_reply_op_t;
    friend struct detail::actor_model_access_t;
};

class actor_join_request_t
{
  public:
    actor_join_request_t (actor_join_info_t info_, message_t message_)
        : _info (std::move (info_)), _message (std::move (message_))
    {
    }

    const actor_join_info_t &info () const noexcept { return _info; }
    message_t &message () noexcept { return _message; }
    const message_t &message () const noexcept { return _message; }

  private:
    actor_join_info_t _info;
    message_t _message;
};

struct actor_join_result_t
{
    actor_join_result_t ()
        : result (request_result_t::ok),
          join_result_code (0),
          actor (),
          joined_spot_rid (detail::unchecked_empty_routing_id ()),
          join_epoch (0),
          flags (0)
    {
    }

    request_result_t result;
    int32_t join_result_code;
    actor_ref_t actor;
    routing_id_t joined_spot_rid;
    uint64_t join_epoch;
    uint32_t flags;

  private:
    friend struct detail::actor_model_access_t;
};

struct actor_join_entry_spot_result_t
{
    actor_join_entry_spot_result_t ()
        : result (request_result_t::ok),
          actor (),
          target_node_rid (detail::unchecked_empty_routing_id ()),
          join_epoch (0),
          flags (0)
    {
    }

    request_result_t result;
    actor_ref_t actor;
    routing_id_t target_node_rid;
    uint64_t join_epoch;
    uint32_t flags;

  private:
    friend struct detail::actor_model_access_t;
};

struct actor_lookup_result_t
{
    actor_lookup_result_t ()
        : result (request_result_t::ok), actor (), flags (0)
    {
    }

    request_result_t result;
    actor_ref_t actor;
    uint32_t flags;

  private:
    friend struct detail::actor_model_access_t;
};

struct spot_actor_lifecycle_info_t
{
    spot_actor_lifecycle_info_t ()
        : previous_actor (),
          current_actor (),
          previous_spot_rid (std::nullopt),
          current_spot_rid (std::nullopt),
          join_epoch (0),
          flags (0)
    {
    }

    actor_ref_t previous_actor;
    actor_ref_t current_actor;
    std::optional<routing_id_t> previous_spot_rid;
    std::optional<routing_id_t> current_spot_rid;
    uint64_t join_epoch;
    uint32_t flags;

  private:
    friend struct detail::actor_model_access_t;
};

enum class spot_actor_lifecycle_event_kind_t : int
{
    joined = 1,
    left = 2
};

struct spot_actor_lifecycle_event_t
{
    spot_actor_lifecycle_event_t ()
        : kind (spot_actor_lifecycle_event_kind_t::joined), info ()
    {
    }

    spot_actor_lifecycle_event_kind_t kind;
    spot_actor_lifecycle_info_t info;

  private:
    friend struct detail::actor_model_access_t;
};

using actor_join_callback_t =
  std::function<void(const actor_join_result_t &, std::vector<message_t>)>;

using actor_join_entry_spot_callback_t =
  std::function<void(const actor_join_entry_spot_result_t &)>;

using actor_lookup_callback_t =
  std::function<void(const actor_lookup_result_t &)>;

enum class spot_kind : int
{
    invalid = 0,
    entry = 1,
    user = 2
};

struct actor_route_t
{
    actor_route_t ()
        : actor (),
          current_spot_rid (std::nullopt),
          current_spot_kind (spot_kind::invalid)
    {
    }

    actor_ref_t actor;
    std::optional<routing_id_t> current_spot_rid;
    spot_kind current_spot_kind;

  private:
    friend struct detail::actor_model_access_t;
};

struct spot_route_t
{
    spot_route_t ()
        : spot_rid (detail::unchecked_empty_routing_id ()),
          owner_node_rid (detail::unchecked_empty_routing_id ()),
          spot_kind_value (spot_kind::invalid)
    {
    }

    spot_kind kind () const noexcept { return spot_kind_value; }

    routing_id_t spot_rid;
    routing_id_t owner_node_rid;
    spot_kind spot_kind_value;

  private:
    friend struct detail::actor_model_access_t;
};

struct actor_part_t
{
    actor_part_t () : info (), part (), has_more (false) {}

    actor_part_t (actor_recv_info_t info_, message_t part_, bool has_more_)
        : info (std::move (info_)),
          part (std::move (part_)),
          has_more (has_more_)
    {
    }

    actor_recv_info_t info;
    message_t part;
    bool has_more;
};

enum class spot_dispatch_event_t : int
{
    subscribe_readable = 1,
    routed_readable = 2,
    timer_readable = 3,
    channel_reply_readable = 4,
    actor_readable = 5,
    actor_join_readable = 6
};

enum class spot_dispatch_subject_kind_t : int
{
    spot = 1,
    timer = 2,
    channel_dealer = 3,
    actor = 4
};

struct spot_dispatch_info_t
{
    spot_dispatch_info_t ()
        : event (spot_dispatch_event_t::subscribe_readable),
          subject_kind (spot_dispatch_subject_kind_t::spot),
          timer (NULL),
          channel_dealer (NULL),
          channel_name (std::nullopt),
          actor (std::nullopt),
          actor_parts ()
    {
    }

    spot_dispatch_event_t event;
    spot_dispatch_subject_kind_t subject_kind;
    timer_t *timer;
    dealer_socket_t *channel_dealer;
    std::optional<std::string> channel_name;
    std::optional<actor_ref_t> actor;
    std::vector<actor_part_t> actor_parts;

  private:
    friend struct detail::actor_model_access_t;
};

} // namespace zlink
