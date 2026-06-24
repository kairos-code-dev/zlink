/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "support_user_actor.hpp"

#include <utility>

namespace zlink::samples::supportchat
{

struct support_user_actor_factory_t
{
    support_user_actor_t create (std::string actor_id) const
    {
        return create (actor_ref_snapshot_t{"", std::move (actor_id), 0});
    }

    support_user_actor_t create (actor_ref_snapshot_t actor, std::string display_name = {},
                                 std::string role = {}) const
    {
        if (display_name.empty ()) {
            display_name = actor.actor_id;
        }
        support_user_actor_t user;
        user.actor = std::move (actor);
        user.display_name = std::move (display_name);
        user.role = std::move (role);
        return user;
    }

};

} // namespace zlink::samples::supportchat
