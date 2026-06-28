/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../delivery_dispatch_server_role.hpp"

#include <zlink/framework.hpp>

namespace zlink::samples::deliverydispatch
{

using namespace framework;

class advance_delivery_handler_t
{
  public:
    using request_type = advance_delivery_req_t;
    using reply_type = delivery_state_t;
    using dependency_types = dependency_list_t<delivery_dispatch_server_role_t>;
    static constexpr const char *topic_name = "AdvanceDeliveryReq";

    explicit advance_delivery_handler_t (delivery_dispatch_server_role_t &server) : _server (server)
    {
    }

    delivery_state_t handle (const advance_delivery_req_t &request)
    {
        return _server.advance (request.delivery_id, request.status, request.courier_id);
    }

  private:
    delivery_dispatch_server_role_t &_server;
};

} // namespace zlink::samples::deliverydispatch
