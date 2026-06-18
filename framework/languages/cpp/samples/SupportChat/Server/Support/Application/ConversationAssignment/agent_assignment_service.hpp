/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "agent_availability_directory.hpp"

#include <optional>

namespace zlink::samples::supportchat
{

// Picks the next waiting agent for a conversation assignment.
class agent_assignment_service_t
{
  public:
    explicit agent_assignment_service_t (agent_availability_directory_t &availability) :
        _availability (availability)
    {
    }

    std::optional<available_agent_t> assign_next_agent () { return _availability.take_next (); }

  private:
    agent_availability_directory_t &_availability;
};

} // namespace zlink::samples::supportchat
