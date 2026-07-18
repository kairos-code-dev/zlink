/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Core/routing_id.hpp"
#include "../Messaging/message.hpp"
#include "../Messaging/request_result.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace zlink
{

namespace detail
{
struct actor_model_access_t;
} // namespace detail

/// @brief The kind of spot an actor is placed on.
enum class spot_kind : int
{
    invalid = 0,
    entry = 1,
    user = 2
};

/// @brief References an actor: the node hosting it, its id, and its generation.
class actor_ref_t
{
  public:
    actor_ref_t () noexcept :
        _node_rid (detail::unchecked_empty_routing_id ()), _actor_id (), _generation (0)
    {
    }

    routing_id_t node_rid () const { return _node_rid; }

    std::string actor_id () const { return _actor_id; }

    uint64_t generation () const noexcept { return _generation; }

    bool unchecked () const noexcept { return _generation == 0u; }

  private:
    actor_ref_t (routing_id_t node_rid_, std::string actor_id_, uint64_t generation_) noexcept :
        _node_rid (std::move (node_rid_)),
        _actor_id (std::move (actor_id_)),
        _generation (generation_)
    {
    }

    routing_id_t _node_rid;
    std::string _actor_id;
    uint64_t _generation;

    friend struct detail::actor_model_access_t;
};

/// @brief The resolved placement of an actor on the mesh.
struct actor_location_t
{
    actor_location_t () :
        actor (),
        spot_rid (detail::unchecked_empty_routing_id ()),
        spot_generation (0),
        membership_epoch (0)
    {
    }

    actor_ref_t actor;
    routing_id_t spot_rid;
    uint64_t spot_generation;
    uint64_t membership_epoch;

  private:
    friend struct detail::actor_model_access_t;
};

} // namespace zlink
