/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../../../Configuration/sample_names.hpp"
#include "../../../../../Shared/Contracts/messages.hpp"
#include "../Actors/support_actor_directory.hpp"
#include "../Actors/support_user_actor_factory.hpp"

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
        support_user_actor_factory_t::remember_identity (
          request.actor_id, request.display_name, request.role);
        if (support_actor_directory_t::shared ().contains (request.actor_id)) {
            auto &actor = support_actor_directory_t::shared ().get (request.actor_id);
            actor.set_identity (request.display_name, request.role);
            return ensure_support_user_actor_res_t{
              actor_ref_snapshot_t{actor.actor.node_rid, actor.actor.actor_id, actor.actor.generation},
              sample_names_t::support_actor_type};
        }
        auto &generation = generation_for (request.actor_id);
        if (generation == 0) {
            generation = ++_next_generation;
        }
        ensure_support_user_actor_res_t response;
        response.actor = actor_ref_snapshot_t{sample_names_t::support_spot_node,
                                              request.actor_id,
                                              generation};
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
