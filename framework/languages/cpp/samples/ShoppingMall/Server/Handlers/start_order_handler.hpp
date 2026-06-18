/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../shopping_mall_server_role.hpp"

#include <zlink/framework.hpp>

namespace zlink::samples::shoppingmall
{

class start_order_handler_t
{
  public:
    using request_type = start_order_req_t;
    using reply_type = start_order_res_t;
    using dependency_types = zlink::framework::dependency_list_t<shopping_mall_server_role_t>;
    static constexpr const char *topic_name = "StartOrderReq";

    explicit start_order_handler_t (shopping_mall_server_role_t &server) : _server (server) {}

    start_order_res_t handle (const start_order_req_t &request)
    {
        return _server.start_order (request);
    }

  private:
    shopping_mall_server_role_t &_server;
};

} // namespace zlink::samples::shoppingmall
