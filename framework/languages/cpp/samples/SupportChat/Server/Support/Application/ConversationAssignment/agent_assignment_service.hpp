/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include <optional>
#include <string>
#include <unordered_map>

namespace zlink::samples::supportchat
{

class agent_availability_directory_t
{
  public:
    void set_available (std::string actor_id, int capacity)
    {
        _capacity[std::move (actor_id)] = capacity;
    }

    void increment_load (const std::string &actor_id)
    {
        ++_load[actor_id];
    }

    void release (const std::string &actor_id)
    {
        auto current = _load.find (actor_id);
        if (current != _load.end () && current->second > 0) {
            --current->second;
        }
    }

    std::optional<std::string> first_available () const
    {
        for (const auto &[actor_id, capacity] : _capacity) {
            const auto current_load = _load.find (actor_id);
            const auto load = current_load != _load.end () ? current_load->second : 0;
            if (load < capacity) {
                return actor_id;
            }
        }
        return std::nullopt;
    }

  private:
    std::unordered_map<std::string, int> _capacity;
    std::unordered_map<std::string, int> _load;
};

class agent_assignment_service_t
{
  public:
    explicit agent_assignment_service_t (agent_availability_directory_t &directory) :
        _directory (directory)
    {
    }

    std::optional<std::string> assign ()
    {
        auto actor_id = _directory.first_available ();
        if (actor_id) {
            _directory.increment_load (*actor_id);
        }
        return actor_id;
    }

  private:
    agent_availability_directory_t &_directory;
};

} // namespace zlink::samples::supportchat
