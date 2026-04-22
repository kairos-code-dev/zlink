/* SPDX-License-Identifier: MPL-2.0 */

#include "sample_common.hpp"

namespace {

struct callback_result_t
{
    zlink::routing_id_t routing_id;
    std::string payload;
};

std::string packet_payload (zlink_msg_t *header_, zlink_msg_t *body_)
{
    assert (header_ != NULL);
    assert (body_ != NULL);
    assert (zlink_msg_size (header_) == 0);
    return std::string (static_cast<const char *> (zlink_msg_data (body_)),
                        zlink_msg_size (body_));
}

std::vector<unsigned char> encode_packet_frame (const std::string &payload_)
{
    std::vector<unsigned char> frame (6u + payload_.size (), 0u);
    const uint32_t body_size = static_cast<uint32_t> (payload_.size ());
    frame[2] = static_cast<unsigned char> ((body_size >> 24) & 0xFFu);
    frame[3] = static_cast<unsigned char> ((body_size >> 16) & 0xFFu);
    frame[4] = static_cast<unsigned char> ((body_size >> 8) & 0xFFu);
    frame[5] = static_cast<unsigned char> (body_size & 0xFFu);
    if (!payload_.empty ()) {
        std::memcpy (frame.data () + 6u, payload_.data (), payload_.size ());
    }
    return frame;
}

void stream_packet_callback (void *,
                             const zlink_routing_id_t *source_rid_,
                             zlink_msg_t *header_,
                             zlink_msg_t *body_,
                             void *userdata_)
{
    std::promise<callback_result_t> *result_promise =
      static_cast<std::promise<callback_result_t> *> (userdata_);
    assert (result_promise != NULL);
    assert (source_rid_ != NULL);
    assert (header_ != NULL);
    assert (body_ != NULL);

    callback_result_t result;
    result.routing_id = zlink::routing_id_t::from_bytes (
      reinterpret_cast<const uint8_t *> (source_rid_->data), source_rid_->size);
    result.payload = packet_payload (header_, body_);

    (void) zlink_msg_close (header_);
    (void) zlink_msg_close (body_);
    result_promise->set_value (result);
}

} // namespace

int main ()
{
    zlink::context_t ctx;
    zlink::stream_socket_t server (ctx);
    zlink::monitor_handle_t server_monitor = server.monitor_handle ();
    server.stream_options ().notify (false);

    server.bind ("tcp://127.0.0.1:0");
    const std::string endpoint = server.options ().last_endpoint ();
    assert (!endpoint.empty ());

    std::promise<callback_result_t> result_promise;
    std::future<callback_result_t> result_future = result_promise.get_future ();
    server.on_packet (&stream_packet_callback, &result_promise);

    detail::raw_tcp_client_t client (endpoint);
    assert (detail::wait_stream_connected (server_monitor));

    const std::string request = detail::k_stream_payload;
    const std::vector<unsigned char> request_frame = encode_packet_frame (request);
    client.send_all (reinterpret_cast<const char *> (request_frame.data ()),
                     request_frame.size ());

    const callback_result_t result = detail::wait_future (result_future, 2000);
    assert (result.payload == detail::k_stream_payload);

    zlink::message_t reply = detail::make_message (detail::k_stream_payload);
    server.send (result.routing_id, reply);

    char response[64];
    const int received = client.recv_exact (response, request.size ());
    assert (received == static_cast<int> (std::strlen (detail::k_stream_payload)));
    assert (std::memcmp (response, detail::k_stream_payload, received) == 0);
    std::printf ("[stream/packet-callback] send: \"%s\" → recv: \"%.*s\"\n",
                 request.c_str (), received, response);

    client.close ();
    return 0;
}
