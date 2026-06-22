/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "support_user_actor.hpp"

#include <map>
#include <stdexcept>
#include <string>

namespace zlink::samples::supportchat
{

// Keeps the latest known SupportUserActor pointer per actor id so the
// assignment handler can locate an available agent actor by id.
class support_actor_directory_t
{
  public:
    static support_actor_directory_t &shared ()
    {
        static support_actor_directory_t directory;
        return directory;
    }

    void add_or_update (support_user_actor_t &actor) { _actors[actor.actor_id ()] = &actor; }

    support_user_actor_t &get (const std::string &actor_id)
    {
        auto found = _actors.find (actor_id);
        if (found == _actors.end () || found->second == nullptr) {
            throw std::runtime_error ("Support actor is not available. actor=" + actor_id);
        }
        return *found->second;
    }

    bool contains (const std::string &actor_id) const
    {
        auto found = _actors.find (actor_id);
        return found != _actors.end () && found->second != nullptr;
    }

  private:
    std::map<std::string, support_user_actor_t *> _actors;
};

} // namespace zlink::samples::supportchat
