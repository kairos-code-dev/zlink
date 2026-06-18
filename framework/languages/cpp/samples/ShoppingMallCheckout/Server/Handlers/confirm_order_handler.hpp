/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../shopping_mall_checkout_server_role.hpp"

#include <zlink/framework.hpp>

namespace zlink::samples::shoppingmallcheckout
{

class confirm_order_handler_t
{
  public:
    using request_type = confirm_order_req_t;
    using reply_type = checkout_state_t;
    using dependency_types =
      zlink::framework::dependency_list_t<shopping_mall_checkout_server_role_t>;
    static constexpr const char *topic_name = "ConfirmOrderReq";

    explicit confirm_order_handler_t (shopping_mall_checkout_server_role_t &server)
        : _server (server)
    {
    }

    checkout_state_t handle (const confirm_order_req_t &request)
    {
        return _server.advance (request.checkout_id, checkout_status_t::order_confirmed);
    }

  private:
    shopping_mall_checkout_server_role_t &_server;
};

} // namespace zlink::samples::shoppingmallcheckout
