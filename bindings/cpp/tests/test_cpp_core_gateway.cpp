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
    eps.pub = unique_inproc ("inproc://cpp-reg-pub-gw-", suffix_);
    eps.router = unique_inproc ("inproc://cpp-reg-router-gw-", suffix_);
    return eps;
}

void step_log (const char *msg_)
{
    if (std::getenv ("ZLINK_TEST_DEBUG")) {
        std::fprintf (stderr, "[cpp-gateway] %s\n", msg_ ? msg_ : "");
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

static std::string last_endpoint_from_receiver (void *receiver_)
{
    char endpoint[256];
    std::memset (endpoint, 0, sizeof (endpoint));
    size_t endpoint_len = sizeof (endpoint);
    assert (zlink_receiver_last_endpoint (receiver_, endpoint, &endpoint_len)
            == 0);

    const size_t bounded = endpoint_len <= sizeof (endpoint) ? endpoint_len
                                                              : sizeof (endpoint);
    size_t len = bounded;
    if (len > 0 && endpoint[len - 1] == '\0')
        --len;
    return std::string (endpoint, len);
}

void test_gateway_single_service_tcp ()
{
    const std::string service_name = "svc";
    const registry_endpoints_t eps = make_registry_endpoints ("single");

    zlink::context_t ctx;
    zlink::service::registry_t registry (ctx);
    assert (registry.valid ());
    assert (registry.set_endpoints (eps.pub, eps.router) == 0);
    assert (registry.start () == 0);
    sleep_ms (100);

    zlink::service::discovery_t discovery (ctx, zlink::service_type::gateway);
    assert (discovery.valid ());
    step_log ("connect discovery");
    assert (discovery.connect_registry (eps.router) == 0);
    assert (discovery.subscribe (service_name) == 0);

    step_log ("setup provider");
    zlink::service::receiver_t provider (ctx);
    assert (provider.valid ());
    assert (zlink_receiver_set_routing_id (
              provider.handle (), "PROV1", sizeof ("PROV1") - 1)
            == 0);
    assert (provider.bind ("tcp://127.0.0.1:*") == 0);
    const std::string advertise_ep =
      last_endpoint_from_receiver (provider.handle ());
    assert (!advertise_ep.empty ());

    assert (provider.connect_registry (eps.router) == 0);
    assert (provider.register_service (service_name, advertise_ep, 1) == 0);
    assert (wait_for_provider (discovery, service_name, 2000));

    zlink_receiver_info_t provider_info;
    std::memset (&provider_info, 0, sizeof (provider_info));
    size_t count = 1;
    assert (discovery.get_receivers (service_name, &provider_info, &count) == 0);
    assert (count == 1);
    assert (std::strcmp (advertise_ep.c_str (), provider_info.endpoint) == 0);
    assert (provider_info.routing_id.size > 0);

    step_log ("create gateway");
    zlink::service::gateway_t gateway (ctx, discovery);
    assert (gateway.valid ());
    assert (wait_gateway_ready (gateway, service_name, 2000));

    step_log ("send payload");
    send_gateway_payload (gateway, service_name, "hello", 5);
    assert (wait_for_router_peers (provider, 2000));
}

} // namespace

int main ()
{
    test_gateway_single_service_tcp ();
    return 0;
}
