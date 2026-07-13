/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include "runtime/channels/channel_reply_writer.hpp"

#include <utility>

namespace zlink::framework::detail
{

namespace
{

const char *boundary_error_name (detail::boundary_error_t state) noexcept
{
    switch (state) {
        case detail::boundary_error_t::timed_out:
            return "timeout";
        case detail::boundary_error_t::shutdown:
            return "shutdown";
        case detail::boundary_error_t::disconnected:
            return "disconnected";
        case detail::boundary_error_t::closed:
            return "closed";
        case detail::boundary_error_t::cancelled:
            return "cancelled";
        case detail::boundary_error_t::stale_generation:
            return "stale_generation";
        case detail::boundary_error_t::none:
            break;
    }
    return "request_failed";
}

std::string error_code_name (framework_error_kind_t kind)
{
    switch (kind) {
        case framework_error_kind_t::handler_not_found:
            return "handler_not_found";
        case framework_error_kind_t::route_not_connected:
            return "route_not_connected";
        case framework_error_kind_t::route_handler_not_found:
            return "route_handler_not_found";
        case framework_error_kind_t::request_target_not_found:
            return "request_target_not_found";
        case framework_error_kind_t::request_rejected:
            return "request_rejected";
        case framework_error_kind_t::request_protocol_error:
            return "request_protocol_error";
        case framework_error_kind_t::payload_decode_failed:
            return "payload_decode_failed";
        case framework_error_kind_t::worker_queue_full:
            return "worker_queue_full";
        case framework_error_kind_t::worker_timed_out:
            return "worker_timed_out";
        case framework_error_kind_t::worker_failed:
            return "worker_failed";
        default:
            return "request_failed";
    }
}

} // namespace

runtime::messaging::envelope_header_t channel_reply_writer_t::create_reply_header (
  runtime::messaging::message_kind_t kind,
  std::string channel_name,
  const runtime::messaging::envelope_header_t &request) const
{
    runtime::messaging::envelope_header_t header;
    header.kind = kind;
    header.channel_name = std::move (channel_name);
    header.message_name = request.message_name;
    header.content_type = request.content_type;
    header.correlation_id = request.correlation_id;
    return header;
}

runtime::messaging::envelope_header_t
channel_reply_writer_t::create_error_header (std::string channel_name,
                                             const runtime::messaging::envelope_header_t &request,
                                             const framework_exception_t &error) const
{
    auto header = create_reply_header (runtime::messaging::message_kind_t::error,
                                       std::move (channel_name), request);
    header.error_code = detail::boundary_state (error) != detail::boundary_error_t::none
                          ? boundary_error_name (detail::boundary_state (error))
                          : error_code_name (error.kind ());
    header.error_message = error.what ();
    return header;
}

runtime::messaging::message_parts_t
channel_reply_writer_t::reply_raw_envelope (const runtime::messaging::envelope_header_t &header,
                                            zlink::message_t body) const
{
    return runtime::messaging::envelope_codec_t{}.encode_raw_body_parts (header, std::move (body));
}

} // namespace zlink::framework::detail
