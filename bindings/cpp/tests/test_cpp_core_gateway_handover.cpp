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
        std::fprintf (stderr, "[cpp-handover] %s\n", msg_ ? msg_ : "");
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

void test_gateway_handover_provider_restart ()
{
    void *ctx = zlink_ctx_new ();
    assert (ctx != NULL);
    const char *service_name = "ho-svc";
    const int timeout_ms = 3000;

    step_log ("setup registry");
    void *registry = NULL;
    setup_registry (ctx, &registry, "inproc://reg-pub-ho1", "inproc://reg-router-ho1");
    sleep_ms (100);

    void *discovery = zlink_discovery_new_typed (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    assert (discovery != NULL);
    assert (zlink_discovery_connect_registry (discovery, "inproc://reg-pub-ho1")
            == 0);
    assert (zlink_discovery_subscribe (discovery, service_name) == 0);

    step_log ("setup provider1");
    char ep1[256] = {0};
    void *provider1 = zlink_receiver_new (ctx, NULL);
    assert (provider1 != NULL);
    assert (zlink_receiver_bind (provider1, "tcp://127.0.0.1:*") == 0);
    void *router1 = zlink_receiver_router_socket_unsafe (provider1);
    assert (router1 != NULL);
    int probe = 1;
    assert (zlink_setsockopt (router1, ZLINK_PROBE_ROUTER, &probe, sizeof (probe))
            == 0);
    const char prov_rid[] = "PROV-HO";
    assert (zlink_setsockopt (router1, ZLINK_ROUTING_ID, prov_rid,
                              sizeof (prov_rid) - 1)
            == 0);
    size_t len1 = sizeof (ep1);
    assert (zlink_getsockopt (router1, ZLINK_LAST_ENDPOINT, ep1, &len1) == 0);
    if (std::getenv ("ZLINK_TEST_DEBUG")) {
        std::fprintf (stderr, "[cpp-handover] ep1=%s\n", ep1);
        std::fflush (stderr);
    }
    assert (zlink_receiver_connect_registry (provider1, "inproc://reg-router-ho1")
            == 0);
    assert (zlink_receiver_register (provider1, service_name, ep1, 1) == 0);
    int rcvtimeo = timeout_ms;
    assert (zlink_setsockopt (router1, ZLINK_RCVTIMEO, &rcvtimeo, sizeof (rcvtimeo))
            == 0);
    sleep_ms (200);

    step_log ("create gateway");
    void *gateway = zlink_gateway_new (ctx, discovery, NULL);
    assert (gateway != NULL);
    wait_gateway_ready (gateway, service_name, timeout_ms);
    if (std::getenv ("ZLINK_TEST_DEBUG")) {
        std::fprintf (stderr, "[cpp-handover] gw_conn=%d\n",
                      zlink_gateway_connection_count (gateway, service_name));
        std::fflush (stderr);
    }
    sleep_ms (200);

    step_log ("send msg1 to provider1");
    zlink_msg_t parts1[1];
    assert (zlink_msg_init_size (&parts1[0], 4) == 0);
    std::memcpy (zlink_msg_data (&parts1[0]), "msg1", 4);
    send_gateway_with_timeout (gateway, service_name, parts1, 1, timeout_ms);
    zlink_peer_info_t peers1[8];
    std::memset (peers1, 0, sizeof (peers1));
    size_t peers1_count = 8;
    assert (zlink_receiver_router_peers (provider1, peers1, &peers1_count) == 0);
    assert (peers1_count > 0);

    step_log ("unregister + destroy provider1");
    assert (zlink_receiver_unregister (provider1, service_name) == 0);
    assert (zlink_receiver_destroy (&provider1) == 0);
    sleep_ms (300);

    step_log ("setup provider2 (same rid)");
    char ep2[256] = {0};
    void *provider2 = zlink_receiver_new (ctx, NULL);
    assert (provider2 != NULL);
    assert (zlink_receiver_bind (provider2, "tcp://127.0.0.1:*") == 0);
    void *router2 = zlink_receiver_router_socket_unsafe (provider2);
    assert (router2 != NULL);
    assert (zlink_setsockopt (router2, ZLINK_PROBE_ROUTER, &probe, sizeof (probe))
            == 0);
    assert (zlink_setsockopt (router2, ZLINK_ROUTING_ID, prov_rid,
                              sizeof (prov_rid) - 1)
            == 0);
    size_t len2 = sizeof (ep2);
    assert (zlink_getsockopt (router2, ZLINK_LAST_ENDPOINT, ep2, &len2) == 0);
    if (std::getenv ("ZLINK_TEST_DEBUG")) {
        std::fprintf (stderr, "[cpp-handover] ep2=%s\n", ep2);
        std::fflush (stderr);
    }
    assert (zlink_receiver_connect_registry (provider2, "inproc://reg-router-ho1")
            == 0);
    assert (zlink_receiver_register (provider2, service_name, ep2, 1) == 0);
    assert (zlink_setsockopt (router2, ZLINK_RCVTIMEO, &rcvtimeo, sizeof (rcvtimeo))
            == 0);
    sleep_ms (300);
    wait_gateway_ready (gateway, service_name, timeout_ms);
    sleep_ms (200);

    step_log ("send msg2 to provider2");
    zlink_msg_t parts2[1];
    assert (zlink_msg_init_size (&parts2[0], 4) == 0);
    std::memcpy (zlink_msg_data (&parts2[0]), "msg2", 4);
    send_gateway_with_timeout (gateway, service_name, parts2, 1, timeout_ms);
    zlink_peer_info_t peers2[8];
    std::memset (peers2, 0, sizeof (peers2));
    size_t peers2_count = 8;
    assert (zlink_receiver_router_peers (provider2, peers2, &peers2_count) == 0);
    assert (peers2_count > 0);

    assert (zlink_gateway_destroy (&gateway) == 0);
    assert (zlink_receiver_destroy (&provider2) == 0);
    assert (zlink_discovery_destroy (&discovery) == 0);
    assert (zlink_registry_destroy (&registry) == 0);
    assert (zlink_ctx_term (ctx) == 0);
}

void test_provider_handover_gateway_reconnect ()
{
    void *ctx = zlink_ctx_new ();
    assert (ctx != NULL);
    const char *service_name = "ho-svc2";
    const int timeout_ms = 3000;
    const char gw_rid[] = "GW-HO";

    step_log ("setup registry");
    void *registry = NULL;
    setup_registry (ctx, &registry, "inproc://reg-pub-ho2", "inproc://reg-router-ho2");
    sleep_ms (100);

    void *discovery1 = zlink_discovery_new_typed (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    assert (discovery1 != NULL);
    assert (zlink_discovery_connect_registry (discovery1, "inproc://reg-pub-ho2")
            == 0);
    assert (zlink_discovery_subscribe (discovery1, service_name) == 0);

    step_log ("setup provider");
    char ep[256] = {0};
    void *provider = zlink_receiver_new (ctx, NULL);
    assert (provider != NULL);
    assert (zlink_receiver_bind (provider, "tcp://127.0.0.1:*") == 0);
    void *provider_router = zlink_receiver_router_socket_unsafe (provider);
    assert (provider_router != NULL);
    int probe = 1;
    assert (zlink_setsockopt (provider_router, ZLINK_PROBE_ROUTER, &probe,
                              sizeof (probe))
            == 0);
    const char prov_rid[] = "PROV-HO2";
    assert (zlink_setsockopt (provider_router, ZLINK_ROUTING_ID, prov_rid,
                              sizeof (prov_rid) - 1)
            == 0);
    size_t ep_len = sizeof (ep);
    assert (zlink_getsockopt (provider_router, ZLINK_LAST_ENDPOINT, ep, &ep_len)
            == 0);
    assert (zlink_receiver_connect_registry (provider, "inproc://reg-router-ho2")
            == 0);
    assert (zlink_receiver_register (provider, service_name, ep, 1) == 0);
    int rcvtimeo = timeout_ms;
    assert (zlink_setsockopt (provider_router, ZLINK_RCVTIMEO, &rcvtimeo,
                              sizeof (rcvtimeo))
            == 0);
    sleep_ms (200);

    step_log ("create gateway1");
    void *gateway1 = zlink_gateway_new (ctx, discovery1, gw_rid);
    assert (gateway1 != NULL);
    wait_gateway_ready (gateway1, service_name, timeout_ms);
    if (std::getenv ("ZLINK_TEST_DEBUG")) {
        std::fprintf (stderr, "[cpp-handover] gw1_conn=%d\n",
                      zlink_gateway_connection_count (gateway1, service_name));
        std::fflush (stderr);
    }
    sleep_ms (200);

    step_log ("send msg1 via gateway1");
    zlink_msg_t parts1[1];
    assert (zlink_msg_init_size (&parts1[0], 4) == 0);
    std::memcpy (zlink_msg_data (&parts1[0]), "gw-1", 4);
    send_gateway_with_timeout (gateway1, service_name, parts1, 1, timeout_ms);

    step_log ("destroy gateway1 + discovery1");
    assert (zlink_gateway_destroy (&gateway1) == 0);
    assert (zlink_discovery_destroy (&discovery1) == 0);
    sleep_ms (300);

    step_log ("create discovery2 + gateway2");
    void *discovery2 = zlink_discovery_new_typed (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    assert (discovery2 != NULL);
    assert (zlink_discovery_connect_registry (discovery2, "inproc://reg-pub-ho2")
            == 0);
    assert (zlink_discovery_subscribe (discovery2, service_name) == 0);
    sleep_ms (200);

    void *gateway2 = zlink_gateway_new (ctx, discovery2, gw_rid);
    assert (gateway2 != NULL);
    wait_gateway_ready (gateway2, service_name, timeout_ms);
    if (std::getenv ("ZLINK_TEST_DEBUG")) {
        std::fprintf (stderr, "[cpp-handover] gw2_conn=%d\n",
                      zlink_gateway_connection_count (gateway2, service_name));
        std::fflush (stderr);
    }
    sleep_ms (200);

    step_log ("send msg2 via gateway2");
    zlink_msg_t parts2[1];
    assert (zlink_msg_init_size (&parts2[0], 4) == 0);
    std::memcpy (zlink_msg_data (&parts2[0]), "gw-2", 4);
    send_gateway_with_timeout (gateway2, service_name, parts2, 1, timeout_ms);

    assert (zlink_gateway_destroy (&gateway2) == 0);
    assert (zlink_receiver_destroy (&provider) == 0);
    assert (zlink_discovery_destroy (&discovery2) == 0);
    assert (zlink_registry_destroy (&registry) == 0);
    assert (zlink_ctx_term (ctx) == 0);
}
} // namespace

int main ()
{
    test_gateway_handover_provider_restart ();
    test_provider_handover_gateway_reconnect ();
    return 0;
}
