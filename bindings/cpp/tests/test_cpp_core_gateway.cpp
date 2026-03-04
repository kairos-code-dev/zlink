#include <zlink.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <thread>
#include <chrono>

namespace
{
void sleep_ms (int ms_)
{
    std::this_thread::sleep_for (std::chrono::milliseconds (ms_));
}

void step_log (const char *msg_)
{
    if (std::getenv ("ZLINK_TEST_DEBUG")) {
        std::fprintf (stderr, "[cpp-gateway] %s\n", msg_ ? msg_ : "");
        std::fflush (stderr);
    }
}

void wait_gateway_ready (void *gateway_, const char *service_name_, int timeout_ms_)
{
    const int sleep_ms_step = 5;
    const int attempts = timeout_ms_ / sleep_ms_step;
    for (int i = 0; i < attempts; ++i) {
        const int count =
          zlink_gateway_connection_count (gateway_, service_name_);
        if (count > 0)
            return;
        sleep_ms (sleep_ms_step);
    }
    assert (false && "gateway connection timeout");
}

void send_gateway_with_timeout (void *gateway_,
                                const char *service_name_,
                                zlink_msg_t *parts_,
                                size_t part_count_,
                                int timeout_ms_)
{
    const int sleep_ms_step = 2;
    const int attempts = timeout_ms_ / sleep_ms_step;
    for (int i = 0; i < attempts; ++i) {
        const int rc = zlink_gateway_send (gateway_, service_name_, parts_,
                                           part_count_, ZLINK_DONTWAIT);
        if (rc == 0)
            return;
        if (zlink_errno () != EAGAIN && zlink_errno () != EHOSTUNREACH)
            break;
        sleep_ms (sleep_ms_step);
    }
    assert (false && "gateway send timeout");
}

void setup_registry (void *ctx_,
                     void **registry_out_,
                     const char *pub_ep_,
                     const char *router_ep_)
{
    void *registry = zlink_registry_new (ctx_);
    assert (registry != NULL);
    assert (zlink_registry_set_endpoints (registry, pub_ep_, router_ep_) == 0);
    assert (zlink_registry_start (registry) == 0);
    *registry_out_ = registry;
}

void test_gateway_single_service_tcp ()
{
    void *ctx = zlink_ctx_new ();
    assert (ctx != NULL);
    const char *service_name = "svc";

    void *registry = NULL;
    step_log ("setup registry");
    setup_registry (ctx, &registry, "inproc://reg-pub-gateway1",
                    "inproc://reg-router-gateway1");
    sleep_ms (100);

    void *discovery = zlink_discovery_new_typed (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    assert (discovery != NULL);
    step_log ("connect discovery");
    assert (zlink_discovery_connect_registry (discovery, "inproc://reg-pub-gateway1")
            == 0);
    assert (zlink_discovery_subscribe (discovery, service_name) == 0);

    step_log ("bind provider router");
    const char provider_rid[] = "PROV1";
    char advertise_ep[256] = {0};
    void *provider = zlink_receiver_new (ctx, NULL);
    assert (provider != NULL);
    assert (zlink_receiver_bind (provider, "tcp://127.0.0.1:*") == 0);
    void *provider_router = zlink_receiver_router_socket_unsafe (provider);
    assert (provider_router != NULL);
    int probe = 1;
    assert (zlink_setsockopt (provider_router, ZLINK_PROBE_ROUTER, &probe,
                              sizeof (probe))
            == 0);
    assert (zlink_setsockopt (provider_router, ZLINK_ROUTING_ID, provider_rid,
                              sizeof (provider_rid) - 1)
            == 0);
    size_t advertise_len = sizeof (advertise_ep);
    assert (zlink_getsockopt (provider_router, ZLINK_LAST_ENDPOINT, advertise_ep,
                              &advertise_len)
            == 0);

    step_log ("connect provider dealer");
    assert (zlink_receiver_connect_registry (provider, "inproc://reg-router-gateway1")
            == 0);
    step_log ("register service");
    assert (zlink_receiver_register (provider, service_name, advertise_ep, 1) == 0);

    sleep_ms (200);

    zlink_receiver_info_t provider_info;
    std::memset (&provider_info, 0, sizeof (provider_info));
    size_t count = 1;
    step_log ("get providers");
    assert (zlink_discovery_get_receivers (discovery, service_name, &provider_info,
                                           &count)
            == 0);
    assert (count == 1);
    assert (std::strcmp (advertise_ep, provider_info.endpoint) == 0);
    assert (provider_info.routing_id.size > 0);

    step_log ("create gateway socket");
    void *gateway = zlink_gateway_new (ctx, discovery, NULL);
    assert (gateway != NULL);
    wait_gateway_ready (gateway, service_name, 2000);
    sleep_ms (200);

    int timeout_ms = 2000;
    assert (zlink_setsockopt (provider_router, ZLINK_RCVTIMEO, &timeout_ms,
                              sizeof (timeout_ms))
            == 0);
    sleep_ms (200);

    step_log ("send payload");
    zlink_msg_t payload;
    assert (zlink_msg_init_size (&payload, 5) == 0);
    std::memcpy (zlink_msg_data (&payload), "hello", 5);

    assert (zlink_gateway_connection_count (gateway, service_name) >= 1);
    sleep_ms (200);

    zlink_msg_t parts[1];
    parts[0] = payload;
    send_gateway_with_timeout (gateway, service_name, parts, 1, 2000);

    zlink_peer_info_t peers[8];
    std::memset (peers, 0, sizeof (peers));
    size_t peer_count = 8;
    assert (zlink_receiver_router_peers (provider, peers, &peer_count) == 0);
    assert (peer_count > 0);

    step_log ("cleanup");
    assert (zlink_gateway_destroy (&gateway) == 0);
    assert (zlink_receiver_destroy (&provider) == 0);
    assert (zlink_discovery_destroy (&discovery) == 0);
    assert (zlink_registry_destroy (&registry) == 0);
    assert (zlink_ctx_term (ctx) == 0);
}
} // namespace

int main ()
{
    test_gateway_single_service_tcp ();
    return 0;
}
