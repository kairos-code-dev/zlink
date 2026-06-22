/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "messages.hpp"

#include <zlink/framework/codecs/json_extension.hpp>

namespace zlink::samples::supportchat
{

inline auto support_chat_json_codec ()
{
    return zlink::framework_codecs::json<authenticate_req_t,
                                         authenticate_res_t,
                                         authenticate_user_req_t,
                                         authenticate_user_res_t,
                                         actor_ref_snapshot_t,
                                         ensure_support_user_actor_req_t,
                                         ensure_support_user_actor_res_t,
                                         open_conversation_api_req_t,
                                         open_conversation_api_res_t,
                                         allocate_conversation_req_t,
                                         allocate_conversation_res_t,
                                         assign_agent_req_t,
                                         assign_agent_res_t,
                                         conversation_state_t,
                                         chat_message_t,
                                         open_conversation_req_t,
                                         open_conversation_res_t,
                                         set_agent_available_req_t,
                                         set_agent_available_res_t,
                                         join_conversation_req_t,
                                         join_conversation_res_t,
                                         send_chat_message_req_t,
                                         send_chat_message_res_t,
                                         set_typing_req_t,
                                         set_typing_res_t,
                                         close_conversation_req_t,
                                         close_conversation_res_t,
                                         participant_joined_notify_t,
                                         conversation_assigned_notify_t,
                                         chat_message_notify_t,
                                         typing_changed_notify_t,
                                         conversation_idle_notify_t,
                                         conversation_closed_notify_t,
                                         conversation_create_request_t> ();
}

} // namespace zlink::samples::supportchat
