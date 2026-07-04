/* SPDX-License-Identifier: MPL-2.0 */

#include "../../Shared/messages.hpp"

#include <zlink/framework.hpp>

#include <iostream>

namespace zlink::e2e::to_actor_messaging
{

class to_actor_e2e_actor_t
{
  public:
    explicit to_actor_e2e_actor_t (std::string actor_id) : _actor_id (std::move (actor_id)) {}

    const std::string &actor_id () const { return _actor_id; }

  private:
    std::string _actor_id;
};

class to_actor_e2e_spot_t : public zlink::framework::spot_t
{
  public:
    void configure (zlink::framework::spot_context_t &context)
    {
        context.handlers ().add_actor_send<&to_actor_e2e_spot_t::on_notify> (
          actor_notify_t::packet_name);
        context.handlers ().add_actor_request<&to_actor_e2e_spot_t::on_ask> (
          actor_ask_t::packet_name);
    }

    void on_notify (to_actor_e2e_actor_t &actor,
                    zlink::framework::spot_actor_send_context_t &,
                    const actor_notify_t &message)
    {
        std::cerr << "to-actor actor notify scenario=" << message.scenario
                  << " actor=" << actor.actor_id () << " value=" << message.value << "\n";
    }

    actor_reply_t on_ask (to_actor_e2e_actor_t &actor,
                          zlink::framework::spot_actor_request_context_t &,
                          const actor_ask_t &message)
    {
        return {message.scenario, actor.actor_id (), "reply:" + message.value};
    }
};

} // namespace zlink::e2e::to_actor_messaging

int main ()
{
    std::cout << "to-actor-messaging actor role=ready\n";
    return 0;
}
