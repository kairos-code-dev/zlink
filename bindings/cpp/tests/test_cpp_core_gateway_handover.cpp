#include "test_helpers.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

struct registry_endpoints_t
{
    std::string pub;
    std::string router;
};

registry_endpoints_t make_registry_endpoints (const char *suffix_)
{
    registry_endpoints_t eps;
    eps.pub = unique_inproc ("inproc://cpp-reg-pub-ho-", suffix_);
    eps.router = unique_inproc ("inproc://cpp-reg-router-ho-", suffix_);
    return eps;
}

void step_log (const char *msg_)
{
    if (std::getenv ("ZLINK_TEST_DEBUG")) {
        std::fprintf (stderr, "[cpp-handover] %s\n", msg_ ? msg_ : "");
        std::fflush (stderr);
    }
}

bool wait_gateway_ready (zlink::service::gateway_t &gateway_,
                         const std::string &service_name_,
                         int timeout_ms_)
{
    const auto deadline = std::chrono::steady_clock::now ()
                          + std::chrono::milliseconds (timeout_ms_);
    while (std::chrono::steady_clock::now () < deadline) {
        if (gateway_.connection_count (service_name_) > 0)
            return true;
        sleep_ms (5);
    }
    return false;
}

bool wait_for_provider (zlink::service::discovery_t &discovery_,
                        const std::string &service_name_,
                        int timeout_ms_)
{
    const auto deadline = std::chrono::steady_clock::now ()
                          + std::chrono::milliseconds (timeout_ms_);
    while (std::chrono::steady_clock::now () < deadline) {
        if (discovery_.receiver_count (service_name_) > 0)
            return true;
        sleep_ms (10);
    }
    return false;
}

bool wait_for_router_peers (zlink::service::receiver_t &receiver_, int timeout_ms_)
{
    const auto deadline = std::chrono::steady_clock::now ()
                          + std::chrono::milliseconds (timeout_ms_);
    while (std::chrono::steady_clock::now () < deadline) {
        zlink_peer_info_t peers[8];
        std::memset (peers, 0, sizeof (peers));
        size_t count = 8;
        if (receiver_.router_peers (peers, &count) == 0 && count > 0)
            return true;
        sleep_ms (10);
    }
    return false;
}

void send_gateway_payload (zlink::service::gateway_t &gateway_,
                           const std::string &service_name_,
                           const char *payload_,
                           size_t payload_size_)
{
    zlink::message_t msg (payload_size_);
    assert (msg.valid ());
    if (payload_size_ > 0)
        std::memcpy (msg.data (), payload_, payload_size_);

    std::vector<zlink::message_t> parts;
    parts.push_back (std::move (msg));
    assert (gateway_.send (service_name_, parts) == 0);
}

std::string configure_receiver (zlink::service::receiver_t &receiver_,
                                const std::string &routing_id_,
                                int rcv_timeout_ms_)
{
    assert (receiver_.set_sockopt (zlink::receiver_socket_role::router,
                                   zlink::socket_options::probe_router, 1)
            == 0);
    assert (receiver_.set_sockopt (zlink::receiver_socket_role::router,
                                   zlink::socket_options::routing_id, routing_id_)
            == 0);
    assert (receiver_.set_sockopt (zlink::receiver_socket_role::router,
                                   zlink::socket_options::rcvtimeo,
                                   rcv_timeout_ms_)
            == 0);

    zlink::socket_t provider_router =
      zlink::socket_t::wrap (receiver_.router_handle ());
    const std::string endpoint = bound_endpoint (provider_router);
    assert (!endpoint.empty ());
    return endpoint;
}

void test_gateway_handover_provider_restart ()
{
    const std::string service_name = "ho-svc";
    const int timeout_ms = 3000;
    const std::string provider_rid = "PROV-HO";
    const registry_endpoints_t eps = make_registry_endpoints ("provider-restart");

    zlink::context_t ctx;
    zlink::service::registry_t registry (ctx);
    assert (registry.valid ());
    assert (registry.set_endpoints (eps.pub, eps.router) == 0);
    assert (registry.start () == 0);
    sleep_ms (100);

    zlink::service::discovery_t discovery (ctx, zlink::service_type::gateway);
    assert (discovery.valid ());
    assert (discovery.connect_registry (eps.pub) == 0);
    assert (discovery.subscribe (service_name) == 0);

    zlink::service::gateway_t gateway (ctx, discovery);
    assert (gateway.valid ());

    {
        step_log ("setup provider1");
        zlink::service::receiver_t provider1 (ctx);
        assert (provider1.valid ());
        assert (provider1.bind ("tcp://127.0.0.1:*") == 0);
        const std::string advertise_ep =
          configure_receiver (provider1, provider_rid, timeout_ms);
        assert (provider1.connect_registry (eps.router) == 0);
        assert (provider1.register_service (service_name, advertise_ep, 1) == 0);

        assert (wait_for_provider (discovery, service_name, timeout_ms));
        assert (wait_gateway_ready (gateway, service_name, timeout_ms));

        step_log ("send msg1 to provider1");
        send_gateway_payload (gateway, service_name, "msg1", 4);
        assert (wait_for_router_peers (provider1, timeout_ms));

        step_log ("unregister + destroy provider1");
        assert (provider1.unregister_service (service_name) == 0);
        assert (provider1.destroy () == 0);
    }

    sleep_ms (300);

    {
        step_log ("setup provider2 (same rid)");
        zlink::service::receiver_t provider2 (ctx);
        assert (provider2.valid ());
        assert (provider2.bind ("tcp://127.0.0.1:*") == 0);
        const std::string advertise_ep =
          configure_receiver (provider2, provider_rid, timeout_ms);
        assert (provider2.connect_registry (eps.router) == 0);
        assert (provider2.register_service (service_name, advertise_ep, 1) == 0);

        assert (wait_gateway_ready (gateway, service_name, timeout_ms));
        step_log ("send msg2 to provider2");
        send_gateway_payload (gateway, service_name, "msg2", 4);
        assert (wait_for_router_peers (provider2, timeout_ms));
    }
}

void test_provider_handover_gateway_reconnect ()
{
    const std::string service_name = "ho-svc2";
    const int timeout_ms = 3000;
    const std::string provider_rid = "PROV-HO2";
    const std::string gateway_rid = "GW-HO";
    const registry_endpoints_t eps = make_registry_endpoints ("gateway-reconnect");

    zlink::context_t ctx;
    zlink::service::registry_t registry (ctx);
    assert (registry.valid ());
    assert (registry.set_endpoints (eps.pub, eps.router) == 0);
    assert (registry.start () == 0);
    sleep_ms (100);

    step_log ("setup provider");
    zlink::service::receiver_t provider (ctx);
    assert (provider.valid ());
    assert (provider.bind ("tcp://127.0.0.1:*") == 0);
    const std::string advertise_ep =
      configure_receiver (provider, provider_rid, timeout_ms);
    assert (provider.connect_registry (eps.router) == 0);
    assert (provider.register_service (service_name, advertise_ep, 1) == 0);

    {
        step_log ("create discovery1 + gateway1");
        zlink::service::discovery_t discovery1 (ctx, zlink::service_type::gateway);
        assert (discovery1.valid ());
        assert (discovery1.connect_registry (eps.pub) == 0);
        assert (discovery1.subscribe (service_name) == 0);

        zlink::service::gateway_t gateway1 (ctx, discovery1, gateway_rid);
        assert (gateway1.valid ());
        assert (wait_gateway_ready (gateway1, service_name, timeout_ms));

        step_log ("send msg1 via gateway1");
        send_gateway_payload (gateway1, service_name, "gw-1", 4);
    }

    sleep_ms (300);

    {
        step_log ("create discovery2 + gateway2");
        zlink::service::discovery_t discovery2 (ctx, zlink::service_type::gateway);
        assert (discovery2.valid ());
        assert (discovery2.connect_registry (eps.pub) == 0);
        assert (discovery2.subscribe (service_name) == 0);

        zlink::service::gateway_t gateway2 (ctx, discovery2, gateway_rid);
        assert (gateway2.valid ());
        assert (wait_gateway_ready (gateway2, service_name, timeout_ms));

        step_log ("send msg2 via gateway2");
        send_gateway_payload (gateway2, service_name, "gw-2", 4);
    }
}

} // namespace

int main ()
{
    test_gateway_handover_provider_restart ();
    test_provider_handover_gateway_reconnect ();
    return 0;
}
