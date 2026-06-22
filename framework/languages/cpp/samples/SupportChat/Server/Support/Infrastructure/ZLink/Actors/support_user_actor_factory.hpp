/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "support_user_actor.hpp"

#include <map>
#include <mutex>
#include <utility>

namespace zlink::samples::supportchat
{

struct support_user_actor_factory_t
{
    support_user_actor_t create (std::string actor_id) const
    {
        auto actor = create (actor_ref_snapshot_t{"", std::move (actor_id), 0});
        const std::lock_guard lock (identities_mutex ());
        auto found = identities ().find (actor.actor_id ());
        if (found != identities ().end ()) {
            actor.set_identity (found->second.display_name, found->second.role);
        }
        return actor;
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

    static void remember_identity (std::string actor_id, std::string display_name, std::string role)
    {
        const std::lock_guard lock (identities_mutex ());
        identities ()[std::move (actor_id)] =
          identity_t{std::move (display_name), std::move (role)};
    }

  private:
    struct identity_t
    {
        std::string display_name;
        std::string role;
    };

    static std::map<std::string, identity_t> &identities ()
    {
        static std::map<std::string, identity_t> values;
        return values;
    }

    static std::mutex &identities_mutex ()
    {
        static std::mutex mutex;
        return mutex;
    }
};

} // namespace zlink::samples::supportchat
