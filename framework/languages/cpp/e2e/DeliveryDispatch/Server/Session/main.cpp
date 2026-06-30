/* SPDX-License-Identifier: MPL-2.0 */

#include "../Configuration/sample_names.hpp"
#include "../Configuration/sample_topology.hpp"
#include "../common_codecs.hpp"

#include <zlink/framework.hpp>

#include <map>
#include <mutex>
#include <condition_variable>
#include <iostream>
#include <chrono>
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
    void subscribe (const std::string &delivery_id, stream_t stream)
    {
        const std::lock_guard lock (_mutex);
        _streams_by_delivery[delivery_id].push_back (std::move (stream));
    }

    task_t<void> publish (const delivery_status_notify_t &notify)
    {
        std::vector<stream_t> streams;
        {
            const std::lock_guard lock (_mutex);
            const auto found = _streams_by_delivery.find (notify.delivery_id);
            if (found != _streams_by_delivery.end ()) {
                streams = found->second;
            }
        }
        const auto payload = zlink::message_t::from_json (notify);
        for (auto &stream : streams) {
            co_await stream.write_packet (payload)
              .packet_name (delivery_status_notify_t::packet_name)
              .async ();
        }
    }

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
    std::map<std::string, std::vector<stream_t>> _streams_by_delivery;
    std::map<std::string, stream_t> _streams_by_courier;
    std::map<std::string, courier_decision_msg_t> _decisions;
};

class customer_session_t final : public packet_stream_session_t
{
  public:
    using dependency_types = dependency_list_t<channel_client_t, customer_session_directory_t>;

    customer_session_t (channel_client_t &channels, customer_session_directory_t &sessions) :
        _channels (channels), _sessions (sessions)
    {
    }

    task_t<void> on_connected (stream_t &) override
    {
        return task_t<void> (result_t<void>::success ());
    }

    task_t<void> on_disconnected (stream_t &) override
    {
        return task_t<void> (result_t<void>::success ());
    }

    task_t<void> on_error (stream_t &, const stream_error_t &) override
    {
        return task_t<void> (result_t<void>::success ());
    }

    task_t<void> on_packet (stream_t &stream,
                            const stream_dispatch_context_t &dispatch,
                            const zlink::message_t &payload) override
    {
        std::cerr << "deliverydispatch session: dispatch packet=" << dispatch.packet_name ()
                  << "\n";
        try {
            if (dispatch.packet_name () == bind_courier_session_req_t::packet_name) {
                const auto request = payload.parse_json<bind_courier_session_req_t> ();
                auto bound = co_await request_courier_gateway<bind_courier_req_t, bind_courier_res_t> (
                  bind_courier_req_t{request.courier_id, "courier-session:" + request.courier_id});
                _sessions.bind_courier (request.courier_id, stream);
                const auto reply = zlink::message_t::from_json (bind_courier_session_res_t{
                  bound.courier_id, bound.actor, bound.session_route});
                stream.reply_packet (reply).submit ();
                std::cerr << "deliverydispatch session: courier bound courier="
                          << request.courier_id << "\n";
                co_return;
            }
            if (dispatch.packet_name () == courier_decision_msg_t::packet_name) {
                _sessions.decide (payload.parse_json<courier_decision_msg_t> ());
                co_return;
            }
            if (dispatch.packet_name () != subscribe_delivery_req_t::packet_name) {
                co_return;
            }
            const auto request = payload.parse_json<subscribe_delivery_req_t> ();
            std::cerr << "deliverydispatch session: ensure customer delivery="
                      << request.delivery_id << "\n";
            auto ensured = request_tracking<ensure_customer_actor_req_t, ensure_customer_actor_res_t> (
              ensure_customer_actor_req_t{sample_names_t::customer_id});
            (void) ensured;
            std::cerr << "deliverydispatch session: subscribe delivery=" << request.delivery_id
                      << "\n";
            auto subscribed =
              request_tracking<subscribe_customer_to_delivery_req_t, subscribe_customer_to_delivery_res_t> (
                subscribe_customer_to_delivery_req_t{sample_names_t::customer_id, request.delivery_id});
            _sessions.subscribe (subscribed.delivery_id, stream);
            const auto reply =
              zlink::message_t::from_json (subscribe_delivery_res_t{subscribed.delivery_id});
            stream.reply_packet (reply).submit ();
            std::cerr << "deliverydispatch session: reply subscribed delivery="
                      << subscribed.delivery_id << "\n";
        }
        catch (const std::exception &error) {
            std::cerr << "deliverydispatch session: failed " << error.what () << "\n";
            throw;
        }
    }

  private:
    template <typename TRequest, typename TReply> TReply request_tracking (TRequest request)
    {
        std::string last_error;
        for (int attempt = 1; attempt <= 40; ++attempt) {
            auto result =
              _channels.request (sample_names_t::tracking_route_channel, request)
                .template async<TReply> ()
                .result ();
            if (result) {
                return result.value ();
            }
            last_error = result.error () ? result.error ()->what () : "tracking request failed";
            std::this_thread::sleep_for (std::chrono::milliseconds (100));
        }
        throw std::runtime_error (last_error);
    }

    template <typename TRequest, typename TReply> task_t<TReply> request_courier_gateway (TRequest request)
    {
        auto reply = co_await _channels.request (sample_names_t::courier_route_channel, request)
          .template async<TReply> ();
        co_return reply;
    }

    channel_client_t &_channels;
    customer_session_directory_t &_sessions;
};

class delivery_status_fanout_handler_t
{
  public:
    using dependency_types = dependency_list_t<customer_session_directory_t>;
    using event_type = delivery_status_notify_t;
    static constexpr const char *topic_name = sample_names_t::status_topic;

    explicit delivery_status_fanout_handler_t (customer_session_directory_t &sessions) :
        _sessions (sessions)
    {
    }

    task_t<void> handle (const delivery_status_notify_t &notify, const publish_context_t &)
    {
        co_await _sessions.publish (notify);
    }

  private:
    customer_session_directory_t &_sessions;
};

class stream_offer_delivery_handler_t
{
  public:
    using dependency_types = dependency_list_t<customer_session_directory_t>;
    using request_type = offer_delivery_req_t;
    using reply_type = offer_delivery_res_t;
    static constexpr const char *topic_name = offer_delivery_req_t::packet_name;

    explicit stream_offer_delivery_handler_t (customer_session_directory_t &sessions) :
        _sessions (sessions)
    {
    }

    task_t<offer_delivery_res_t> handle (const offer_delivery_req_t &request)
    {
        co_return co_await _sessions.offer_to_courier (request);
    }

  private:
    customer_session_directory_t &_sessions;
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
          .trace_log_file (deliverydispatch_log_dir () + "/flow-session.log")
          .trace_label ("deliverydispatch-session");
        options.services ().add_singleton<customer_session_directory_t> ();
        add_deliverydispatch_json_codecs (options.codecs ());
        options.use_discovery ().add_registry_endpoint (topology.registry_router_endpoint);
        options.add_client_server_channel (sample_names_t::tracking_route_channel).enable_client ();
        options.add_client_server_channel (sample_names_t::courier_route_channel).enable_client ();
        options.add_client_server_channel (sample_names_t::courier_session_route_channel)
          .enable_server (topology.courier_session_route_endpoint)
          .use_handler_group ("courier-session");
        options.add_fanout_channel (sample_names_t::status_fanout_channel)
          .enable_subscriber (topology.status_fanout_endpoint)
          .use_handler_group ("status");
        options.handlers ().group ("status").add_publish<delivery_status_fanout_handler_t> ();
        options.handlers ().group ("courier-session").add<stream_offer_delivery_handler_t> ();
        options.add_stream_node (sample_names_t::customer_stream_node)
          .bind (topology.session_stream_endpoint)
          .register_session<customer_session_t> ();
    });
    return app.run (argc, argv);
}
