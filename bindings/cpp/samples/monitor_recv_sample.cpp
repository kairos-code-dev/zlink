/* SPDX-License-Identifier: MPL-2.0 */

#include "sample_common.hpp"

int main ()
{
    zlink::context_t ctx;
    zlink::pair_socket_t server (ctx);
    zlink::pair_socket_t client (ctx);
    zlink::monitor_handle_t server_monitor =
      server.monitor_handle (zlink::monitor_event::connection_ready);
    zlink::monitor_handle_t client_monitor =
      client.monitor_handle (zlink::monitor_event::connection_ready);

    assert (!server_monitor.recv (zlink::non_blocking_t {}));
    assert (!client_monitor.recv (zlink::non_blocking_t {}));

    assert (server.bind ("tcp://127.0.0.1:0") == 0);
    const std::string endpoint = server.options ().last_endpoint ();
    assert (!endpoint.empty ());
    assert (client.connect (endpoint) == 0);

    const zlink::monitor_event_t server_event = server_monitor.recv ();
    const zlink::monitor_event_t client_event = client_monitor.recv ();
    assert (server_event.event == zlink::monitor_event::connection_ready);
    assert (client_event.event == zlink::monitor_event::connection_ready);
    assert (!server_monitor.recv (zlink::non_blocking_t {}));
    assert (!client_monitor.recv (zlink::non_blocking_t {}));

    std::printf (
      "[monitor/recv] recv: \"connection-ready\" -> recv(non_blocking): empty\n");
    return 0;
}
