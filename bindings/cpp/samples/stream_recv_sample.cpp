/* SPDX-License-Identifier: MPL-2.0 */

#include "sample_common.hpp"

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

    detail::raw_tcp_client_t client (endpoint);
    assert (detail::wait_stream_connected (server_monitor));

    const char *request = detail::k_stream_payload;
    const size_t request_size = std::strlen (request);
    client.send_all (request, request_size);

    const zlink::received_t inbound = server.recv ();
    assert (!inbound.routing_id.empty ());
    assert (inbound.parts.size () == 1);
    assert (inbound.parts[0].to_string () == detail::k_stream_payload);

    zlink::message_t reply = detail::make_message (detail::k_stream_payload);
    server.send (inbound.routing_id, reply);

    char response[64];
    const int received = client.recv_exact (response, request_size);
    assert (received == static_cast<int> (std::strlen (detail::k_stream_payload)));
    assert (std::memcmp (response, detail::k_stream_payload, received) == 0);
    std::printf ("[stream/recv] send: \"%s\" → recv: \"%.*s\"\n",
                 request, received, response);

    client.close ();
    return 0;
}
