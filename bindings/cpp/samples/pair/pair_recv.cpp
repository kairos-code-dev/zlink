/* SPDX-License-Identifier: MPL-2.0 */

#include "../common/sample_common.hpp"

int main ()
{
    zlink::context_t ctx;
    zlink::socket_t server (ctx, zlink::socket_type::pair);
    zlink::socket_t client (ctx, zlink::socket_type::pair);
    zlink::monitor_handle_t server_monitor (server, zlink::monitor_event::all);
    zlink::monitor_handle_t client_monitor (client, zlink::monitor_event::all);

    const std::string endpoint = zlink_cpp_sample::unique_tcp ("pair-recv");
    assert (server.bind (endpoint) == 0);
    assert (client.connect (endpoint) == 0);
    assert (zlink_cpp_sample::wait_for_socket_monitor_event (
      server_monitor,
      static_cast<uint64_t> (zlink::monitor_event::connection_ready_changed),
      2000, 1));
    assert (zlink_cpp_sample::wait_for_socket_monitor_event (
      client_monitor,
      static_cast<uint64_t> (zlink::monitor_event::connection_ready_changed),
      2000, 1));

    zlink::message_t outbound = zlink_cpp_sample::make_message ("pair-recv");
    assert (client.send (outbound) == 0);

    zlink::message_t inbound;
    assert (server.recv (inbound) == 0);
    assert (inbound.to_string () == "pair-recv");
    return 0;
}
