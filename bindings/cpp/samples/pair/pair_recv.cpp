/* SPDX-License-Identifier: MPL-2.0 */

#include "../common/sample_common.hpp"

int main ()
{
    zlink::context_t ctx;
    zlink::pair_socket_t server (ctx);
    zlink::pair_socket_t client (ctx);
    zlink::monitor_handle_t server_monitor = server.monitor_handle ();
    zlink::monitor_handle_t client_monitor = client.monitor_handle ();

    const std::string endpoint = detail::unique_tcp ("pair-recv");
    assert (server.bind (endpoint) == 0);
    assert (client.connect (endpoint) == 0);
    assert (detail::wait_for_socket_monitor_event (
      server_monitor,
      static_cast<uint64_t> (zlink::monitor_event::connection_ready_changed),
      2000, 1));
    assert (detail::wait_for_socket_monitor_event (
      client_monitor,
      static_cast<uint64_t> (zlink::monitor_event::connection_ready_changed),
      2000, 1));

    zlink::message_t outbound = detail::make_message ("pair-recv");
    assert (client.send (outbound) == 0);

    zlink::message_t inbound;
    assert (server.recv (inbound) == 0);
    assert (inbound.to_string () == "pair-recv");
    return 0;
}
