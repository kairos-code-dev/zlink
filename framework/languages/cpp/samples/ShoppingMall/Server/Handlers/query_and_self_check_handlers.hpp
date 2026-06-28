/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../shopping_mall_server_role.hpp"

#include <zlink/framework.hpp>

namespace zlink::samples::shoppingmall
{

using namespace framework;

class get_order_state_handler_t
{
  public:
    using request_type = get_order_state_req_t;
    using reply_type = get_order_state_res_t;
    using dependency_types = dependency_list_t<shopping_mall_server_role_t>;
    static constexpr const char *topic_name = "GetOrderStateReq";
    explicit get_order_state_handler_t (shopping_mall_server_role_t &server) : _server (server) {}
    get_order_state_res_t handle (const get_order_state_req_t &request)
    {
        return _server.get_order (request.order_id);
    }

  private:
    shopping_mall_server_role_t &_server;
};

class delete_order_projection_handler_t
{
  public:
    using request_type = delete_order_projection_req_t;
    using reply_type = get_order_state_res_t;
    using dependency_types = dependency_list_t<shopping_mall_server_role_t>;
    static constexpr const char *topic_name = "DeleteOrderProjectionReq";
    explicit delete_order_projection_handler_t (shopping_mall_server_role_t &server) :
        _server (server)
    {
    }
    get_order_state_res_t handle (const delete_order_projection_req_t &request)
    {
        return _server.delete_projection (request.order_id);
    }

  private:
    shopping_mall_server_role_t &_server;
};

class rebuild_order_projection_handler_t
{
  public:
    using request_type = rebuild_order_projection_req_t;
    using reply_type = rebuild_order_projection_res_t;
    using dependency_types = dependency_list_t<shopping_mall_server_role_t>;
    static constexpr const char *topic_name = "RebuildOrderProjectionReq";
    explicit rebuild_order_projection_handler_t (shopping_mall_server_role_t &server) :
        _server (server)
    {
    }
    rebuild_order_projection_res_t handle (const rebuild_order_projection_req_t &request)
    {
        return _server.rebuild_projection (request.order_id);
    }

  private:
    shopping_mall_server_role_t &_server;
};

class seed_pending_idempotency_handler_t
{
  public:
    using request_type = seed_pending_idempotency_req_t;
    using reply_type = start_order_res_t;
    using dependency_types = dependency_list_t<shopping_mall_server_role_t>;
    static constexpr const char *topic_name = "SeedPendingIdempotencyReq";
    explicit seed_pending_idempotency_handler_t (shopping_mall_server_role_t &server) :
        _server (server)
    {
    }
    start_order_res_t handle (const seed_pending_idempotency_req_t &request)
    {
        return _server.seed_pending (request);
    }

  private:
    shopping_mall_server_role_t &_server;
};

class server_assertion_handler_t
{
  public:
    using request_type = server_assertion_req_t;
    using reply_type = server_assertion_res_t;
    using dependency_types = dependency_list_t<shopping_mall_server_role_t>;
    static constexpr const char *topic_name = "ServerAssertionReq";
    explicit server_assertion_handler_t (shopping_mall_server_role_t &server) : _server (server) {}
    server_assertion_res_t handle (const server_assertion_req_t &request)
    {
        return _server.assert_evidence (request);
    }

  private:
    shopping_mall_server_role_t &_server;
};

} // namespace zlink::samples::shoppingmall
