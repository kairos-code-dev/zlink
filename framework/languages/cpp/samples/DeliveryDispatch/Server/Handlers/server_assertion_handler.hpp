/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../delivery_dispatch_server_role.hpp"

#include <zlink/framework.hpp>

namespace zlink::samples::deliverydispatch
{

class server_assertion_handler_t
{
  public:
    using request_type = server_assertion_req_t;
    using reply_type = server_assertion_res_t;
    using dependency_types = zlink::framework::dependency_list_t<delivery_dispatch_server_role_t>;
    static constexpr const char *topic_name = "ServerAssertionReq";

    explicit server_assertion_handler_t (delivery_dispatch_server_role_t &server) : _server (server)
    {
    }

    server_assertion_res_t handle (const server_assertion_req_t &request)
    {
        return _server.assert_evidence (request.successful_delivery_id,
                                        request.reassigned_delivery_id);
    }

  private:
    delivery_dispatch_server_role_t &_server;
};

} // namespace zlink::samples::deliverydispatch
