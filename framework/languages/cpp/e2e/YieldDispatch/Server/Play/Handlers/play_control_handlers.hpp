/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Support/play_support.hpp"

#include <zlink/framework.hpp>

#include <atomic>
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
                                          zlink::framework::spot_node_manager_t>;
    using request_type = bind_yield_actors_req_t;
    using reply_type = bind_yield_actors_res_t;

    bind_yield_actors_handler_t (evidence_store_t &evidence,
                                 zlink::framework::spot_node_manager_t &spots) :
        _evidence (evidence), _spots (spots)
    {
    }

    bind_yield_actors_res_t handle (
      const bind_yield_actors_req_t &request,
      const zlink::framework::route_handler_context_t &)
    {
        const auto spot_rid = zlink::framework::spot_rid_t::from_string (request.spot_rid);
        (void) _spots.get_or_create_spot (probe_spot_name, spot_rid);
        bind_yield_actors_res_t reply{.spot_rid = request.spot_rid};
        for (const auto &actor_id : request.actor_ids) {
            auto actor_ref = zlink::framework::actor_ref_t (
              zlink::framework::node_rid_t::from_string (_evidence.node_rid), actor_type,
              actor_id, 0);
            if (auto current = _spots.current_actor_ref (actor_ref)) {
                actor_ref = *current;
            } else {
                actor_ref = zlink::framework::actor_ref_t (
                  zlink::framework::node_rid_t::from_string (_evidence.node_rid),
                  actor_type, actor_id, ++_generation);
            }
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
    static inline std::atomic_uint64_t _generation{0};
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
