/* SPDX-License-Identifier: FSL-1.1-ALv2 */

/* SupportChat API 서버.
 *
 * 공통 sample spec: 인증(`AuthenticateUserReq/Res`)과 대화 개설 접수
 * (`OpenConversationApiReq/Res` -> Support `AllocateConversationReq/Res`)는 API 서버가
 * 소유한다. Session 서버는 stream 경계만 다루고 사용자 정보를 스스로 만들지 않는다. */

#include "../../Shared/Contracts/messages.hpp"
#include "../Configuration/location_store.hpp"
#include "../Configuration/sample_configuration.hpp"

#include <zlink/framework.hpp>

#include <iostream>
#include <map>
#include <optional>
#include <string>

namespace zlink::samples::supportchat
{
namespace
{

using namespace zlink::framework;

struct supportchat_user_t
{
    std::string actor_id;
    std::string display_name;
    std::string role;
};

/* 샘플 사용자 디렉터리: access token -> 사용자 프로필. */
class user_directory_t
{
  public:
    std::optional<supportchat_user_t> find (const std::string &access_token) const
    {
        const auto found = _users.find (access_token);
        if (found == _users.end ()) {
            return std::nullopt;
        }
        return found->second;
    }

  private:
    std::map<std::string, supportchat_user_t> _users{
      {"agent", supportchat_user_t{"agent-1", "Agent One", std::string (role_t::agent)}},
      {"customer",
       supportchat_user_t{"customer-1", "Customer One", std::string (role_t::customer)}},
      {"customer-2",
       supportchat_user_t{"customer-2", "Customer Two", std::string (role_t::customer)}}};
};

class authenticate_user_handler_t
{
  public:
    using request_type = authenticate_user_req_t;
    using reply_type = authenticate_user_res_t;
    using dependency_types = dependency_list_t<user_directory_t>;
    static constexpr const char *topic_name = authenticate_user_req_t::packet_name;

    explicit authenticate_user_handler_t (user_directory_t &users) : _users (users) {}

    authenticate_user_res_t handle (const authenticate_user_req_t &request)
    {
        const auto user = _users.find (request.access_token);
        if (!user) {
            std::cerr << "supportchat api: authenticate rejected token=" << request.access_token
                      << "\n";
            return authenticate_user_res_t{false, std::nullopt, std::nullopt, std::nullopt,
                                           std::string ("UnknownAccessToken")};
        }
        std::cerr << "supportchat api: authenticate actor=" << user->actor_id << "\n";
        return authenticate_user_res_t{true, user->actor_id, user->display_name, user->role,
                                       std::nullopt};
    }

  private:
    user_directory_t &_users;
};

class open_conversation_api_handler_t
{
  public:
    using request_type = open_conversation_api_req_t;
    using reply_type = open_conversation_api_res_t;
    using dependency_types = dependency_list_t<channel_client_t>;
    static constexpr const char *topic_name = open_conversation_api_req_t::packet_name;

    explicit open_conversation_api_handler_t (channel_client_t &channels) : _channels (channels) {}

    task_t<open_conversation_api_res_t> handle (const open_conversation_api_req_t &request)
    {
        /* 대화 배정은 Support 서버가 소유한다. API는 접수만 하고 할당 결과를 돌려준다. */
        auto allocated =
          co_await _channels
            .request ("supportchat.support",
                      allocate_conversation_req_t{request.customer_actor_id,
                                                  request.customer_display_name, request.subject})
            .submit<allocate_conversation_res_t> ();
        std::cerr << "supportchat api: conversation allocated id=" << allocated.conversation_id
                  << " status=" << allocated.status << "\n";
        co_return open_conversation_api_res_t{allocated.conversation_id, allocated.status};
    }

  private:
    channel_client_t &_channels;
};

} // namespace
} // namespace zlink::samples::supportchat

int main (int argc, char **argv)
{
    using namespace zlink::framework;
    using namespace zlink::samples::supportchat;

    auto app = app_t::create ();
    const auto configuration = load_sample_configuration (app, argc, argv);
    const auto &topology = configuration.topology;
    app.add_zlink_framework ([&] (zlink_framework_options_t &options) {
        options.configure_dispatch ()
          .message_flow (message_flow_log_mode_t::key_transitions)
          .trace_log_file (configuration.flow_log_path ())
          .trace_label ("supportchat-api");
        add_supportchat_location_store (options, topology);
        options.services ().add_singleton<user_directory_t> ();

        options.add_client_server_channel ("supportchat.api")
          .enable_server (topology.api_route_endpoint)
          .use_handler_group ("supportchat-api");
        /* Support로 나가는 client 연결은 공유 location store가 만든다. */
        options.add_client_server_channel ("supportchat.support").enable_client ();

        options.handlers ()
          .group ("supportchat-api")
          .add<authenticate_user_handler_t> ()
          .add<open_conversation_api_handler_t> ();
    });
    std::cout << "supportchat api role=ready" << std::endl;
    return app.run (argc, argv);
}
