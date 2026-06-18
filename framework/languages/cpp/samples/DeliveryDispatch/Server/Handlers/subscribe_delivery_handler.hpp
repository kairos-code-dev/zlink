/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../delivery_dispatch_server_role.hpp"

#include <zlink/framework.hpp>

namespace zlink::samples::deliverydispatch
{

class subscribe_delivery_handler_t
{
  public:
    using request_type = subscribe_delivery_req_t;
    using reply_type = subscribe_delivery_accepted_t;
    using dependency_types = zlink::framework::dependency_list_t<delivery_dispatch_server_role_t>;
    static constexpr const char *topic_name = "SubscribeDelivery";

    explicit subscribe_delivery_handler_t (delivery_dispatch_server_role_t &server) : _server (server)
    {
    }

    subscribe_delivery_accepted_t handle (const subscribe_delivery_req_t &request)
    {
        return _server.subscribe_delivery (request.delivery_id);
    }

  private:
    delivery_dispatch_server_role_t &_server;
};

} // namespace zlink::samples::deliverydispatch
