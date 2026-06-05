/* SPDX-License-Identifier: MPL-2.0 */

#include <zlink/framework.hpp>

#include "runtime/channels/channel_packet_dispatcher.hpp"
#include "runtime/channels/channel_reply_writer.hpp"
#include "runtime/channels/channel_message_pump.hpp"
#include "runtime/channels/channel_receive_loop.hpp"
#include "runtime/channels/channel_runtime.hpp"
#include "runtime/channels/channel_runtime_bundle.hpp"
#include "runtime/channels/channel_runtime_manager.hpp"
#include "runtime/channels/route_channel_runtime.hpp"
#include "runtime/channels/route_channel_registration.hpp"
#include "runtime/channels/route_connection_set.hpp"
#include "runtime/channels/route_handler_registry.hpp"
#include "runtime/channels/route_internal_packet_dispatcher.hpp"
#include "runtime/channels/route_receive_pump.hpp"
#include "runtime/backend/native_route_backend.hpp"
#include "runtime/messaging/envelope_codec.hpp"

#include <zlink.hpp>

#include <chrono>
#include <string>

namespace
{

struct request_t
{
    int value{};
};

struct reply_t
{
    int value{};
};

struct event_t
{
    int value{};
};

class local_handler_t
{
  public:
    reply_t handle_request (const request_t &request)
    {
        last_request = request.value;
        return {request.value + 100};
    }

    void handle_send (const event_t &event) { last_event = event.value; }

    reply_t handle_route_request (const request_t &request, const zlink::framework::route_handler_context_t &context)
    {
        last_route_request = request.value;
        last_route_source = context.source_node_rid.to_string ();
        return {request.value + 200};
    }

    void handle_route_send (const event_t &event, const zlink::framework::route_handler_context_t &context)
    {
        last_route_event = event.value;
        last_route_source = context.source_node_rid.to_string ();
    }

    int last_request = 0;
    int last_event = 0;
    int last_route_request = 0;
    int last_route_event = 0;
    std::string last_route_source;
};

class local_internal_dispatcher_t final : public zlink::framework::detail::route_internal_packet_dispatcher_t
{
  public:
    bool can_handle_send (std::string_view packet_name) const override { return packet_name == "internal.send"; }

    bool can_handle_request (std::string_view packet_name) const override { return packet_name == "internal.request"; }

    zlink::framework::result_t<void>
    dispatch_send (const zlink::framework::detail::route_received_packet_t &received) const override
    {
        (void) received;
        ++send_count;
        return zlink::framework::result_t<void>::success ();
    }

    zlink::framework::result_t<zlink::message_t>
    dispatch_request (const zlink::framework::detail::route_received_packet_t &received,
                      const zlink::framework::runtime::messaging::envelope_header_t &header) const override
    {
        (void) received;
        if (header.message_name != "internal.request") {
            return zlink::framework::result_t<zlink::message_t>::failure (
              zlink::framework::framework_error_kind_t::route_handler_not_found, "unsupported internal request");
        }
        return zlink::framework::result_t<zlink::message_t>::success (zlink::message_t::from (std::string ("88")));
    }

    mutable int send_count = 0;
};

template <typename T> void add_int_serializer (zlink::framework::serializer_registry_t &serializers)
{
    serializers.add<T> ([] (const T &value) { return zlink::message_t::from (std::to_string (value.value)); },
                        [] (const zlink::message_t &message) { return T{std::stoi (message.to_string ())}; });
}

} // namespace

int main ()
{
    zlink::framework::zlink_builder_t zlink;
    zlink.add_node ("outbound-node")
      .channel ("profile",
                [] (zlink::framework::channel_builder_t &channel) {
                    channel.enable_client (
                      [] (zlink::framework::capability_builder_t &client) { client.connect ("tcp://127.0.0.1:7101"); });
                })
      .channel ("events", [] (zlink::framework::channel_builder_t &channel) {
          channel.enable_publisher (
            [] (zlink::framework::capability_builder_t &publisher) { publisher.bind ("tcp://127.0.0.1:7201"); });
      });

    const auto channels = zlink.channels ();
    if (channels.size () != 2) {
        return 1;
    }

    auto client = zlink.request_client ("profile");
    auto outbound_runtime = zlink::framework::detail::channel_runtime_t::from (zlink.message_bus ());
    auto request_call = client.request<request_t, reply_t> ({1})
                          .packet_name ("profile.lookup")
                          .metadata ("trace-id", "request-trace")
                          .timeout (std::chrono::milliseconds (3000));
    if (!outbound_runtime.outbound_calls ().empty ()) {
        return 30;
    }
    auto request_result = request_call.submit ().result ();
    if (request_result || request_result.error_kind () != zlink::framework::framework_error_kind_t::timeout) {
        return 2;
    }
    if (outbound_runtime.outbound_calls ().size () != 1 || outbound_runtime.outbound_calls ()[0].kind != "request"
        || outbound_runtime.outbound_calls ()[0].packet_name != "profile.lookup"
        || outbound_runtime.outbound_calls ()[0].timeout != std::chrono::milliseconds (3000)
        || outbound_runtime.outbound_calls ()[0].metadata.at ("trace-id") != "request-trace") {
        return 31;
    }

    auto bus = zlink.message_bus ();
    auto send_call =
      bus.send ("profile", request_t{2}).packet_name ("profile.command").metadata ("trace-id", "send-trace");
    if (outbound_runtime.outbound_calls ().size () != 1) {
        return 32;
    }
    auto send_result = send_call.submit ().result ();
    if (!send_result) {
        return 3;
    }
    if (outbound_runtime.outbound_calls ().size () != 2 || outbound_runtime.outbound_calls ()[1].kind != "send"
        || outbound_runtime.outbound_calls ()[1].packet_name != "profile.command"
        || outbound_runtime.outbound_calls ()[1].metadata.at ("trace-id") != "send-trace") {
        return 33;
    }

    auto publish_result = zlink.publisher ()
                            .publish ("events", "profile.changed", event_t{3})
                            .packet_name ("profile.changed.event")
                            .metadata ("trace-id", "publish-trace")
                            .submit ()
                            .result ();
    if (!publish_result) {
        return 4;
    }
    if (outbound_runtime.outbound_calls ().size () != 3 || outbound_runtime.outbound_calls ()[2].kind != "publish"
        || outbound_runtime.outbound_calls ()[2].topic != "profile.changed"
        || outbound_runtime.outbound_calls ()[2].packet_name != "profile.changed.event"
        || outbound_runtime.outbound_calls ()[2].metadata.at ("trace-id") != "publish-trace") {
        return 34;
    }

    auto disconnected_result = bus.send ("missing", request_t{4}).submit ().result ();
    if (disconnected_result
        || disconnected_result.error_kind () != zlink::framework::framework_error_kind_t::disconnected) {
        return 5;
    }

    zlink::framework::zlink_builder_t full_queue;
    full_queue.max_pending (0).channel ("profile", [] (zlink::framework::channel_builder_t &channel) {
        channel.enable_client ([] (zlink::framework::capability_builder_t &client) { client.use_discovery (); });
    });
    auto queue_full_result = full_queue.message_bus ().request<request_t, reply_t> ("profile", {5}).submit ().result ();
    if (queue_full_result
        || queue_full_result.error_kind () != zlink::framework::framework_error_kind_t::request_rejected) {
        return 6;
    }

    bool mixed_connection_failed = false;
    try {
        zlink::framework::zlink_builder_t invalid;
        invalid.channel ("bad", [] (zlink::framework::channel_builder_t &channel) {
            channel.enable_client ([] (zlink::framework::capability_builder_t &client) {
                client.connect ("tcp://127.0.0.1:7301").use_discovery ();
            });
        });
    }
    catch (const zlink::framework::framework_exception_t &error) {
        mixed_connection_failed = error.kind () == zlink::framework::framework_error_kind_t::request_protocol_error;
    }
    if (!mixed_connection_failed) {
        return 7;
    }

    zlink::framework::zlink_builder_t outbound_only;
    outbound_only.channel ("client-only", [] (zlink::framework::channel_builder_t &channel) {
        channel.enable_client ([] (zlink::framework::capability_builder_t &client) { client.use_discovery (); });
        channel.enable_publisher (
          [] (zlink::framework::capability_builder_t &publisher) { publisher.use_discovery (); });
    });
    const auto outbound_channels = outbound_only.channels ();
    if (outbound_channels.size () != 1 || outbound_channels[0].server.enabled || !outbound_channels[0].client.enabled
        || !outbound_channels[0].publisher.enabled) {
        return 8;
    }

    zlink::framework::zlink_builder_t fanout;
    fanout.channel ("broadcast", [] (zlink::framework::channel_builder_t &channel) {
        channel.enable_publisher (
          [] (zlink::framework::capability_builder_t &publisher) { publisher.bind ("tcp://127.0.0.1:7351"); });
        channel.enable_subscriber ([] (zlink::framework::capability_builder_t &subscriber) {
            subscriber.connect ("tcp://127.0.0.1:7351").connect ("tcp://127.0.0.1:7352");
        });
    });
    auto fanout_manager = zlink::framework::detail::channel_runtime_manager_t::from (fanout);
    fanout_manager.initialize_publisher_channels ();
    fanout_manager.initialize_inbound_channels ();
    auto &fanout_publisher = fanout_manager.get_or_create_publisher_bundle ("broadcast");
    if (!fanout_publisher.contains_manual_connection ("tcp://127.0.0.1:7351")
        || fanout_manager.monitoring_source ("broadcast.publisher") != "broadcast.publisher"
        || fanout_manager.monitoring_source ("broadcast.subscriber") != "broadcast.subscriber") {
        return 74;
    }

    zlink::framework::zlink_builder_t local_server;
    local_server.channel ("local", [] (zlink::framework::channel_builder_t &channel) {
        channel.enable_server (
          [] (zlink::framework::capability_builder_t &server) { server.bind ("tcp://127.0.0.1:7401"); });
    });

    zlink::framework::service_collection_t services;
    services.add_singleton<local_handler_t> ();
    auto provider = services.build_provider ();

    zlink::framework::serializer_registry_t serializers;
    add_int_serializer<request_t> (serializers);
    add_int_serializer<reply_t> (serializers);
    add_int_serializer<event_t> (serializers);

    zlink::framework::handler_registry_t handlers;
    handlers.on_request<local_handler_t, request_t, reply_t> ("local", "request", &local_handler_t::handle_request,
                                                              {.packet_name = "request"});
    handlers.on_send<local_handler_t, event_t> ("local", "send", &local_handler_t::handle_send,
                                                {.packet_name = "event"});

    auto local_runtime = zlink::framework::detail::channel_runtime_t::from (local_server.message_bus ());
    auto local_reply = local_runtime.dispatch_request ("local", "request", "request", provider, serializers, handlers,
                                                       zlink::message_t::from (std::string ("23")));
    if (!local_reply || serializers.get<reply_t> ().deserialize (local_reply.value ()).value != 123) {
        return 9;
    }

    zlink::framework::runtime::messaging::envelope_codec_t envelope_codec;
    zlink::framework::runtime::messaging::envelope_header_t request_header;
    request_header.kind = zlink::framework::runtime::messaging::message_kind_t::request;
    request_header.channel_name = "local";
    request_header.message_name = "request";
    request_header.topic = "request";
    request_header.correlation_id = "corr-1";
    auto request_parts =
      envelope_codec.encode_raw_body_parts (request_header, zlink::message_t::from (std::string ("24")));
    zlink::framework::detail::channel_packet_dispatcher_t packet_dispatcher (local_runtime);
    const auto packet_reply =
      packet_dispatcher.dispatch_server_message ("local", request_parts, provider, serializers, handlers);
    if (!packet_reply) {
        return 18;
    }
    const auto packet_reply_header = envelope_codec.decode_header (packet_reply.value ());
    const auto packet_reply_body = envelope_codec.decode_body (packet_reply.value ());
    if (!packet_reply_header
        || packet_reply_header.value ().kind != zlink::framework::runtime::messaging::message_kind_t::response
        || packet_reply_header.value ().correlation_id != "corr-1" || !packet_reply_body
        || serializers.get<reply_t> ().deserialize (packet_reply_body.value ()).value != 124) {
        return 19;
    }

    request_header.message_name = "missing";
    auto missing_parts =
      envelope_codec.encode_raw_body_parts (request_header, zlink::message_t::from (std::string ("24")));
    const auto packet_error =
      packet_dispatcher.dispatch_server_message ("local", missing_parts, provider, serializers, handlers);
    if (!packet_error) {
        return 20;
    }
    const auto packet_error_header = envelope_codec.decode_header (packet_error.value ());
    if (!packet_error_header
        || packet_error_header.value ().kind != zlink::framework::runtime::messaging::message_kind_t::error
        || packet_error_header.value ().error_code.value_or ("") != "handler_not_found") {
        return 21;
    }
    zlink::framework::detail::channel_reply_writer_t reply_writer;
    const zlink::framework::framework_error_kind_t reply_error_kinds[] = {
      zlink::framework::framework_error_kind_t::route_not_connected,
      zlink::framework::framework_error_kind_t::route_handler_not_found,
      zlink::framework::framework_error_kind_t::request_target_not_found,
      zlink::framework::framework_error_kind_t::request_rejected,
      zlink::framework::framework_error_kind_t::request_protocol_error,
      zlink::framework::framework_error_kind_t::timeout,
      zlink::framework::framework_error_kind_t::shutdown,
      zlink::framework::framework_error_kind_t::disconnected,
      zlink::framework::framework_error_kind_t::closed,
      zlink::framework::framework_error_kind_t::request_failed};
    const std::string reply_error_codes[] = {"route_not_connected",
                                             "route_handler_not_found",
                                             "request_target_not_found",
                                             "request_rejected",
                                             "request_protocol_error",
                                             "timeout",
                                             "shutdown",
                                             "disconnected",
                                             "closed",
                                             "request_failed"};
    for (std::size_t index = 0; index < std::size (reply_error_kinds); ++index) {
        zlink::framework::framework_exception_t reply_error (reply_error_kinds[index], "mapped error");
        const auto error_header = reply_writer.create_error_header ("local", request_header, reply_error);
        if (error_header.kind != zlink::framework::runtime::messaging::message_kind_t::error
            || error_header.error_code.value_or ("") != reply_error_codes[index]
            || error_header.error_message.value_or ("") != "mapped error") {
            return 72;
        }
    }

    zlink::framework::runtime::messaging::message_parts_t missing_body_parts (
      std::vector<zlink::message_t>{envelope_codec.encode_header (request_header)});
    const auto missing_body_error =
      packet_dispatcher.dispatch_server_message ("local", missing_body_parts, provider, serializers, handlers);
    if (!missing_body_error) {
        return 63;
    }
    const auto missing_body_header = envelope_codec.decode_header (missing_body_error.value ());
    if (!missing_body_header
        || missing_body_header.value ().kind != zlink::framework::runtime::messaging::message_kind_t::error
        || missing_body_header.value ().correlation_id != "corr-1"
        || missing_body_header.value ().error_code.value_or ("") != "request_protocol_error") {
        return 64;
    }

    auto local_send = local_runtime.dispatch_send ("local", "send", "event", provider, serializers, handlers,
                                                   zlink::message_t::from (std::string ("31")));
    if (!local_send || provider.get_required<local_handler_t> ().last_event != 31) {
        return 10;
    }

    auto not_server = local_runtime.dispatch_request ("profile", "request", "request", provider, serializers, handlers,
                                                      zlink::message_t::from (std::string ("1")));
    if (not_server || not_server.error_kind () != zlink::framework::framework_error_kind_t::route_not_connected) {
        return 11;
    }

    auto reservation = outbound_runtime.reserve_outbound_request ("profile");
    if (!reservation || outbound_runtime.pending_count () != 1) {
        return 12;
    }
    auto unmatched_reply = outbound_runtime.complete_outbound_reply (reservation.value () + 1000);
    if (unmatched_reply
        || unmatched_reply.error_kind () != zlink::framework::framework_error_kind_t::request_protocol_error) {
        return 13;
    }
    auto matched_reply = outbound_runtime.complete_outbound_reply (reservation.value ());
    if (!matched_reply || outbound_runtime.pending_count () != 0) {
        return 14;
    }

    zlink::framework::detail::channel_runtime_bundle_t bundle;
    if (!bundle.try_add_manual_connection ("tcp://127.0.0.1:7401")
        || bundle.try_add_manual_connection ("tcp://127.0.0.1:7401")
        || !bundle.contains_manual_connection ("tcp://127.0.0.1:7401")) {
        return 22;
    }
    bundle.try_add_manual_connection ("tcp://127.0.0.1:7400");
    const auto manual_connections = bundle.list_manual_connections ();
    if (manual_connections.size () != 2 || manual_connections[0] != "tcp://127.0.0.1:7400"
        || manual_connections[1] != "tcp://127.0.0.1:7401") {
        return 23;
    }
    bundle.remove_manual_connection ("tcp://127.0.0.1:7401");
    if (bundle.contains_manual_connection ("tcp://127.0.0.1:7401")) {
        return 24;
    }

    zlink::framework::detail::channel_receive_loop_t receive_loop (
      bundle, zlink::framework::detail::channel_message_pump_t (
                zlink::framework::detail::channel_packet_dispatcher_t (local_runtime)));
    receive_loop.enqueue_server_message (request_parts);
    if (receive_loop.pending_message_count () != 1) {
        return 25;
    }
    const auto receive_result = receive_loop.drain_server_messages ("local", provider, serializers, handlers);
    if (!receive_result || receive_result.value ().dispatched != 1 || receive_result.value ().replies.size () != 1
        || receive_loop.pending_message_count () != 0 || bundle.receive_active ()) {
        return 26;
    }
    const auto loop_reply_header = envelope_codec.decode_header (receive_result.value ().replies[0]);
    const auto loop_reply_body = envelope_codec.decode_body (receive_result.value ().replies[0]);
    if (!loop_reply_header
        || loop_reply_header.value ().kind != zlink::framework::runtime::messaging::message_kind_t::response
        || !loop_reply_body || serializers.get<reply_t> ().deserialize (loop_reply_body.value ()).value != 124) {
        return 27;
    }
    if (!bundle.try_enter_receive ()) {
        return 28;
    }
    const auto reentrant_result = receive_loop.drain_server_messages ("local", provider, serializers, handlers);
    bundle.leave_receive ();
    if (reentrant_result
        || reentrant_result.error_kind () != zlink::framework::framework_error_kind_t::request_rejected) {
        return 29;
    }

    zlink::framework::detail::route_connection_set_t route_connections;
    if (!route_connections.connect ("tcp://route-b:7500") || !route_connections.connect ("tcp://route-a:7500")
        || route_connections.connect ("tcp://route-a:7500")) {
        return 30;
    }
    const auto route_connection_list = route_connections.list ();
    if (route_connection_list.size () != 2 || route_connection_list[0] != "tcp://route-a:7500"
        || route_connection_list[1] != "tcp://route-b:7500") {
        return 31;
    }
    if (!route_connections.disconnect ("tcp://route-b:7500") || route_connections.contains ("tcp://route-b:7500")) {
        return 32;
    }

    zlink::framework::detail::route_channel_runtime_t route_runtime ("game.route");
    const auto target_node = zlink::routing_id_t::from (std::string ("remote-node"));
    auto disconnected_send = route_runtime.submit_send (target_node, "event", event_t{77}, serializers);
    if (disconnected_send
        || disconnected_send.error_kind () != zlink::framework::framework_error_kind_t::route_not_connected) {
        return 33;
    }
    route_runtime.start ();
    route_runtime.connect ("tcp://route-peer:7500");
    auto route_send = route_runtime.submit_send (target_node, "event", event_t{78}, serializers);
    if (!route_send || route_runtime.outbound_packets ().size () != 1
        || route_runtime.outbound_packets ()[0].request_seq.has_value ()) {
        return 34;
    }
    const auto route_send_header = envelope_codec.decode_header (route_runtime.outbound_packets ()[0].parts);
    if (!route_send_header
        || route_send_header.value ().kind != zlink::framework::runtime::messaging::message_kind_t::command
        || route_send_header.value ().channel_name != "game.route"
        || route_send_header.value ().message_name != "event") {
        return 35;
    }
    auto route_request =
      route_runtime.submit_request (target_node, "request", request_t{79}, serializers, std::chrono::milliseconds (25));
    if (!route_request || route_runtime.pending_request_count () != 1 || route_runtime.outbound_packets ().size () != 2
        || !route_runtime.outbound_packets ()[1].request_seq.has_value ()) {
        return 36;
    }
    const auto route_request_header = envelope_codec.decode_header (route_runtime.outbound_packets ()[1].parts);
    if (!route_request_header
        || route_request_header.value ().kind != zlink::framework::runtime::messaging::message_kind_t::request
        || route_request_header.value ().channel_name != "game.route"
        || route_request_header.value ().message_name != "request"
        || !route_request_header.value ().deadline.has_value ()) {
        return 37;
    }
    const auto target_spot = zlink::routing_id_t::from (std::string ("remote-spot"));
    auto spot_request = route_runtime.request_to_spot_parts (target_node, target_spot, request_parts);
    if (!spot_request || route_runtime.pending_request_count () != 2
        || route_runtime.outbound_packets ().back ().target_spot_rid.value () != target_spot) {
        return 38;
    }
    if (!route_runtime.complete_request (route_request.value ()) || route_runtime.pending_request_count () != 1) {
        return 39;
    }
    route_runtime.stop ();
    if (route_runtime.running () || route_runtime.pending_request_count () != 0) {
        return 40;
    }

    zlink::framework::detail::channel_runtime_manager_t manager =
      zlink::framework::detail::channel_runtime_manager_t::from (zlink.message_bus ());
    manager.initialize_client_channels ();
    auto &client_bundle = manager.get_or_create_client_bundle ("profile");
    if (!client_bundle.contains_manual_connection ("tcp://127.0.0.1:7101")) {
        return 41;
    }
    manager.initialize_publisher_channels ();
    auto &publisher_bundle = manager.get_or_create_publisher_bundle ("events");
    if (!publisher_bundle.contains_manual_connection ("tcp://127.0.0.1:7201")) {
        return 42;
    }
    if (manager.monitoring_source ("profile.client") != "profile.client"
        || manager.monitoring_source ("events.publisher") != "events.publisher") {
        return 43;
    }
    bool missing_monitoring_failed = false;
    try {
        (void) manager.monitoring_source ("profile.server");
    }
    catch (const zlink::framework::framework_exception_t &error) {
        missing_monitoring_failed = error.kind () == zlink::framework::framework_error_kind_t::request_protocol_error;
    }
    if (!missing_monitoring_failed) {
        return 44;
    }
    manager.initialize_route_channels ({"profile"});
    auto &managed_route = manager.get_route_channel ("profile");
    if (!managed_route.running () || managed_route.list_connections ().empty ()) {
        return 45;
    }

    zlink::framework::detail::route_receive_pump_t route_pump{
      zlink::framework::detail::route_packet_dispatcher_t ("game.route")};
    route_pump.enqueue (zlink::framework::detail::route_received_packet_t{
      zlink::routing_id_t::from (std::string ("source-node")), 77, request_parts});
    const auto route_receive = route_pump.drain ();
    if (!route_receive || route_receive.value ().dispatched != 1 || route_receive.value ().replies.size () != 1
        || route_receive.value ().replies[0].request_seq.value_or (0) != 77) {
        return 46;
    }
    const auto route_error_header = envelope_codec.decode_header (route_receive.value ().replies[0].parts);
    if (!route_error_header
        || route_error_header.value ().kind != zlink::framework::runtime::messaging::message_kind_t::error
        || route_error_header.value ().channel_name != "game.route"
        || route_error_header.value ().error_code.value_or ("") != "route_handler_not_found") {
        return 47;
    }

    zlink::framework::detail::route_handler_registry_t route_handlers;
    route_handlers.on_request<local_handler_t, request_t, reply_t> ("game.route", "request",
                                                                    &local_handler_t::handle_route_request);
    route_handlers.on_send<local_handler_t, event_t> ("game.route", "event", &local_handler_t::handle_route_send);
    zlink::framework::detail::no_route_internal_packet_dispatcher_t no_internal;
    zlink::framework::detail::route_receive_pump_t route_handler_pump{
      zlink::framework::detail::route_packet_dispatcher_t ("game.route", provider, serializers, route_handlers,
                                                           no_internal)};
    route_handler_pump.enqueue (zlink::framework::detail::route_received_packet_t{
      zlink::routing_id_t::from (std::string ("source-node")), 78, request_parts});
    const auto route_handler_receive = route_handler_pump.drain ();
    if (!route_handler_receive || route_handler_receive.value ().replies.size () != 1) {
        return 48;
    }
    const auto route_reply_body = envelope_codec.decode_body (route_handler_receive.value ().replies[0].parts);
    if (!route_reply_body || serializers.get<reply_t> ().deserialize (route_reply_body.value ()).value != 224
        || provider.get_required<local_handler_t> ().last_route_request != 24
        || provider.get_required<local_handler_t> ().last_route_source != "source-node") {
        return 49;
    }
    route_handler_pump.enqueue (
      zlink::framework::detail::route_received_packet_t{zlink::routing_id_t::from (std::string ("source-node")),
                                                        std::nullopt, route_runtime.outbound_packets ()[0].parts});
    const auto route_send_receive = route_handler_pump.drain ();
    if (!route_send_receive || !route_send_receive.value ().replies.empty ()
        || provider.get_required<local_handler_t> ().last_route_event != 78) {
        return 50;
    }

    local_internal_dispatcher_t internal_a;
    zlink::framework::detail::composite_route_internal_packet_dispatcher_t composite_internal;
    composite_internal.add (internal_a);
    zlink::framework::runtime::messaging::envelope_header_t internal_header;
    internal_header.kind = zlink::framework::runtime::messaging::message_kind_t::request;
    internal_header.channel_name = "game.route";
    internal_header.message_name = "internal.request";
    internal_header.correlation_id = "internal-1";
    auto internal_parts =
      envelope_codec.encode_raw_body_parts (internal_header, zlink::message_t::from (std::string ("{}")));
    zlink::framework::detail::route_receive_pump_t internal_pump{zlink::framework::detail::route_packet_dispatcher_t (
      "game.route", provider, serializers, route_handlers, composite_internal)};
    internal_pump.enqueue (zlink::framework::detail::route_received_packet_t{
      zlink::routing_id_t::from (std::string ("source-node")), 79, internal_parts});
    const auto internal_receive = internal_pump.drain ();
    if (!internal_receive || internal_receive.value ().replies.size () != 1) {
        return 51;
    }
    const auto internal_reply_body = envelope_codec.decode_body (internal_receive.value ().replies[0].parts);
    if (!internal_reply_body || internal_reply_body.value ().to_string () != "88") {
        return 52;
    }
    if (no_internal.can_handle_send ("internal.send") || no_internal.can_handle_request ("internal.request")) {
        return 65;
    }
    const auto no_internal_send = no_internal.dispatch_send (zlink::framework::detail::route_received_packet_t{
      zlink::routing_id_t::from (std::string ("source-node")), std::nullopt, internal_parts});
    if (no_internal_send
        || no_internal_send.error_kind () != zlink::framework::framework_error_kind_t::route_handler_not_found) {
        return 66;
    }
    const auto no_internal_request = no_internal.dispatch_request (
      zlink::framework::detail::route_received_packet_t{zlink::routing_id_t::from (std::string ("source-node")), 81,
                                                        internal_parts},
      internal_header);
    if (no_internal_request
        || no_internal_request.error_kind () != zlink::framework::framework_error_kind_t::route_handler_not_found) {
        return 67;
    }
    zlink::framework::runtime::messaging::envelope_header_t internal_send_header;
    internal_send_header.kind = zlink::framework::runtime::messaging::message_kind_t::command;
    internal_send_header.channel_name = "game.route";
    internal_send_header.message_name = "internal.send";
    auto internal_send_parts =
      envelope_codec.encode_raw_body_parts (internal_send_header, zlink::message_t::from (std::string ("{}")));
    if (!composite_internal.can_handle_send ("internal.send")
        || !composite_internal.dispatch_send (zlink::framework::detail::route_received_packet_t{
          zlink::routing_id_t::from (std::string ("source-node")), std::nullopt, internal_send_parts})
        || internal_a.send_count != 1) {
        return 68;
    }
    zlink::framework::runtime::messaging::envelope_header_t unsupported_internal_header;
    unsupported_internal_header.kind = zlink::framework::runtime::messaging::message_kind_t::command;
    unsupported_internal_header.channel_name = "game.route";
    unsupported_internal_header.message_name = "internal.unsupported";
    auto unsupported_internal_parts =
      envelope_codec.encode_raw_body_parts (unsupported_internal_header, zlink::message_t::from (std::string ("{}")));
    const auto unsupported_internal_send =
      composite_internal.dispatch_send (zlink::framework::detail::route_received_packet_t{
        zlink::routing_id_t::from (std::string ("source-node")), std::nullopt, unsupported_internal_parts});
    if (unsupported_internal_send
        || unsupported_internal_send.error_kind ()
             != zlink::framework::framework_error_kind_t::route_handler_not_found) {
        return 69;
    }
    unsupported_internal_header.kind = zlink::framework::runtime::messaging::message_kind_t::request;
    const auto unsupported_internal_request = composite_internal.dispatch_request (
      zlink::framework::detail::route_received_packet_t{zlink::routing_id_t::from (std::string ("source-node")), 82,
                                                        unsupported_internal_parts},
      unsupported_internal_header);
    if (unsupported_internal_request
        || unsupported_internal_request.error_kind ()
             != zlink::framework::framework_error_kind_t::route_handler_not_found) {
        return 70;
    }
    const auto invalid_internal_send =
      composite_internal.dispatch_send (zlink::framework::detail::route_received_packet_t{
        zlink::routing_id_t::from (std::string ("source-node")), std::nullopt,
        zlink::framework::runtime::messaging::message_parts_t (
          std::vector<zlink::message_t>{zlink::message_t::from (std::string ("not-json"))})});
    if (invalid_internal_send
        || invalid_internal_send.error_kind () != zlink::framework::framework_error_kind_t::request_protocol_error) {
        return 71;
    }

    zlink::framework::detail::route_channel_registration_t route_registration ("registered.route");
    route_registration.bind ("tcp://registered-bind:7600")
      .connect ("tcp://registered-peer:7601")
      .add_handler_group ("game")
      .add_request_handler<local_handler_t, request_t, reply_t> ("request", &local_handler_t::handle_route_request)
      .add_send_handler<local_handler_t, event_t> ("event", &local_handler_t::handle_route_send);
    zlink::framework::detail::route_channel_initializer_t route_initializer;
    auto initialized_route = route_initializer.initialize (route_registration);
    if (!initialized_route.runtime->running () || initialized_route.runtime->list_connections ().size () != 2
        || route_registration.handler_groups ().size () != 1
        || initialized_route.handlers.find ("registered.route",
                                            zlink::framework::runtime::messaging::message_kind_t::request, "request")
             == nullptr) {
        return 53;
    }
    zlink::framework::runtime::messaging::envelope_header_t registered_header;
    registered_header.kind = zlink::framework::runtime::messaging::message_kind_t::request;
    registered_header.channel_name = "registered.route";
    registered_header.message_name = "request";
    registered_header.correlation_id = "registered-1";
    auto registered_parts =
      envelope_codec.encode_raw_body_parts (registered_header, zlink::message_t::from (std::string ("25")));
    zlink::framework::detail::route_receive_pump_t registered_pump{zlink::framework::detail::route_packet_dispatcher_t (
      "registered.route", provider, serializers, initialized_route.handlers, no_internal)};
    registered_pump.enqueue (zlink::framework::detail::route_received_packet_t{
      zlink::routing_id_t::from (std::string ("source-node")), 80, registered_parts});
    auto registered_receive = registered_pump.drain ();
    if (!registered_receive || registered_receive.value ().replies.size () != 1) {
        return 54;
    }
    const auto registered_reply_body = envelope_codec.decode_body (registered_receive.value ().replies[0].parts);
    if (!registered_reply_body
        || serializers.get<reply_t> ().deserialize (registered_reply_body.value ()).value != 225) {
        return 55;
    }

    zlink::framework::zlink_builder_t public_route_builder;
    public_route_builder.route_channel ("public.route", [] (zlink::framework::route_channel_builder_t &route) {
        route.bind ("tcp://public-bind:7700")
          .connect ("tcp://public-peer:7701")
          .enable_spot_route_egress ("play.route")
          .add_handler_group ("public")
          .add_request_handler<local_handler_t, request_t, reply_t> ("request", &local_handler_t::handle_route_request)
          .add_send_handler<local_handler_t, event_t> ("event", &local_handler_t::handle_route_send);
    });
    if (public_route_builder.route_channels ().size () != 1
        || public_route_builder.route_channels ()[0] != "public.route") {
        return 56;
    }
    auto public_manager = zlink::framework::detail::channel_runtime_manager_t::from (public_route_builder);
    public_manager.initialize_route_channels (public_route_builder);
    auto &public_route = public_manager.get_route_channel ("public.route");
    if (!public_route.running () || public_route.list_connections ().size () != 2
        || !public_route.spot_route_egress_target () || *public_route.spot_route_egress_target () != "play.route") {
        return 57;
    }
    auto public_route_client = public_route_builder.route_client (serializers);
    zlink::context_t native_route_context;
    zlink::router_socket_t native_router (native_route_context);
    zlink::framework::detail::backend::native_route_backend_t native_backend (native_router);
    const auto native_empty_send = native_backend.submit_send (zlink::routing_id_t::from (std::string ("target-node")),
                                                               zlink::framework::runtime::messaging::message_parts_t{});
    const auto native_empty_request = native_backend.submit_request (
      zlink::routing_id_t::from (std::string ("target-node")), zlink::framework::runtime::messaging::message_parts_t{},
      std::chrono::milliseconds (1));
    if (native_empty_send
        || native_empty_send.error_kind () != zlink::framework::framework_error_kind_t::request_protocol_error
        || native_empty_request
        || native_empty_request.error_kind () != zlink::framework::framework_error_kind_t::request_protocol_error) {
        return 73;
    }
    public_route.attach_native_backend (native_backend);
    int send_backend_seen = 0;
    public_route.set_send_backend (
      [&send_backend_seen, &envelope_codec] (
        const zlink::routing_id_t &target,
        const zlink::framework::runtime::messaging::message_parts_t &parts) -> zlink::framework::result_t<void> {
          auto header = envelope_codec.decode_header (parts);
          if (target.to_string () != "target-node" || !header || header.value ().message_name != "client.event"
              || header.value ().metadata.find ("trace-id") == header.value ().metadata.end ()
              || header.value ().metadata.at ("trace-id") != "trace-send") {
              return zlink::framework::result_t<void>::failure (
                zlink::framework::framework_error_kind_t::request_failed,
                "route send backend received unexpected packet");
          }
          ++send_backend_seen;
          return zlink::framework::result_t<void>::success ();
      });
    auto public_route_send =
      public_route_client.send ("public.route", zlink::routing_id_t::from (std::string ("target-node")), event_t{31})
        .packet_name ("client.event")
        .metadata ("trace-id", "trace-send")
        .submit ()
        .result ();
    if (!public_route_send || public_route.outbound_packets ().size () != 1 || send_backend_seen != 1) {
        return 58;
    }
    auto public_send_header = envelope_codec.decode_header (public_route.outbound_packets ()[0].parts);
    if (!public_send_header
        || public_send_header.value ().kind != zlink::framework::runtime::messaging::message_kind_t::command
        || public_send_header.value ().channel_name != "public.route"
        || public_send_header.value ().message_name != "client.event"
        || public_send_header.value ().metadata.at ("trace-id") != "trace-send"
        || public_route.outbound_packets ()[0].target_node_rid.to_string () != "target-node") {
        return 59;
    }
    auto public_route_request =
      public_route_client
        .request ("public.route", zlink::routing_id_t::from (std::string ("target-node")), request_t{41})
        .packet_name ("client.request")
        .metadata ("trace-id", "trace-request")
        .timeout (std::chrono::milliseconds (25))
        .submit ()
        .result ();
    if (!public_route_request || public_route_request.value () != 1 || public_route.pending_request_count () != 1
        || public_route.outbound_packets ().size () != 2 || !public_route.outbound_packets ()[1].request_seq) {
        return 60;
    }
    auto public_request_header = envelope_codec.decode_header (public_route.outbound_packets ()[1].parts);
    if (!public_request_header
        || public_request_header.value ().kind != zlink::framework::runtime::messaging::message_kind_t::request
        || public_request_header.value ().channel_name != "public.route"
        || public_request_header.value ().message_name != "client.request"
        || public_request_header.value ().metadata.at ("trace-id") != "trace-request"
        || !public_request_header.value ().deadline) {
        return 61;
    }
    public_route.set_request_backend (
      [&envelope_codec, &serializers] (const zlink::routing_id_t &target,
                                       const zlink::framework::runtime::messaging::message_parts_t &parts,
                                       std::chrono::milliseconds timeout)
        -> zlink::framework::result_t<zlink::framework::runtime::messaging::message_parts_t> {
          if (target.to_string () != "target-node" || timeout != std::chrono::milliseconds (50)) {
              return zlink::framework::result_t<zlink::framework::runtime::messaging::message_parts_t>::failure (
                zlink::framework::framework_error_kind_t::request_failed,
                "typed route backend received unexpected target or timeout");
          }
          auto header = envelope_codec.decode_header (parts);
          auto body = envelope_codec.decode_body (parts);
          if (!header || !body || header.value ().message_name != "typed.client.request"
              || header.value ().metadata.find ("trace-id") == header.value ().metadata.end ()
              || header.value ().metadata.at ("trace-id") != "trace-typed"
              || serializers.get<request_t> ().deserialize (body.value ()).value != 51) {
              return zlink::framework::result_t<zlink::framework::runtime::messaging::message_parts_t>::failure (
                zlink::framework::framework_error_kind_t::request_failed,
                "typed route backend received unexpected payload");
          }
          zlink::framework::runtime::messaging::envelope_header_t reply_header;
          reply_header.kind = zlink::framework::runtime::messaging::message_kind_t::response;
          reply_header.channel_name = "public.route";
          reply_header.message_name = header.value ().message_name;
          reply_header.content_type = header.value ().content_type;
          reply_t reply{351};
          return zlink::framework::result_t<zlink::framework::runtime::messaging::message_parts_t>::success (
            envelope_codec.encode_parts (reply_header, std::type_index (typeid (reply_t)), &reply, serializers));
      });
    auto public_typed_reply =
      public_route_client
        .request<request_t, reply_t> ("public.route", zlink::routing_id_t::from (std::string ("target-node")),
                                      request_t{51})
        .packet_name ("typed.client.request")
        .metadata ("trace-id", "trace-typed")
        .timeout (std::chrono::milliseconds (50))
        .submit ()
        .result ();
    if (!public_typed_reply || public_typed_reply.value ().value != 351 || public_route.outbound_packets ().size () != 3
        || public_route.pending_request_count () != 1) {
        return 62;
    }

    return 0;
}
