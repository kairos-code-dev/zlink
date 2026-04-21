/* SPDX-License-Identifier: MPL-2.0 */

#include "sample_common.hpp"

int main ()
{
    zlink::context_t ctx;
    zlink::pair_socket_t server (ctx);
    zlink::pair_socket_t client (ctx);
    zlink::monitor_handle_t server_monitor = server.monitor_handle ();
    zlink::monitor_handle_t client_monitor = client.monitor_handle ();

    server.bind ("tcp://127.0.0.1:0");
    const std::string endpoint = server.options ().last_endpoint ();
    assert (!endpoint.empty ());
    client.connect (endpoint);
    assert (detail::wait_connected (server_monitor, client_monitor));

    const std::string sent = detail::k_pair_payload;
    zlink::message_t outbound = detail::make_message (sent);
    client.send (outbound);

    std::optional<zlink::received_t> inbound = server.recv ();
    assert (inbound.has_value ());
    assert (inbound->parts ().size () == 1);
    const std::string received = inbound->parts ()[0].to_string ();
    assert (received == detail::k_pair_payload);
    inbound->close ();
    std::printf ("[pair/recv] send: \"%s\" → recv: \"%s\"\n",
                 sent.c_str (), received.c_str ());
    return 0;
}
