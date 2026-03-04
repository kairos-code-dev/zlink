#include "test_helpers.hpp"

#include <cstring>
#include <vector>

static bool wait_for_event (zlink::socket_t &monitor_,
                            uint64_t expected_event_,
                            zlink_monitor_event_t *out_)
{
    for (int attempt = 0; attempt < 100; ++attempt) {
        for (;;) {
            zlink_monitor_event_t ev;
            if (zlink_monitor_recv (
                  monitor_.handle (), &ev, static_cast<int> (zlink::recv_flag::dontwait))
                != 0)
                break;
            if (ev.event != expected_event_)
                continue;
            if (out_)
                *out_ = ev;
            return true;
        }
        sleep_ms (20);
    }
    return false;
}

static void test_auto_routing_id_generation ()
{
    zlink::context_t ctx;
    const zlink::socket_type types[] = {
      zlink::socket_type::pair,   zlink::socket_type::pub,
      zlink::socket_type::sub,    zlink::socket_type::dealer,
      zlink::socket_type::router, zlink::socket_type::xpub,
      zlink::socket_type::xsub,   zlink::socket_type::stream,
    };

    for (size_t i = 0; i < sizeof (types) / sizeof (types[0]); ++i) {
        zlink::socket_t sock (ctx, types[i]);
        unsigned char rid[255];
        std::memset (rid, 0, sizeof (rid));
        size_t size = sizeof (rid);
        assert (sock.get (zlink::socket_option::routing_id, rid, &size) == 0);
        assert (size == 16);
    }
}

static void test_monitor_open_and_connection_ready ()
{
    zlink::context_t ctx;

    zlink::socket_t server (ctx, zlink::socket_type::router);
    zlink::socket_t client (ctx, zlink::socket_type::dealer);

    const std::string endpoint = endpoint_for (transport_case_t{"tcp", ""}, "monitor");
    assert (server.bind (endpoint) == 0);

    zlink::socket_t monitor =
      server.monitor_open (zlink::monitor_event::connection_ready
                           | zlink::monitor_event::disconnected);

    assert (client.connect (endpoint) == 0);

    zlink_monitor_event_t ready;
    assert (wait_for_event (
      monitor, static_cast<uint64_t> (ZLINK_EVENT_CONNECTION_READY), &ready));
    assert (ready.routing_id.size > 0);
    assert (ready.remote_addr[0] != '\0' || ready.local_addr[0] != '\0');

    assert (client.close () == 0);

    zlink_monitor_event_t disconnected;
    assert (wait_for_event (
      monitor, static_cast<uint64_t> (ZLINK_EVENT_DISCONNECTED), &disconnected));
    assert (disconnected.remote_addr[0] != '\0' || disconnected.local_addr[0] != '\0');

    assert (monitor.close () == 0);
}

int main ()
{
    test_auto_routing_id_generation ();
    test_monitor_open_and_connection_ready ();
    return 0;
}
