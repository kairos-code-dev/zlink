/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Support/play_support.hpp"

#include <zlink/framework.hpp>

#include <string>

namespace zlink::framework::e2e::yield_dispatch::server::play
{

class ensure_spot_handler_t
{
  public:
    using dependency_types =
      zlink::framework::dependency_list_t<evidence_store_t,
                                          zlink::framework::spot_node_manager_t>;
    using request_type = ensure_spot_req_t;
    using reply_type = ensure_spot_res_t;

    ensure_spot_handler_t (evidence_store_t &evidence,
                           zlink::framework::spot_node_manager_t &spots) :
        _evidence (evidence), _spots (spots)
    {
    }

    ensure_spot_res_t handle (const ensure_spot_req_t &request,
                                const zlink::framework::route_handler_context_t &)
    {
        try {
            const auto rid = zlink::framework::spot_rid_t::from_string (request.spot_rid);
            const auto created = _spots.get_or_create_spot (probe_spot_name, rid);
            _evidence.add ("spot-ensured|rid=" + _evidence.node_rid + "|spot="
                           + std::string (created.spot_rid.value ()) + "|request="
                           + request.spot_rid);
            return {.spot_rid = std::string (created.spot_rid.value ()),
                    .node_rid = _evidence.node_rid};
        }
        catch (const zlink::framework::framework_exception_t &) {
            throw;
        }
        catch (const std::exception &error) {
            throw zlink::framework::framework_exception_t (
              zlink::framework::framework_error_kind_t::request_failed,
              std::string ("ensure spot failed: ") + error.what ());
        }
    }

  private:
    evidence_store_t &_evidence;
    zlink::framework::spot_node_manager_t &_spots;
};

class bind_yield_actors_handler_t
{
  public:
    using dependency_types =
      zlink::framework::dependency_list_t<evidence_store_t,
                                          zlink::framework::spot_node_manager_t,
                                          zlink::framework::session_actor_manager_t>;
    using request_type = bind_yield_actors_req_t;
    using reply_type = bind_yield_actors_res_t;

    bind_yield_actors_handler_t (evidence_store_t &evidence,
                                 zlink::framework::spot_node_manager_t &spots,
                                 zlink::framework::session_actor_manager_t &actors) :
        _evidence (evidence), _spots (spots), _actors (actors)
    {
    }

    bind_yield_actors_res_t handle (
      const bind_yield_actors_req_t &request,
      const zlink::framework::route_handler_context_t &)
    {
        const auto spot_rid = zlink::framework::spot_rid_t::from_string (request.spot_rid);
        (void) _spots.get_or_create_spot (probe_spot_name, spot_rid);
        _evidence.add ("bind-start|rid=" + _evidence.node_rid + "|spot=" + request.spot_rid
                       + "|actors=" + std::to_string (request.actor_ids.size ()));
        bind_yield_actors_res_t reply{.spot_rid = request.spot_rid};
        for (const auto &actor_id : request.actor_ids) {
            auto actor = _actors.get_or_create (actor_type, actor_id);
            if (!actor) {
                throw zlink::framework::framework_exception_t (
                  actor.error_kind (),
                  actor.error () ? actor.error ()->what () : "actor get or create failed");
            }
            auto bound = _actors.bind_or_get (actor.value ().ref ()).async ().result ();
            if (!bound) {
                throw zlink::framework::framework_exception_t (
                  bound.error_kind (),
                  bound.error () ? bound.error ()->what () : "actor bind failed");
            }
            auto joined =
              bound.value ()
                .context ()
                .join_spot (spot_rid,
                            delay_req_t{.request_id = "bind-" + actor_id,
                                        .delay_ms = 0,
                                        .marker = "bind"})
                .timeout (std::chrono::milliseconds (3000))
                .template async<delay_res_t> ()
                .result ();
            if (!joined) {
                throw zlink::framework::framework_exception_t (
                  joined.error_kind (),
                  joined.error () ? joined.error ()->what () : "actor join failed");
            }
            if (joined.value ().result_code != 0) {
                throw zlink::framework::framework_exception_t (
                  zlink::framework::framework_error_kind_t::request_failed,
                  "yield actor join was rejected: " + actor_id);
            }
            auto actor_ref = joined.value ().actor.value ();
            _evidence.add ("bind-actor|rid=" + _evidence.node_rid + "|spot="
                           + request.spot_rid + "|actor=" + actor_id + "|generation="
                           + std::to_string (actor_ref.generation ()));
            reply.actors.push_back (
              {.actor_id = actor_id,
               .node_rid = std::string (actor_ref.node_rid ().value ()),
               .generation = actor_ref.generation ()});
        }
        return reply;
    }

  private:
    evidence_store_t &_evidence;
    zlink::framework::spot_node_manager_t &_spots;
    zlink::framework::session_actor_manager_t &_actors;
};

class evidence_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<evidence_store_t>;
    using request_type = yield_evidence_req_t;
    using reply_type = yield_evidence_res_t;

    explicit evidence_handler_t (evidence_store_t &evidence) : _evidence (evidence) {}

    yield_evidence_res_t handle (const yield_evidence_req_t &request,
                                   const zlink::framework::route_handler_context_t &)
    {
        return _evidence.snapshot (request.request_id);
    }

  private:
    evidence_store_t &_evidence;
};

class evidence_wait_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<evidence_store_t>;
    using request_type = yield_evidence_wait_req_t;
    using reply_type = yield_evidence_res_t;

    explicit evidence_wait_handler_t (evidence_store_t &evidence) : _evidence (evidence) {}

    yield_evidence_res_t handle (const yield_evidence_wait_req_t &request,
                                   const zlink::framework::route_handler_context_t &)
    {
        return _evidence.wait (request);
    }

  private:
    evidence_store_t &_evidence;
};

} // namespace zlink::framework::e2e::yield_dispatch::server::play
