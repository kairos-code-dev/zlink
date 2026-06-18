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
    void set_available (const std::string &actor_id, std::string display_name, bool is_available)
    {
        if (!is_available) {
            _actor_ids.erase (actor_id);
            return;
        }
        if (_actor_ids.insert (actor_id).second) {
            _available.push_back (available_agent_t{actor_id, std::move (display_name)});
        }
    }

    std::optional<available_agent_t> take_next ()
    {
        while (!_available.empty ()) {
            auto candidate = _available.front ();
            _available.pop_front ();
            if (_actor_ids.erase (candidate.actor_id) > 0) {
                return candidate;
            }
        }
        return std::nullopt;
    }

  private:
    std::deque<available_agent_t> _available;
    std::set<std::string> _actor_ids;
};

} // namespace zlink::samples::supportchat
