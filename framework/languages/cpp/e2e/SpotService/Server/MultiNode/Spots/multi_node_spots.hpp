/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../../Shared/spot_service_contracts.hpp"
#include "../../Shared/scenario_state.hpp"

#include <zlink/framework.hpp>

#include <string>

namespace e2e = zlink::framework::e2e::spot_service;

template <const char *NodeName> class multi_node_spot_t : public zlink::framework::spot_t
{
  public:
    explicit multi_node_spot_t (scenario_state_t &state) : _state (state) {}

    void configure (zlink::framework::spot_context_t &context)
    {
        _context = context;
        context.handlers ().add_handler<&multi_node_spot_t::state_request> ("StateReq");
    }

    void on_initialize ()
    {
        _state.record ("MultiSpotInitialized", {}, std::string (_context.spot_rid ().value ()));
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

  private:
    scenario_state_t &_state;
    zlink::framework::spot_context_t _context;
    int _value = 0;
    int _sequence = 0;
};

inline constexpr char multi_node_a_name[] = "multi-a";
inline constexpr char multi_node_b_name[] = "multi-b";

using multi_node_spot_a_t = multi_node_spot_t<multi_node_a_name>;
using multi_node_spot_b_t = multi_node_spot_t<multi_node_b_name>;
