/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../../Shared/spot_service_contracts.hpp"
#include "../../Shared/scenario_state.hpp"

#include <zlink/framework.hpp>

#include <chrono>
#include <optional>
#include <string>
#include <thread>
#include <utility>

namespace e2e = zlink::framework::e2e::spot_service;

inline constexpr char multi_node_a_name[] = "multi-a";
inline constexpr char multi_node_b_name[] = "multi-b";

inline zlink::framework::spot_ref_t multi_node_spot_ref (const std::string &node_rid,
                                                         const std::string &spot_rid)
{
    return zlink::framework::spot_ref_t{
      .mesh_name = e2e::spot_only_mesh,
      .node_rid = zlink::routing_id_t::from (node_rid),
      .spot_rid = zlink::routing_id_t::from (spot_rid)};
}

struct multi_node_actor_t
{
    explicit multi_node_actor_t (std::string actor_id) : actor_id (std::move (actor_id)) {}

    void set_actor_ref (const zlink::framework::actor_ref_t &value)
    {
        actor_ref = value;
        actor_id = std::string (value.actor_id ());
    }

    void set_actor_context (zlink::framework::actor_context_t value)
    {
        context = std::move (value);
    }

    std::string actor_id;
    zlink::framework::actor_ref_t actor_ref;
    zlink::framework::actor_context_t context;
};

struct multi_node_actor_factory_t
{
    multi_node_actor_t create (std::string actor_id) const
    {
        return multi_node_actor_t (std::move (actor_id));
    }
};

template <const char *NodeName> class multi_node_spot_t : public zlink::framework::spot_t
{
  public:
    explicit multi_node_spot_t (scenario_state_t &state) : _state (state) {}

    void configure (zlink::framework::spot_context_t &context)
    {
        _context = context;
        context.handlers ().add_handler<&multi_node_spot_t::state_request> ("StateReq");
        context.handlers ().add_handler<&multi_node_spot_t::state_command> ("StateMsg");
        context.handlers ().add_handler<&multi_node_spot_t::state_command> ("DirectSpotMsg");
        context.handlers ().add_actor_request<&multi_node_spot_t::actor_probe> (
          "SpotOnlyActorProbeReq");
    }

    void on_initialize ()
    {
        _state.record ("MultiSpotInitialized", {}, std::string (_context.spot_rid ().value ()));
    }

    zlink::framework::spot_create_response_t on_create (const zlink::framework::message_t &request)
    {
        if (!request.empty ()) {
            std::optional<e2e::spot_only_mesh_req_t> command;
            try {
                command = request.decode<e2e::spot_only_mesh_req_t> ();
            }
            catch (const std::exception &) {
            }
            if (command) {
                const auto target_node =
                  std::string (NodeName) == multi_node_a_name ? multi_node_b_name
                                                              : multi_node_a_name;
                auto context = _context;
                auto *state = &_state;
                std::thread (
                  [context = std::move (context), state, command = *command, target_node] () mutable {
                      auto reply =
                        request_target_state_with_retry (context, target_node,
                                                         command.target_spot_rid);
                      if (!reply) {
                          return;
                      }
                      context
                        .send_to (
                          multi_node_spot_ref (target_node, command.target_spot_rid),
                          e2e::direct_spot_msg_t{
                            .source_actor_id = command.source_spot_rid,
                            .value = "sm-f6-send-" + command.marker})
                        .packet_name ("StateMsg")
                        .submit ();
                      state->record ("SpotOnlyRequest", {},
                                     std::string (context.spot_rid ().value ()),
                                     "target=" + command.target_spot_rid + "|value="
                                       + std::to_string (reply.value ().value)
                                       + "|marker=" + command.marker);
                  })
                  .detach ();
            }
        }
        return zlink::framework::spot_create_response_t::accept ();
    }

    zlink::framework::spot_actor_join_response_t
    on_actor_join (multi_node_actor_t &actor, const zlink::framework::message_t &)
    {
        _state.record ("SpotActorJoined", actor.actor_id,
                       std::string (_context.spot_rid ().value ()));
        return zlink::framework::spot_actor_join_response_t::accept ();
    }

    e2e::state_res_t state_request (const e2e::state_req_t &request)
    {
        if (request.op == "add") {
            _value += request.amount;
        }
        ++_sequence;
        _state.record ("MultiStateRequest", {}, std::string (_context.spot_rid ().value ()),
                       std::to_string (_value));
        return {.spot_rid = std::string (_context.spot_rid ().value ()),
                .owner_node_rid = NodeName,
                .value = _value,
                .sequence = _sequence};
    }

    void state_command (const e2e::direct_spot_msg_t &request)
    {
        _state.record ("SpotStateCommand", request.source_actor_id,
                       std::string (_context.spot_rid ().value ()), request.value);
    }

    e2e::actor_ping_res_t actor_probe (const multi_node_actor_t &actor,
                                       zlink::framework::spot_actor_request_context_t &,
                                       const e2e::actor_ping_req_t &request)
    {
        return {.actor_id = actor.actor_id,
                .node_rid = NodeName,
                .spot_rid = std::string (_context.spot_rid ().value ()),
                .value = request.value,
                .seen = 1};
    }

  private:
    static zlink::framework::result_t<e2e::state_res_t>
    request_target_state_with_retry (zlink::framework::spot_context_t &context,
                                     const std::string &target_node,
                                     const std::string &target_spot_rid)
    {
        const auto deadline = std::chrono::steady_clock::now () + std::chrono::seconds (10);
        zlink::framework::result_t<e2e::state_res_t> last =
          zlink::framework::result_t<e2e::state_res_t>::failure (
            zlink::framework::framework_error_kind_t::request_failed,
            "spot-only StateReq was not attempted");
        do {
            last =
              context
                .request_to<e2e::state_res_t> (
                  multi_node_spot_ref (target_node, target_spot_rid),
                  e2e::state_req_t{.op = "add", .amount = 7})
                .packet_name ("StateReq")
                .timeout (std::chrono::milliseconds (3000))
                .async ()
                .result ();
            if (last) {
                return last;
            }
            std::this_thread::sleep_for (std::chrono::milliseconds (100));
        } while (std::chrono::steady_clock::now () < deadline);

        return last;
    }

    scenario_state_t &_state;
    zlink::framework::spot_context_t _context;
    int _value = 0;
    int _sequence = 0;
};

using multi_node_spot_a_t = multi_node_spot_t<multi_node_a_name>;
using multi_node_spot_b_t = multi_node_spot_t<multi_node_b_name>;

class multi_node_entry_spot_t : public zlink::framework::entry_spot_t
{
  public:
    explicit multi_node_entry_spot_t (scenario_state_t &state) : _state (state) {}

    void configure (zlink::framework::entry_spot_context_t &context)
    {
        _context = context;
        context.handlers ().add_actor_request<&multi_node_entry_spot_t::join_spot_only> (
          "SpotOnlyJoinReq");
    }

    void configure (zlink::framework::spot_context_t &context)
    {
        zlink::framework::entry_spot_context_t entry_context (context);
        configure (entry_context);
    }

    zlink::framework::task_t<e2e::spot_only_join_res_t>
    join_spot_only (multi_node_actor_t &actor,
                    zlink::framework::spot_actor_request_context_t &,
                    const e2e::spot_only_join_req_t &request)
    {
        if (request.actor_id != actor.actor_id) {
            throw zlink::framework::framework_exception_t (
              zlink::framework::framework_error_kind_t::request_protocol_error,
              "spot-only join request actor does not match dispatched actor");
        }
        auto joined =
          co_await actor.context
            .join_spot (zlink::framework::spot_rid_t::from_string (request.target_spot_rid),
                        zlink::framework::message_t {})
            .async ();
        const auto accepted = joined.result_code == 0 && joined.actor.has_value ();
        _state.record ("SpotOnlyActorJoin", actor.actor_id, request.target_spot_rid,
                       "accepted=" + std::string (accepted ? "true" : "false")
                         + "|marker=" + request.marker);
        co_return e2e::spot_only_join_res_t{.target_spot_rid = request.target_spot_rid,
                                            .actor_id = actor.actor_id,
                                            .accepted = accepted,
                                            .marker = request.marker};
    }

  private:
    scenario_state_t &_state;
    zlink::framework::entry_spot_context_t _context;
};
