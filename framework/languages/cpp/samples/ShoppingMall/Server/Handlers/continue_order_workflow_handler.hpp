/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../shopping_mall_server_role.hpp"

#include <zlink/framework.hpp>

namespace zlink::samples::shoppingmall
{

using namespace framework;

class continue_order_workflow_handler_t
{
  public:
    using request_type = continue_order_workflow_req_t;
    using reply_type = continue_order_workflow_res_t;
    using dependency_types = dependency_list_t<shopping_mall_server_role_t>;
    static constexpr const char *topic_name = "ContinueOrderWorkflowReq";

    explicit continue_order_workflow_handler_t (shopping_mall_server_role_t &server) :
        _server (server)
    {
    }

    continue_order_workflow_res_t handle (const continue_order_workflow_req_t &request)
    {
        return _server.continue_workflow (request.order_id);
    }

  private:
    shopping_mall_server_role_t &_server;
};

} // namespace zlink::samples::shoppingmall
