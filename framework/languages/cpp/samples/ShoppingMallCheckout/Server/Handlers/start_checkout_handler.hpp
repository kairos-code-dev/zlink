/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../shopping_mall_checkout_server_role.hpp"

#include <zlink/framework.hpp>

namespace zlink::samples::shoppingmallcheckout
{

class start_checkout_handler_t
{
  public:
    using request_type = start_checkout_req_t;
    using reply_type = checkout_state_t;
    using dependency_types =
      zlink::framework::dependency_list_t<shopping_mall_checkout_server_role_t>;
    static constexpr const char *topic_name = "StartCheckoutReq";

    explicit start_checkout_handler_t (shopping_mall_checkout_server_role_t &server)
        : _server (server)
    {
    }

    checkout_state_t handle (const start_checkout_req_t &request)
    {
        return _server.start_checkout (request.customer_id);
    }

  private:
    shopping_mall_checkout_server_role_t &_server;
};

} // namespace zlink::samples::shoppingmallcheckout
