/* SPDX-License-Identifier: MPL-2.0 */

#include "../../Shared/messages.hpp"

#include <zlink/framework.hpp>

#include <chrono>
#include <iostream>
#include <string>

namespace zlink::e2e::to_actor_messaging
{

class caller_handler_t
{
  public:
    using request_type = caller_request_t;
    using reply_type = caller_reply_t;
    using dependency_types = zlink::framework::dependency_list_t<zlink::framework::actor_client_t>;
    static constexpr const char *topic_name = "CallerRequest";

    explicit caller_handler_t (zlink::framework::actor_client_t &actors) : _actors (actors) {}

    zlink::framework::task_t<caller_reply_t> handle (const caller_request_t &request)
    {
        actor_notify_t notify{request.scenario, request.actor_id, request.value};
        co_await _actors.send_to_actor (request.actor_id, notify)
          .packet_name (actor_notify_t::packet_name)
          .async ();
        actor_ask_t ask{request.scenario, request.actor_id, request.value};
        auto reply = co_await _actors.request_to_actor (request.actor_id, ask)
                       .packet_name (actor_ask_t::packet_name)
                       .timeout (std::chrono::seconds (5))
                       .async<actor_reply_t> ();
        co_return caller_reply_t{reply.scenario, reply.actor_id, reply.value};
    }

  private:
    zlink::framework::actor_client_t &_actors;
};

} // namespace zlink::e2e::to_actor_messaging

int main ()
{
    std::cout << "to-actor-messaging caller role=ready\n";
    return 0;
}
