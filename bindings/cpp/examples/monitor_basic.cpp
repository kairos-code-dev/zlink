#include <zlink.hpp>

#include <iostream>

int main ()
{
    zlink::context_t ctx;
    zlink::socket_t server (ctx, zlink::socket_type::pair);
    zlink::socket_t client (ctx, zlink::socket_type::pair);

    zlink::socket_t monitor_sock = server.monitor_open (zlink::monitor_event::all);
    zlink::monitor_socket_t monitor (std::move (monitor_sock));

    const char *endpoint = "inproc://monitor-basic";
    if (server.bind (endpoint) != 0) {
        std::cerr << "bind failed" << std::endl;
        return 1;
    }
    if (client.connect (endpoint) != 0) {
        std::cerr << "connect failed" << std::endl;
        return 1;
    }

    zlink_monitor_event_t event;
    if (monitor.recv (event) != 0) {
        std::cerr << "monitor_recv failed" << std::endl;
        return 1;
    }

    std::cout << "event=" << event.event << " value=" << event.value
              << " local=" << event.local_addr << " remote=" << event.remote_addr
              << std::endl;

    return 0;
}
