/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Handlers/event_notify_handler.hpp"

namespace zlink::framework::e2e::pubsub::server::subscriber
{

class evidence_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<evidence_store_t>;

    explicit evidence_handler_t (evidence_store_t &state) : _state (state) {}

    zlink::framework::http_response_t handle (const zlink::framework::http_request_t &)
    {
        zlink::framework::http_response_t response;
        response.body = nlohmann::json (_state.snapshot ()).dump ();
        return response;
    }

  private:
    evidence_store_t &_state;
};

} // namespace zlink::framework::e2e::pubsub::server::subscriber
