/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <deque>
#include <optional>
#include <set>
#include <string>

namespace zlink::samples::supportchat
{

struct available_agent_t
{
    std::string actor_id;
    std::string display_name;
};

// Tracks agent actor ids registered as available for assignment.
class agent_availability_directory_t
{
  public:
    static agent_availability_directory_t &shared ()
    {
        static agent_availability_directory_t directory;
        return directory;
    }

    void set_available (const std::string &actor_id, std::string display_name, bool is_available)
    {
        auto &actor_ids = shared_actor_ids ();
        auto &available = shared_available ();
        if (!is_available) {
            actor_ids.erase (actor_id);
            return;
        }
        if (actor_ids.insert (actor_id).second) {
            available.push_back (available_agent_t{actor_id, std::move (display_name)});
        }
    }

    std::optional<available_agent_t> take_next ()
    {
        auto &actor_ids = shared_actor_ids ();
        auto &available = shared_available ();
        while (!available.empty ()) {
            auto candidate = available.front ();
            available.pop_front ();
            if (actor_ids.erase (candidate.actor_id) > 0) {
                return candidate;
            }
        }
        return std::nullopt;
    }

  private:
    static std::deque<available_agent_t> &shared_available ()
    {
        static std::deque<available_agent_t> values;
        return values;
    }

    static std::set<std::string> &shared_actor_ids ()
    {
        static std::set<std::string> values;
        return values;
    }
};

} // namespace zlink::samples::supportchat
