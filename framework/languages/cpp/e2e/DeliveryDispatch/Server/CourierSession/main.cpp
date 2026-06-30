/* SPDX-License-Identifier: MPL-2.0 */

#include "../Configuration/sample_names.hpp"
#include "../Configuration/sample_topology.hpp"
#include "../common_codecs.hpp"

#include <zlink/framework.hpp>

#include <map>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <iostream>
#include <string>

namespace zlink::samples::deliverydispatch
{

using namespace framework;

class courier_session_directory_t
{
  public:
    void bind_courier (const std::string &courier_id, stream_t stream)
    {
        const std::lock_guard lock (_mutex);
        _streams_by_courier[courier_id] = std::move (stream);
    }

    task_t<offer_delivery_res_t> offer_to_courier (const offer_delivery_req_t &offer)
    {
        stream_t stream;
        {
            const std::lock_guard lock (_mutex);
            const auto found = _streams_by_courier.find (offer.courier_id);
            if (found == _streams_by_courier.end ()) {
                co_return offer_delivery_res_t{offer.delivery_id, offer.courier_id, false,
                                               "courier session is not bound"};
            }
            stream = found->second;
        }

        const auto notify =
          zlink::message_t::from_json (offer_delivery_notify_t{offer.courier_id, offer.delivery_id,
                                                               offer.pickup_address,
                                                               offer.dropoff_address});
        co_await stream.write_packet (notify)
          .packet_name (offer_delivery_notify_t::packet_name)
          .async ();

        std::unique_lock lock (_mutex);
        const auto key = offer.courier_id + "|" + offer.delivery_id;
        const auto ready = _condition.wait_for (lock, std::chrono::seconds (5), [&] {
            return _decisions.contains (key);
        });
        if (!ready) {
            co_return offer_delivery_res_t{offer.delivery_id, offer.courier_id, false,
                                           "courier decision timed out"};
        }
        auto decision = _decisions.at (key);
        _decisions.erase (key);
        co_return offer_delivery_res_t{decision.delivery_id, decision.courier_id,
                                       decision.accepted, decision.reason};
    }

    void decide (courier_decision_msg_t decision)
    {
        {
            const std::lock_guard lock (_mutex);
            const auto key = decision.courier_id + "|" + decision.delivery_id;
            _decisions[key] = std::move (decision);
        }
        _condition.notify_all ();
    }

  private:
    std::mutex _mutex;
    std::condition_variable _condition;
    std::map<std::string, stream_t> _streams_by_courier;
    std::map<std::string, courier_decision_msg_t> _decisions;
};

class courier_session_t final : public packet_stream_session_t
{
  public:
    using dependency_types = dependency_list_t<channel_client_t, courier_session_directory_t>;

    courier_session_t (channel_client_t &channels, courier_session_directory_t &sessions) :
        _channels (channels), _sessions (sessions)
    {
    }

    task_t<void> on_connected (stream_t &) override { co_return; }

    task_t<void> on_disconnected (stream_t &) override { co_return; }

    task_t<void> on_error (stream_t &, const stream_error_t &) override { co_return; }

    task_t<void> on_packet (stream_t &stream,
                            const stream_dispatch_context_t &dispatch,
                            const zlink::message_t &payload) override
    {
        std::cerr << "deliverydispatch courier-session: dispatch packet="
                  << dispatch.packet_name () << "\n";
        if (dispatch.packet_name () == bind_courier_session_req_t::packet_name) {
            const auto request = payload.parse_json<bind_courier_session_req_t> ();
            auto bound = co_await request_courier_gateway<bind_courier_req_t, bind_courier_res_t> (
              bind_courier_req_t{request.courier_id, "courier-session:" + request.courier_id});
            _sessions.bind_courier (request.courier_id, stream);
            const auto reply = zlink::message_t::from_json (bind_courier_session_res_t{
              bound.courier_id, bound.actor, bound.session_route});
            co_await stream.reply_packet (reply).async ();
            std::cerr << "deliverydispatch courier-session: bound courier="
                      << request.courier_id << "\n";
            co_return;
        }
        if (dispatch.packet_name () == courier_decision_msg_t::packet_name) {
            _sessions.decide (payload.parse_json<courier_decision_msg_t> ());
            co_return;
        }
    }

  private:
    template <typename TRequest, typename TReply> task_t<TReply> request_courier_gateway (TRequest request)
    {
        auto reply = co_await _channels.request (sample_names_t::courier_route_channel, request)
          .template async<TReply> ();
        co_return reply;
    }

    channel_client_t &_channels;
    courier_session_directory_t &_sessions;
};

class stream_offer_delivery_handler_t
{
  public:
    using dependency_types = dependency_list_t<courier_session_directory_t>;
    using request_type = offer_delivery_req_t;
    using reply_type = offer_delivery_res_t;
    static constexpr const char *topic_name = offer_delivery_req_t::packet_name;

    explicit stream_offer_delivery_handler_t (courier_session_directory_t &sessions) :
        _sessions (sessions)
    {
    }

    task_t<offer_delivery_res_t> handle (const offer_delivery_req_t &request)
    {
        co_return co_await _sessions.offer_to_courier (request);
    }

  private:
    courier_session_directory_t &_sessions;
};

} // namespace zlink::samples::deliverydispatch

int main (int argc, char **argv)
{
    using namespace zlink::framework;
    using namespace zlink::samples::deliverydispatch;

    const sample_topology_t topology;
    auto app = app_t::create ();
    app.add_zlink_framework ([&] (zlink_framework_options_t &options) {
        options.configure_dispatch ()
          .message_flow (message_flow_log_mode_t::key_transitions)
          .trace_log_file (deliverydispatch_log_dir () + "/flow-courier-session.log")
          .trace_label ("deliverydispatch-courier-session");
        options.services ().add_singleton<courier_session_directory_t> ();
        add_deliverydispatch_json_codecs (options.codecs ());
        options.use_discovery ().add_registry_endpoint (topology.registry_router_endpoint);
        options.add_client_server_channel (sample_names_t::courier_route_channel).enable_client ();
        options.add_client_server_channel (sample_names_t::courier_session_route_channel)
          .enable_server (topology.courier_session_route_endpoint)
          .use_handler_group ("courier-session");
        options.handlers ().group ("courier-session").add<stream_offer_delivery_handler_t> ();
        options.add_stream_node (sample_names_t::courier_stream_node)
          .bind (topology.courier_stream_endpoint)
          .register_session<courier_session_t> ();
    });
    return app.run (argc, argv);
}
