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

static bool test_debug_enabled ()
{
    return std::getenv ("ZLINK_TEST_DEBUG") != NULL;
}

static void step_log (const char *msg_)
{
    if (!test_debug_enabled ())
        return;
    std::fprintf (stderr, "[cpp-discovery] %s\n", msg_ ? msg_ : "");
    std::fflush (stderr);
}

static registry_endpoints_t make_registry_endpoints (const char *suffix_)
{
    registry_endpoints_t eps;
    eps.pub = unique_inproc ("inproc://cpp-reg-pub-", suffix_);
    eps.router = unique_inproc ("inproc://cpp-reg-router-", suffix_);
    return eps;
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

static bool wait_for_provider (zlink::service::discovery_t &discovery_,
                               const char *service_name_,
                               int timeout_ms_)
{
    const auto deadline = std::chrono::steady_clock::now ()
                          + std::chrono::milliseconds (timeout_ms_);
    while (std::chrono::steady_clock::now () < deadline) {
        if (discovery_.receiver_count (service_name_) > 0)
            return true;
        sleep_ms (25);
    }
    return false;
}

static bool wait_for_provider_removal (zlink::service::discovery_t &discovery_,
                                       const char *service_name_,
                                       int timeout_ms_)
{
    const auto deadline = std::chrono::steady_clock::now ()
                          + std::chrono::milliseconds (timeout_ms_);
    while (std::chrono::steady_clock::now () < deadline) {
        if (discovery_.receiver_count (service_name_) == 0)
            return true;
        sleep_ms (25);
    }
    return false;
}

static void test_discovery_provider_registration ()
{
    step_log ("test_discovery_provider_registration");

    zlink::context_t ctx;
    assert (zlink_ctx_set (ctx.handle (), ZLINK_BLOCKY, 0) == 0);
    const registry_endpoints_t eps = make_registry_endpoints ("basic");

    zlink::service::registry_t registry (ctx);
    assert (registry.handle () != NULL);
    assert (registry.set_endpoints (eps.pub.c_str (), eps.router.c_str ()) == 0);
    assert (registry.start () == 0);
    sleep_ms (50);

    zlink::service::discovery_t discovery (ctx, zlink::service_type::spot);
    assert (discovery.handle () != NULL);
    assert (discovery.connect_registry (eps.router.c_str ()) == 0);
    sleep_ms (50);

    zlink::service::receiver_t provider (ctx);
    assert (provider.handle () != NULL);
    assert (provider.bind ("tcp://127.0.0.1:*") == 0);

    const std::string advertise_ep =
      last_endpoint_from_receiver (provider.handle ());
    assert (!advertise_ep.empty ());

    assert (provider.connect_registry (eps.router.c_str ()) == 0);
    assert (provider.register_service ("test-svc", advertise_ep.c_str (), 1)
            == 0);

    assert (wait_for_provider (discovery, "test-svc", 2000));
    assert (discovery.receiver_count ("test-svc") == 1);

    zlink_receiver_info_t providers[4];
    std::memset (providers, 0, sizeof (providers));
    size_t provider_count = 4;
    assert (discovery.get_receivers ("test-svc", providers, &provider_count) == 0);
    assert (provider_count == 1);
    assert (std::strcmp (providers[0].service_name, "test-svc") == 0);
    assert (std::strcmp (providers[0].endpoint, advertise_ep.c_str ()) == 0);
    assert (providers[0].weight == 1);
    assert (providers[0].routing_id.size > 0);
}

static void test_discovery_service_filtering ()
{
    step_log ("test_discovery_service_filtering");

    zlink::context_t ctx;
    assert (zlink_ctx_set (ctx.handle (), ZLINK_BLOCKY, 0) == 0);
    const registry_endpoints_t eps = make_registry_endpoints ("filter");

    zlink::service::registry_t registry (ctx);
    assert (registry.handle () != NULL);
    assert (registry.set_endpoints (eps.pub.c_str (), eps.router.c_str ()) == 0);
    assert (registry.start () == 0);
    sleep_ms (50);

    zlink::service::discovery_t discovery (ctx, zlink::service_type::spot);
    assert (discovery.handle () != NULL);
    assert (discovery.connect_registry (eps.router.c_str ()) == 0);
    sleep_ms (50);

    zlink::service::receiver_t provider_a (ctx);
    assert (provider_a.handle () != NULL);
    assert (provider_a.bind ("tcp://127.0.0.1:*") == 0);
    const std::string advertise_a =
      last_endpoint_from_receiver (provider_a.handle ());
    assert (provider_a.connect_registry (eps.router.c_str ()) == 0);
    assert (provider_a.register_service ("svc-A", advertise_a.c_str (), 10) == 0);

    zlink::service::receiver_t provider_b (ctx);
    assert (provider_b.handle () != NULL);
    assert (provider_b.bind ("tcp://127.0.0.1:*") == 0);
    const std::string advertise_b =
      last_endpoint_from_receiver (provider_b.handle ());
    assert (provider_b.connect_registry (eps.router.c_str ()) == 0);
    assert (provider_b.register_service ("svc-B", advertise_b.c_str (), 20) == 0);

    assert (wait_for_provider (discovery, "svc-A", 2000));

    zlink_receiver_info_t providers[4];
    std::memset (providers, 0, sizeof (providers));
    size_t count = 4;
    assert (discovery.get_receivers ("svc-A", providers, &count) == 0);
    assert (count == 1);
    assert (std::strcmp (providers[0].service_name, "svc-A") == 0);
    assert (std::strcmp (providers[0].endpoint, advertise_a.c_str ()) == 0);
    assert (providers[0].weight == 10);

    assert (wait_for_provider (discovery, "svc-B", 2000));

    std::memset (providers, 0, sizeof (providers));
    count = 4;
    assert (discovery.get_receivers ("svc-B", providers, &count) == 0);
    assert (count == 1);
    assert (std::strcmp (providers[0].service_name, "svc-B") == 0);
    assert (std::strcmp (providers[0].endpoint, advertise_b.c_str ()) == 0);
    assert (providers[0].weight == 20);

    assert (discovery.receiver_count ("svc-A") == 1);
    assert (discovery.receiver_count ("svc-B") == 1);
}

static void test_discovery_heartbeat_timeout ()
{
    step_log ("test_discovery_heartbeat_timeout");

    zlink::context_t ctx;
    assert (zlink_ctx_set (ctx.handle (), ZLINK_BLOCKY, 0) == 0);
    const registry_endpoints_t eps = make_registry_endpoints ("heartbeat");
    const uint32_t heartbeat_interval = 100;
    const uint32_t heartbeat_timeout = 500;

    zlink::service::registry_t registry (ctx);
    assert (registry.handle () != NULL);
    assert (registry.set_endpoints (eps.pub.c_str (), eps.router.c_str ()) == 0);
    assert (registry.set_heartbeat (heartbeat_interval, heartbeat_timeout) == 0);
    assert (registry.start () == 0);
    sleep_ms (50);

    zlink::service::discovery_t discovery (ctx, zlink::service_type::spot);
    assert (discovery.handle () != NULL);
    assert (discovery.connect_registry (eps.router.c_str ()) == 0);
    sleep_ms (50);

    zlink::service::receiver_t provider (ctx);
    assert (provider.handle () != NULL);
    assert (provider.bind ("tcp://127.0.0.1:*") == 0);
    const std::string advertise_ep =
      last_endpoint_from_receiver (provider.handle ());
    assert (provider.connect_registry (eps.router.c_str ()) == 0);
    assert (provider.register_service ("hb-svc", advertise_ep.c_str (), 1) == 0);

    assert (wait_for_provider (discovery, "hb-svc", 2000));
    assert (discovery.receiver_count ("hb-svc") == 1);

    assert (provider.destroy () == 0);

    const int timeout_margin = 300;
    sleep_ms (static_cast<int> (heartbeat_timeout) + timeout_margin);

    assert (wait_for_provider_removal (discovery, "hb-svc", 1000));
    assert (discovery.receiver_count ("hb-svc") == 0);
}

static void test_discovery_weight_update ()
{
    step_log ("test_discovery_weight_update");

    zlink::context_t ctx;
    assert (zlink_ctx_set (ctx.handle (), ZLINK_BLOCKY, 0) == 0);
    const registry_endpoints_t eps = make_registry_endpoints ("weight");
    const uint32_t initial_weight = 10;
    const uint32_t updated_weight = 50;

    zlink::service::registry_t registry (ctx);
    assert (registry.handle () != NULL);
    assert (registry.set_endpoints (eps.pub.c_str (), eps.router.c_str ()) == 0);
    assert (registry.start () == 0);
    sleep_ms (50);

    zlink::service::discovery_t discovery (ctx, zlink::service_type::spot);
    assert (discovery.handle () != NULL);
    assert (discovery.connect_registry (eps.router.c_str ()) == 0);
    sleep_ms (50);

    zlink::service::receiver_t provider (ctx);
    assert (provider.handle () != NULL);
    assert (provider.bind ("tcp://127.0.0.1:*") == 0);
    const std::string advertise_ep =
      last_endpoint_from_receiver (provider.handle ());
    assert (provider.connect_registry (eps.router.c_str ()) == 0);
    assert (provider.register_service (
              "weight-svc", advertise_ep.c_str (), initial_weight)
            == 0);

    assert (wait_for_provider (discovery, "weight-svc", 2000));

    zlink_receiver_info_t providers[4];
    std::memset (providers, 0, sizeof (providers));
    size_t count = 4;
    assert (discovery.get_receivers ("weight-svc", providers, &count) == 0);
    assert (count == 1);
    assert (providers[0].weight == initial_weight);

    assert (provider.update_weight ("weight-svc", updated_weight) == 0);
    sleep_ms (200);

    std::memset (providers, 0, sizeof (providers));
    count = 4;
    assert (discovery.get_receivers ("weight-svc", providers, &count) == 0);
    assert (count == 1);
    assert (providers[0].weight == updated_weight);
}

} // namespace

int main ()
{
    test_discovery_provider_registration ();
    test_discovery_service_filtering ();
    test_discovery_heartbeat_timeout ();
    test_discovery_weight_update ();
    return 0;
}
