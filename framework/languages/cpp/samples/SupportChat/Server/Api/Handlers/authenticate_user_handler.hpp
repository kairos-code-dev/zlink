/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../../Shared/Contracts/messages.hpp"

#include <zlink/framework.hpp>

#include <utility>

namespace zlink::samples::supportchat
{

// API authentication boundary: validates a stream access token and returns the
// actor identity (id, display name, role).
class authenticate_user_handler_t
{
  public:
    using request_type = authenticate_user_req_t;
    using reply_type = authenticate_user_res_t;
    static constexpr const char *topic_name = "AuthenticateUser";

    explicit authenticate_user_handler_t (zlink::framework::logger_t<> logger = {}) :
        _logger (std::move (logger))
    {
    }

    authenticate_user_res_t handle (const authenticate_user_req_t &request)
    {
        const auto &token = request.access_token;
        if (token == support_chat_tokens_t::customer1) {
            return accepted ("customer-1", "Customer 1", support_chat_roles_t::customer);
        }
        if (token == support_chat_tokens_t::customer2) {
            return accepted ("customer-2", "Customer 2", support_chat_roles_t::customer);
        }
        if (token == support_chat_tokens_t::customer3) {
            return accepted ("customer-3", "Customer 3", support_chat_roles_t::customer);
        }
        if (token == support_chat_tokens_t::agent1) {
            return accepted ("agent-1", "Agent 1", support_chat_roles_t::agent);
        }
        if (token == support_chat_tokens_t::agent2) {
            return accepted ("agent-2", "Agent 2", support_chat_roles_t::agent);
        }
        _logger.warn ("reject support chat token", {{"access_token", token}});
        return {false, "", "", "", "Unknown support chat token."};
    }

  private:
    authenticate_user_res_t accepted (std::string actor_id, std::string display_name,
                                      std::string role)
    {
        _logger.info ("authenticate support user", {{"actor_id", actor_id}});
        return {true, std::move (actor_id), std::move (display_name), std::move (role), ""};
    }

    zlink::framework::logger_t<> _logger;
};

} // namespace zlink::samples::supportchat
