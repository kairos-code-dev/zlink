/* SPDX-License-Identifier: MPL-2.0 */

#include "runtime/channels/route_packet_dispatcher.hpp"

#include "runtime/channels/channel_reply_writer.hpp"

#include <utility>

namespace zlink::framework::detail
{

route_packet_dispatcher_t::route_packet_dispatcher_t (std::string router_channel_id) :
    _router_channel_id (std::move (router_channel_id))
{
}

route_packet_dispatcher_t::route_packet_dispatcher_t (std::string router_channel_id,
                                                      service_provider_t &services,
                                                      serializer_registry_t &serializers,
                                                      const route_handler_registry_t &handlers,
                                                      const route_internal_packet_dispatcher_t &internal_packets) :
    _router_channel_id (std::move (router_channel_id)),
    _services (&services),
    _serializers (&serializers),
    _handlers (&handlers),
    _internal_packets (&internal_packets)
{
}

result_t<std::optional<route_dispatch_reply_t>>
route_packet_dispatcher_t::dispatch (const route_received_packet_t &received) const
{
    runtime::messaging::envelope_codec_t codec;
    auto header = codec.decode_header (received.parts);
    if (!header) {
        return result_t<std::optional<route_dispatch_reply_t>>::failure (
          header.error_kind (), header.error () ? header.error ()->what () : "route envelope header decode failed");
    }

    switch (header.value ().kind) {
        case runtime::messaging::message_kind_t::command:
            return dispatch_send (received, header.value ());
        case runtime::messaging::message_kind_t::request:
            return dispatch_request (received, header.value ());
        default:
            return result_t<std::optional<route_dispatch_reply_t>>::failure (
              framework_error_kind_t::request_protocol_error, "unsupported route message kind");
    }
}

result_t<std::optional<route_dispatch_reply_t>>
route_packet_dispatcher_t::dispatch_send (const route_received_packet_t &received,
                                          const runtime::messaging::envelope_header_t &header) const
{
    if (_internal_packets != nullptr && _internal_packets->can_handle_send (header.message_name)) {
        auto dispatched = _internal_packets->dispatch_send (received);
        if (!dispatched) {
            return result_t<std::optional<route_dispatch_reply_t>>::failure (
              dispatched.error_kind (),
              dispatched.error () ? dispatched.error ()->what () : "route internal send failed");
        }
        return result_t<std::optional<route_dispatch_reply_t>>::success (std::nullopt);
    }
    if (_handlers == nullptr || _services == nullptr || _serializers == nullptr
        || _handlers->find (_router_channel_id, runtime::messaging::message_kind_t::command, header.message_name)
             == nullptr) {
        return result_t<std::optional<route_dispatch_reply_t>>::success (std::nullopt);
    }

    auto body = runtime::messaging::envelope_codec_t{}.decode_body (received.parts);
    if (!body) {
        return result_t<std::optional<route_dispatch_reply_t>>::failure (
          body.error_kind (), body.error () ? body.error ()->what () : "route command body missing");
    }
    framework::route_handler_context_t context{_router_channel_id, received.source_node_rid, header.message_name,
                                               header.content_type};
    auto dispatched = _invoker
                        .invoke_send (*_handlers, _router_channel_id, header.message_name, *_services, *_serializers,
                                      body.value (), context)
                        .result ();
    if (!dispatched) {
        return result_t<std::optional<route_dispatch_reply_t>>::failure (
          dispatched.error_kind (), dispatched.error () ? dispatched.error ()->what () : "routed send handler failed");
    }
    return result_t<std::optional<route_dispatch_reply_t>>::success (std::nullopt);
}

result_t<std::optional<route_dispatch_reply_t>>
route_packet_dispatcher_t::dispatch_request (const route_received_packet_t &received,
                                             const runtime::messaging::envelope_header_t &header) const
{
    if (_internal_packets != nullptr && _internal_packets->can_handle_request (header.message_name)) {
        auto reply = _internal_packets->dispatch_request (received, header);
        if (!reply) {
            framework_exception_t error (reply.error_kind (),
                                         reply.error () ? reply.error ()->what () : "route internal request failed");
            return reply_error (received, header, error);
        }
        channel_reply_writer_t writer;
        return result_t<std::optional<route_dispatch_reply_t>>::success (route_dispatch_reply_t{
          received.source_node_rid, received.request_seq,
          writer.reply_raw_envelope (
            writer.create_reply_header (runtime::messaging::message_kind_t::response, _router_channel_id, header),
            reply.value ())});
    }

    if (_handlers == nullptr || _services == nullptr || _serializers == nullptr
        || _handlers->find (_router_channel_id, runtime::messaging::message_kind_t::request, header.message_name)
             == nullptr) {
        framework_exception_t error (framework_error_kind_t::route_handler_not_found,
                                     "No routed request handler is registered for '" + _router_channel_id + ":"
                                       + header.message_name + "'.");
        return reply_error (received, header, error);
    }

    auto body = runtime::messaging::envelope_codec_t{}.decode_body (received.parts);
    if (!body) {
        return result_t<std::optional<route_dispatch_reply_t>>::failure (
          body.error_kind (), body.error () ? body.error ()->what () : "route request body missing");
    }
    framework::route_handler_context_t context{_router_channel_id, received.source_node_rid, header.message_name,
                                               header.content_type};
    auto reply = _invoker
                   .invoke_request (*_handlers, _router_channel_id, header.message_name, *_services, *_serializers,
                                    body.value (), context)
                   .result ();
    if (!reply) {
        framework_exception_t error (reply.error_kind (),
                                     reply.error () ? reply.error ()->what () : "routed request handler failed");
        return reply_error (received, header, error);
    }
    channel_reply_writer_t writer;
    return result_t<std::optional<route_dispatch_reply_t>>::success (route_dispatch_reply_t{
      received.source_node_rid, received.request_seq,
      writer.reply_raw_envelope (
        writer.create_reply_header (runtime::messaging::message_kind_t::response, _router_channel_id, header),
        reply.value ())});
}

result_t<std::optional<route_dispatch_reply_t>>
route_packet_dispatcher_t::reply_error (const route_received_packet_t &received,
                                        const runtime::messaging::envelope_header_t &header,
                                        const framework_exception_t &error) const
{
    channel_reply_writer_t writer;
    auto reply = writer.reply_raw_envelope (writer.create_error_header (_router_channel_id, header, error),
                                            zlink::message_t::from (""));
    return result_t<std::optional<route_dispatch_reply_t>>::success (
      route_dispatch_reply_t{received.source_node_rid, received.request_seq, std::move (reply)});
}

} // namespace zlink::framework::detail
