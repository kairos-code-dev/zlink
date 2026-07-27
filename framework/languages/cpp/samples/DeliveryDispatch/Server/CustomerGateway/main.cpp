/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include "../Configuration/location_store.hpp"
#include "../Configuration/sample_names.hpp"
#include "../Configuration/sample_configuration.hpp"
#include "../common_codecs.hpp"

#include <zlink/framework.hpp>

#include <set>
#include <mutex>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace zlink::samples::deliverydispatch
{

using namespace framework;

class customer_session_directory_t
{
  public:
    void subscribe (const std::string &customer_id,
                    const std::string &delivery_id,
                    stream_t stream)
    {
        const std::lock_guard lock (_mutex);
        _subscriptions[delivery_id] = subscription_t{customer_id, std::move (stream)};
        std::cerr << "deliverydispatch customer-session: registered subscription customer="
                  << customer_id << " delivery=" << delivery_id << "\n";
    }

    std::optional<std::string> customer_for_delivery (const std::string &delivery_id) const
    {
        const std::lock_guard lock (_mutex);
        const auto found = _subscriptions.find (delivery_id);
        if (found == _subscriptions.end ()) {
            return std::nullopt;
        }
        return found->second.customer_id;
    }

    void unsubscribe_customer (const std::string &customer_id)
    {
        const std::lock_guard lock (_mutex);
        for (auto it = _subscriptions.begin (); it != _subscriptions.end ();) {
            if (it->second.customer_id == customer_id) {
                it = _subscriptions.erase (it);
            } else {
                ++it;
            }
        }
    }

  private:
    struct subscription_t
    {
        std::string customer_id;
        stream_t stream;
    };

    mutable std::mutex _mutex;
    std::map<std::string, subscription_t> _subscriptions;
};

class customer_actor_t
{
  public:
    explicit customer_actor_t (std::string actor_id) : actor_id (std::move (actor_id)) {}

    void set_actor_ref (const zlink::framework::actor_ref_t &value)
    {
        actor_ref = value;
        actor_id = std::string (value.actor_id ());
    }

    void set_actor_context (actor_context_t &value) { context = &value; }

    std::string actor_id;
    zlink::framework::actor_ref_t actor_ref;
    actor_context_t *context = nullptr;
};

struct customer_actor_factory_t
{
    customer_actor_t create (std::string actor_id) const
    {
        return customer_actor_t (std::move (actor_id));
    }
};

class customer_entry_spot_t : public entry_spot_t
{
  public:
    customer_entry_spot_t (customer_session_directory_t &sessions,
                           actor_manager_t &actors) :
        _sessions (sessions), _actors (actors)
    {
    }

    void configure (entry_spot_context_t &context)
    {
        /* 공통 sample spec §7.3: Tracking은 고객 actor를 먼저 찾은 뒤 one-way로 상태를
         * 보낸다. 조회는 이 entry spot의 route 핸들러가 답한다. */
        context.handlers ()
          .add_handler<&customer_entry_spot_t::find_customer_actor> (
            find_customer_actor_req_t::packet_name)
          .add_actor_request<&customer_entry_spot_t::subscribe_delivery> (
            subscribe_delivery_req_t::packet_name)
          .add_actor_send<&customer_entry_spot_t::status_updated> (
            delivery_status_updated_msg_t::packet_name);
    }

    task_t<find_customer_actor_res_t>
    find_customer_actor (const find_customer_actor_req_t &request)
    {
        auto actor = co_await _actors.find (request.customer_id);
        if (!actor) {
            co_return find_customer_actor_res_t{request.customer_id, std::nullopt};
        }
        co_return find_customer_actor_res_t{request.customer_id,
                                            actor_ref_snapshot_t::from (*actor)};
    }

    spot_actor_join_response_t on_actor_join (std::string_view,
                                              const zlink::message_t &)
    {
        return spot_actor_join_response_t::accept ();
    }

    subscribe_delivery_res_t
    subscribe_delivery (customer_actor_t &actor,
                        message_context_t &,
                        const subscribe_delivery_req_t &request)
    {
        return {request.delivery_id};
    }

    void status_updated (customer_actor_t &actor,
                         message_context_t &,
                         const delivery_status_updated_msg_t &status)
    {
        auto customer_id = _sessions.customer_for_delivery (status.delivery_id);
        if (!customer_id || *customer_id != actor.actor_id) {
            std::cerr << "deliverydispatch customer-entry: ignored status delivery="
                      << status.delivery_id << " actor=" << actor.actor_id << "\n";
            return;
        }
        std::cerr << "deliverydispatch customer-entry: push status delivery="
                  << status.delivery_id << " status=" << status.status << "\n";
        actor.context->bound_session ()
          .send (delivery_status_notify_t{status.delivery_id, status.status, status.courier_id,
                                          status.occurred_at})
          .submit ();
    }

  private:
    customer_session_directory_t &_sessions;
    actor_manager_t &_actors;
};

class customer_gateway_session_t final : public packet_stream_session_t
{
  public:
    using dependency_types = dependency_list_t<customer_session_directory_t>;

    explicit customer_gateway_session_t (customer_session_directory_t &sessions) :
        _sessions (sessions)
    {
    }

    task_t<void> on_connected (stream_t &) override { co_return; }

    task_t<void> on_disconnected (stream_t &) override
    {
        for (const auto &actor_id : _bound_actors) {
            _sessions.unsubscribe_customer (actor_id);
        }
        _bound_actors.clear ();
        co_return;
    }

    task_t<void> on_error (stream_t &, const stream_error_t &) override { co_return; }

    task_t<void> on_packet (stream_t &stream,
                            const stream_dispatch_context_t &dispatch,
                            const zlink::message_t &payload) override
    {
        std::cerr << "deliverydispatch customer-gateway: dispatch packet="
                  << dispatch.packet_name () << "\n";
        if (dispatch.packet_name () != subscribe_delivery_req_t::packet_name) {
            auto actor = require_single_bound_actor (
              stream, std::string (dispatch.packet_name ()));
            if (dispatch.can_reply ()) {
                auto reply = co_await actor.relay_request (payload).submit ();
                stream.reply_packet (reply).submit ();
                co_return;
            }
            co_await actor.relay (payload);
            co_return;
        }
        const auto request = payload.parse_json<subscribe_delivery_req_t> ();
        auto &actors = stream.actors ();
        auto actor = actors.get_or_create (sample_names_t::customer_actor_type,
                                           sample_names_t::customer_id, request);
        if (!actor) {
            throw framework_exception_t (
              actor.error_kind (),
              actor.error () ? actor.error ()->what () : "customer actor create failed");
        }
        auto bound = co_await actors.bind_or_get (actor.value ().ref ()).submit ();
        const auto actor_id = std::string (bound.actor_id ());
        /* Join은 현재 Actor turn이 끝난 뒤 실행하도록 예약한다. 이어지는 relay는 같은 Actor의
         * 순서 경계에서 join 뒤에 처리된다. */
        bound.context ().join_entry_spot (request).defer ();
        auto current = actors.find (actor_id);
        if (!current) {
            throw framework_exception_t (framework_error_kind_t::actor_route_not_found,
                                         "joined customer actor route is not found");
        }
        auto reply =
          co_await current->relay_request (zlink::message_t::from_json (request)).submit ();
        _bound_actors.insert (actor_id);
        _sessions.subscribe (actor_id, request.delivery_id, stream);
        stream.reply_packet (reply).submit ();
        std::cerr << "deliverydispatch customer-session: bound customer actor="
                  << actor_id
                  << "\n";
        std::cerr << "deliverydispatch customer-session: subscribed customer="
                  << sample_names_t::customer_id << " delivery=" << request.delivery_id << "\n";
    }

  private:
    session_actor_t require_single_bound_actor (stream_t &stream,
                                                const std::string &packet_name)
    {
        if (_bound_actors.size () != 1) {
            throw framework_exception_t (framework_error_kind_t::actor_route_not_found,
                                         "single bound customer actor is required for "
                                           + packet_name);
        }
        auto actor = stream.actors ().find (*_bound_actors.begin ());
        if (!actor) {
            throw framework_exception_t (framework_error_kind_t::actor_route_not_found,
                                         "bound customer actor route is not found for "
                                           + packet_name);
        }
        return *actor;
    }

    customer_session_directory_t &_sessions;
    std::set<std::string> _bound_actors;
};

} // namespace zlink::samples::deliverydispatch

int main (int argc, char **argv)
{
    using namespace zlink::framework;
    using namespace zlink::samples::deliverydispatch;

    auto app = app_t::create ();
    const auto configuration = load_sample_configuration (app, argc, argv);
    const auto &topology = configuration.topology;
    app.add_zlink_framework ([&] (zlink_framework_options_t &options) {
        options.configure_dispatch ()
          .message_flow (message_flow_log_mode_t::key_transitions)
          .trace_log_file (configuration.flow_log_path ())
          .trace_label ("deliverydispatch-customer-gateway");
        auto sessions = std::make_unique<customer_session_directory_t> ();
        auto *sessions_ptr = sessions.get ();
        options.services ().add_singleton<customer_session_directory_t> (std::move (sessions));
        auto services = options.services ().build_provider ();
        add_deliverydispatch_json_codecs (options.codecs ());
        add_deliverydispatch_location_store (options, topology);
        auto actor_mesh = options.add_route_mesh (sample_names_t::customer_actor_discovery);
        actor_mesh.listen (topology.customer_spot_router_endpoint);
        actor_mesh.channel_name (sample_names_t::customer_actor_discovery);
        actor_mesh.add_entry_spot<customer_entry_spot_t> ([sessions_ptr, services] () mutable {
              return std::make_shared<customer_entry_spot_t> (
                *sessions_ptr, services.get_required<actor_manager_t> ());
          })
          .add_actor_factory<customer_actor_factory_t> (sample_names_t::customer_actor_type);
        options.add_stream_node (sample_names_t::customer_stream_node)
          .bind (topology.customer_stream_endpoint)
          .register_session<customer_gateway_session_t> ();
    });
    return app.run (argc, argv);
}
