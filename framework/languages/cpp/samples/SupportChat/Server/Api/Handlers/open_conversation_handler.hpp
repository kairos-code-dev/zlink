/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../Configuration/sample_names.hpp"
#include "../../../Shared/Contracts/messages.hpp"

#include <zlink/framework.hpp>

#include <utility>

namespace zlink::samples::supportchat
{

using zlink::framework::task_t;

// API orchestration handler: turns a customer's open request into a Support
// allocation and agent assignment over the Support channel.
class open_conversation_handler_t
{
  public:
    using request_type = open_conversation_api_req_t;
    using reply_type = open_conversation_api_res_t;
    using dependency_types =
      zlink::framework::dependency_list_t<zlink::framework::channel_client_t>;
    static constexpr const char *topic_name = "OpenConversationApi";

    explicit open_conversation_handler_t (zlink::framework::channel_client_t &client,
                                          zlink::framework::logger_t<> logger = {}) :
        _client (client), _logger (std::move (logger))
    {
    }

    task_t<open_conversation_api_res_t> handle (const open_conversation_api_req_t &request)
    {
        auto allocated =
          co_await _client
            .request (sample_names_t::support_channel,
                      allocate_conversation_req_t{request.customer_actor_id,
                                                  request.customer_display_name, request.subject})
            .async<allocate_conversation_res_t> ();

        auto assigned =
          co_await _client
            .request (sample_names_t::support_channel,
                      assign_agent_req_t{allocated.conversation_id, ""})
            .async<assign_agent_res_t> ();

        _logger.info ("open conversation",
                      {{"conversation_id", allocated.conversation_id},
                       {"status", assigned.status}});
        co_return open_conversation_api_res_t{allocated.conversation_id, assigned.status};
    }

  private:
    zlink::framework::channel_client_t &_client;
    zlink::framework::logger_t<> _logger;
};

} // namespace zlink::samples::supportchat
