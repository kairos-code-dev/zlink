/* SPDX-License-Identifier: MPL-2.0 */

#include "runtime/channels/channel_packet_dispatcher.hpp"

#include "runtime/channels/channel_reply_writer.hpp"

#include <utility>

namespace zlink::framework::detail
{

channel_packet_dispatcher_t::channel_packet_dispatcher_t (
  channel_runtime_t runtime)
  : _runtime (std::move (runtime))
{
}

result_t<runtime::messaging::message_parts_t>
channel_packet_dispatcher_t::dispatch_server_message (
  std::string channel_name,
  const runtime::messaging::message_parts_t &parts,
  service_provider_t &services,
  serializer_registry_t &serializers,
  const handler_registry_t &handlers) const
{
  runtime::messaging::envelope_codec_t codec;
  auto header = codec.decode_header (parts);
  if (!header) {
    return result_t<runtime::messaging::message_parts_t>::failure (
      header.error_kind (),
      header.error () ? header.error ()->what ()
                      : "channel envelope header decode failed");
  }
  auto body = codec.decode_body (parts);
  if (!body) {
    if (header.value ().kind == runtime::messaging::message_kind_t::request) {
      channel_reply_writer_t writer;
      framework_exception_t error (
        body.error_kind (),
        body.error () ? body.error ()->what () : "channel body decode failed");
      return result_t<runtime::messaging::message_parts_t>::success (
        writer.reply_raw_envelope (
          writer.create_error_header (std::move (channel_name),
                                      header.value (),
                                      error),
          zlink::message_t::from ("")));
    }
    return result_t<runtime::messaging::message_parts_t>::failure (
      body.error_kind (),
      body.error () ? body.error ()->what () : "channel body decode failed");
  }

  if (header.value ().kind == runtime::messaging::message_kind_t::request) {
    auto reply = _runtime.dispatch_request (
      channel_name,
      header.value ().topic.value_or ("Channel"),
      header.value ().message_name,
      services,
      serializers,
      handlers,
      body.value ());
    channel_reply_writer_t writer;
    if (!reply) {
      framework_exception_t error (
        reply.error_kind (),
        reply.error () ? reply.error ()->what () : "channel request failed");
      return result_t<runtime::messaging::message_parts_t>::success (
        writer.reply_raw_envelope (
          writer.create_error_header (std::move (channel_name),
                                      header.value (),
                                      error),
          zlink::message_t::from ("")));
    }
    return result_t<runtime::messaging::message_parts_t>::success (
      writer.reply_raw_envelope (
        writer.create_reply_header (
          runtime::messaging::message_kind_t::response,
          std::move (channel_name),
          header.value ()),
        reply.value ()));
  }

  if (header.value ().kind == runtime::messaging::message_kind_t::command) {
    auto result = _runtime.dispatch_send (
      std::move (channel_name),
      header.value ().topic.value_or ("Channel"),
      header.value ().message_name,
      services,
      serializers,
      handlers,
      body.value ());
    if (!result) {
      return result_t<runtime::messaging::message_parts_t>::failure (
        result.error_kind (),
        result.error () ? result.error ()->what () : "channel command failed");
    }
    return result_t<runtime::messaging::message_parts_t>::success (
      runtime::messaging::message_parts_t {});
  }

  return result_t<runtime::messaging::message_parts_t>::failure (
    framework_error_kind_t::request_protocol_error,
    "unsupported channel message kind");
}

} // namespace zlink::framework::detail
