/* SPDX-License-Identifier: MPL-2.0 */

#include "runtime/channels/channel_reply_writer.hpp"

#include <utility>

namespace zlink::framework::detail
{

namespace
{

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
        case framework_error_kind_t::timeout:
            return "timeout";
        case framework_error_kind_t::shutdown:
            return "shutdown";
        case framework_error_kind_t::disconnected:
            return "disconnected";
        case framework_error_kind_t::closed:
            return "closed";
        default:
            return "request_failed";
    }
}

} // namespace

runtime::messaging::envelope_header_t
channel_reply_writer_t::create_reply_header (runtime::messaging::message_kind_t kind,
                                             std::string channel_name,
                                             const runtime::messaging::envelope_header_t &request) const
{
    runtime::messaging::envelope_header_t header;
    header.kind = kind;
    header.channel_name = std::move (channel_name);
    header.message_name = request.message_name;
    header.content_type = runtime::messaging::envelope_codec_t::default_content_type;
    header.correlation_id = request.correlation_id;
    return header;
}

runtime::messaging::envelope_header_t
channel_reply_writer_t::create_error_header (std::string channel_name,
                                             const runtime::messaging::envelope_header_t &request,
                                             const framework_exception_t &error) const
{
    auto header = create_reply_header (runtime::messaging::message_kind_t::error, std::move (channel_name), request);
    header.error_code = error_code_name (error.kind ());
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
