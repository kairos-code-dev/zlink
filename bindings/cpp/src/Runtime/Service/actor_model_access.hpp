/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_RUNTIME_SERVICE_ACTOR_MODEL_ACCESS_HPP_INCLUDED
#define ZLINK_CPP_RUNTIME_SERVICE_ACTOR_MODEL_ACCESS_HPP_INCLUDED

#include "../Core/routing_id_access.hpp"
#include <zlink/Contracts/Service/actor_models.hpp>

#include <zlink.h>

#include <cstring>
#include <string>

namespace zlink::detail
{

inline std::string fixed_cstr_to_string (const char *src_, size_t capacity_)
{
    const size_t n = ::strnlen (src_, capacity_);
    return std::string (src_, n);
}

struct actor_model_access_t
{
    static actor_ref_t from_native (const zlink_actor_ref_t &native_)
    {
        return actor_ref_t (native_routing_id (native_.node_rid),
                            fixed_cstr_to_string (native_.actor_id, sizeof (native_.actor_id)),
                            native_.generation);
    }

    static zlink_actor_ref_t to_native (const actor_ref_t &actor_) noexcept
    {
        zlink_actor_ref_t out;
        std::memset (&out, 0, sizeof (out));
        out.node_rid = routing_id_native_value (actor_._node_rid);
        const size_t max = sizeof (out.actor_id) - 1u;
        const size_t n = actor_._actor_id.size () < max ? actor_._actor_id.size () : max;
        if (n > 0)
            std::memcpy (out.actor_id, actor_._actor_id.data (), n);
        out.generation = actor_._generation;
        return out;
    }

    static const zlink_actor_ref_t *native_ptr (const actor_ref_t &actor_) noexcept
    {
        thread_local zlink_actor_ref_t native[8];
        thread_local size_t index = 0;
        zlink_actor_ref_t &slot = native[index++ % 8u];
        slot = to_native (actor_);
        return &slot;
    }

    static actor_location_t from_native (const zlink_actor_location_t &native_)
    {
        actor_location_t out;
        out.actor = from_native (native_.actor);
        out.spot_rid = native_routing_id (native_.spot_rid);
        out.spot_generation = native_.spot_generation;
        out.membership_epoch = native_.membership_epoch;
        return out;
    }
};

inline const zlink_actor_ref_t *actor_ref_native (const actor_ref_t &actor_) noexcept
{
    return actor_model_access_t::native_ptr (actor_);
}

} // namespace zlink::detail

#endif
