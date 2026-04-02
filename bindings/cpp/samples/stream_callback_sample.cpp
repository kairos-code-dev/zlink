/* SPDX-License-Identifier: MPL-2.0 */

#include "sample_common.hpp"

namespace {

struct callback_result_t
{
    zlink::routing_id_t routing_id;
    std::string payload;
};

void stream_callback (const zlink_routing_id_t *source_rid_,
                      zlink_msg_t *parts_,
                      size_t part_count_,
                      void *userdata_)
{
    std::promise<callback_result_t> *result_promise =
      static_cast<std::promise<callback_result_t> *> (userdata_);
    assert (result_promise != NULL);
    assert (source_rid_ != NULL);
    assert (part_count_ == 1);

    callback_result_t result;
    result.routing_id = zlink::routing_id_t (source_rid_->data, source_rid_->size);
    result.payload.assign (
      static_cast<const char *> (zlink_msg_data (&parts_[0])),
      zlink_msg_size (&parts_[0]));

    zlink_multipart_close (parts_, part_count_);
    result_promise->set_value (result);
}

} // namespace

int main ()
{
    zlink::context_t ctx;
    zlink::stream_socket_t server (ctx);
    zlink::monitor_handle_t server_monitor = server.monitor_handle ();
    assert (server.set_option (zlink::stream_options::notify, 0) == 0);

    assert (server.bind ("tcp://127.0.0.1:0") == 0);
    std::string endpoint;
    assert (server.get_option (zlink::socket_options::last_endpoint, endpoint)
            == 0);
    assert (!endpoint.empty ());

    std::promise<callback_result_t> result_promise;
    std::future<callback_result_t> result_future = result_promise.get_future ();
    assert (server.on_receive (&stream_callback, &result_promise) == 0);

    detail::raw_tcp_client_t client (endpoint);
    assert (detail::wait_stream_connected (server_monitor));

    const char *request = detail::k_stream_payload;
    const size_t request_size = std::strlen (request);
    client.send_all (request, request_size);

    const callback_result_t result = detail::wait_future (result_future, 2000);
    assert (result.payload == detail::k_stream_payload);

    zlink::message_t reply = detail::make_message (detail::k_stream_payload);
    server.send (result.routing_id, reply);

    char response[64];
    const int received = client.recv_exact (response, request_size);
    assert (received == static_cast<int> (std::strlen (detail::k_stream_payload)));
    assert (std::memcmp (response, detail::k_stream_payload, received) == 0);
    std::printf ("[stream/callback] send: \"%s\" → recv: \"%.*s\"\n",
                 request, received, response);

    client.close ();
    return 0;
}
