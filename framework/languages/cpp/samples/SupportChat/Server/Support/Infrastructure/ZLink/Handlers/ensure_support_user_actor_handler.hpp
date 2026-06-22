/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../../../Configuration/sample_names.hpp"
#include "../../../../../Shared/Contracts/messages.hpp"

namespace zlink::samples::supportchat
{

// ActorGateway endpoint handler: the Session server asks the Support server to
// create a SupportUserActor or return an existing one. Reconnect reuses the
// same actor id, so the actor ref is stable across binds.
class ensure_support_user_actor_handler_t
{
  public:
    using request_type = ensure_support_user_actor_req_t;
    using reply_type = ensure_support_user_actor_res_t;
    static constexpr const char *topic_name = "EnsureSupportUserActor";

    ensure_support_user_actor_res_t handle (const ensure_support_user_actor_req_t &request)
    {
        auto &generation = generation_for (request.actor_id);
        if (generation == 0) {
            generation = ++_next_generation;
        }
        ensure_support_user_actor_res_t response;
        response.actor = actor_ref_snapshot_t{{}, request.actor_id, generation};
        response.actor_type = sample_names_t::support_actor_type;
        return response;
    }

  private:
    unsigned long long &generation_for (const std::string &actor_id)
    {
        return _generations[actor_id];
    }

    unsigned long long _next_generation = 0;
    std::map<std::string, unsigned long long> _generations;
};

} // namespace zlink::samples::supportchat
