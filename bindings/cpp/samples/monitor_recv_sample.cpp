/* SPDX-License-Identifier: MPL-2.0 */

#include "sample_common.hpp"

int main ()
{
    zlink::context_t ctx;
    zlink::pair_socket_t server (ctx);
    zlink::pair_socket_t client (ctx);
    zlink::monitor_handle_t server_monitor (
      server, zlink::monitor_event::connection_ready_changed);
    zlink::monitor_handle_t client_monitor (
      client, zlink::monitor_event::connection_ready_changed);

    assert (!server_monitor.try_recv ());
    assert (!client_monitor.try_recv ());

    assert (server.bind ("tcp://127.0.0.1:0") == 0);
    std::string endpoint;
    assert (server.get_option (zlink::socket_options::last_endpoint, endpoint)
            == 0);
    assert (!endpoint.empty ());
    assert (client.connect (endpoint) == 0);

    const zlink_socket_monitor_event_t server_event = server_monitor.recv ();
    const zlink_socket_monitor_event_t client_event = client_monitor.recv ();
    assert (server_event.event
            == static_cast<uint64_t> (
              zlink::monitor_event::connection_ready_changed));
    assert (client_event.event
            == static_cast<uint64_t> (
              zlink::monitor_event::connection_ready_changed));
    assert (server_event.value == 1);
    assert (client_event.value == 1);
    assert (!server_monitor.try_recv ());
    assert (!client_monitor.try_recv ());

    std::printf (
      "[monitor] event: \"CONNECTION_READY\" -> tryRecv: empty\n");
    return 0;
}
