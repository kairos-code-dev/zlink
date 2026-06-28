/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../Configuration/sample_names.hpp"
#include "../../../Shared/Contracts/messages.hpp"

#include <zlink/framework.hpp>

#include <utility>

namespace zlink::samples::supportchat
{

using namespace framework;

// API orchestration handler: turns a customer's open request into a Support
// allocation and agent assignment over the Support channel.
class open_conversation_handler_t
{
  public:
    using request_type = open_conversation_api_req_t;
    using reply_type = open_conversation_api_res_t;
    using dependency_types = dependency_list_t<channel_client_t>;
    static constexpr const char *topic_name = "OpenConversationApi";

    explicit open_conversation_handler_t (channel_client_t &client, logger_t<> logger = {}) :
        _client (client), _logger (std::move (logger))
    {
    }

    task_t<open_conversation_api_res_t> handle (const open_conversation_api_req_t &request)
    {
        const auto allocate_request = allocate_conversation_req_t{
            request.customer_actor_id, request.customer_display_name, request.subject
        };
        auto allocated = co_await _client.request (
            sample_names_t::support_channel, allocate_request).async<allocate_conversation_res_t> ();

        const auto assign_request = assign_agent_req_t{allocated.conversation_id, ""};
        auto assigned = co_await _client.request (
            sample_names_t::support_channel, assign_request).async<assign_agent_res_t> ();

        _logger.info ("open conversation", {{"conversation_id", allocated.conversation_id},
                                            {"status", assigned.status}});
        co_return open_conversation_api_res_t{allocated.conversation_id, assigned.status};
    }

  private:
    channel_client_t &_client;
    logger_t<> _logger;
};

} // namespace zlink::samples::supportchat
