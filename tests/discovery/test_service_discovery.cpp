/* SPDX-License-Identifier: MPL-2.0 */

#include "../testutil_unity.hpp"
#include "../testutil.hpp"

#include <errno.h>
#include <string.h>
#include <stdlib.h>

SETUP_TEARDOWN_TESTCONTEXT

// Debug logging enabled by ZLINK_TEST_DEBUG environment variable
static bool test_debug_enabled ()
{
    return getenv ("ZLINK_TEST_DEBUG") != NULL;
}

static void step_log (const char *msg_)
{
    if (test_debug_enabled ()) {
        fprintf (stderr, "[test] %s\n", msg_ ? msg_ : "");
        fflush (stderr);
    }
}

// Setup a registry with the given endpoints
static void setup_registry (void *ctx_,
                            void **registry_out_,
                            const char *pub_ep_,
                            const char *router_ep_)
{
    void *registry = zlink_registry_new (ctx_);
    TEST_ASSERT_NOT_NULL (registry);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_registry_set_endpoints (registry, pub_ep_, router_ep_));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_start (registry));
    *registry_out_ = registry;
}

// Wait for provider to appear in discovery
static bool wait_for_provider (void *discovery_,
                               const char *service_name_,
                               int timeout_ms_)
{
    const int sleep_ms = 25;
    const int max_attempts = timeout_ms_ / sleep_ms;

    for (int i = 0; i < max_attempts; ++i) {
        const int count = zlink_discovery_provider_count (discovery_, service_name_);
        if (count > 0)
            return true;
        msleep (sleep_ms);
    }
    return false;
}

// Wait for provider to disappear from discovery
static bool wait_for_provider_removal (void *discovery_,
                                       const char *service_name_,
                                       int timeout_ms_)
{
    const int sleep_ms = 25;
    const int max_attempts = timeout_ms_ / sleep_ms;

    for (int i = 0; i < max_attempts; ++i) {
        const int count = zlink_discovery_provider_count (discovery_, service_name_);
        if (count == 0)
            return true;
        msleep (sleep_ms);
    }
    return false;
}

// Test: Basic registration and discovery
static void test_discovery_provider_registration ()
{
    step_log ("=== test_discovery_provider_registration ===");

    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);

    // Setup registry with inproc endpoints
    step_log ("setup registry");
    void *registry = NULL;
    setup_registry (ctx, &registry, "inproc://reg-pub-basic",
                    "inproc://reg-router-basic");
    msleep (50);

    // Create discovery and connect to registry
    step_log ("setup discovery");
    void *discovery = zlink_discovery_new (ctx);
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_connect_registry (discovery, "inproc://reg-pub-basic"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_subscribe (discovery, "test-svc"));
    msleep (50);

    // Create provider and register
    step_log ("create provider");
    void *provider = zlink_provider_new (ctx);
    TEST_ASSERT_NOT_NULL (provider);

    char bind_ep[64];
    snprintf (bind_ep, sizeof (bind_ep), "tcp://127.0.0.1:%d",
              test_port (5700));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_provider_bind (provider, bind_ep));

    char advertise_ep[256] = {0};
    size_t advertise_len = sizeof (advertise_ep);
    void *router = zlink_provider_router (provider);
    TEST_ASSERT_NOT_NULL (router);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_getsockopt (router, ZLINK_LAST_ENDPOINT, advertise_ep,
                        &advertise_len));

    step_log ("connect to registry and register");
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_provider_connect_registry (provider, "inproc://reg-router-basic"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_provider_register (provider, "test-svc", advertise_ep, 1));

    // Wait for provider to appear in discovery
    step_log ("wait for provider");
    TEST_ASSERT_TRUE (wait_for_provider (discovery, "test-svc", 2000));

    // Verify provider count
    const int count = zlink_discovery_provider_count (discovery, "test-svc");
    TEST_ASSERT_EQUAL_INT (1, count);

    // Get provider info
    step_log ("get providers");
    zlink_provider_info_t providers[4];
    size_t provider_count = 4;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_get_providers (discovery, "test-svc", providers,
                                      &provider_count));
    TEST_ASSERT_EQUAL_INT (1, (int) provider_count);

    // Verify provider information
    TEST_ASSERT_EQUAL_STRING ("test-svc", providers[0].service_name);
    TEST_ASSERT_EQUAL_STRING (advertise_ep, providers[0].endpoint);
    TEST_ASSERT_EQUAL_UINT32 (1, providers[0].weight);
    TEST_ASSERT_GREATER_THAN_UINT (0, providers[0].routing_id.size);

    if (test_debug_enabled ()) {
        fprintf (stderr, "[provider] endpoint=%s\n", providers[0].endpoint);
        fprintf (stderr, "[provider] weight=%u\n",
                 static_cast<unsigned> (providers[0].weight));
        fprintf (stderr, "[provider] rid_size=%u rid=0x",
                 static_cast<unsigned> (providers[0].routing_id.size));
        for (uint8_t j = 0; j < providers[0].routing_id.size; ++j)
            fprintf (stderr, "%02x", providers[0].routing_id.data[j]);
        fprintf (stderr, "\n");
    }

    // Cleanup
    step_log ("cleanup");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_provider_destroy (&provider));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));

    step_log ("=== test_discovery_provider_registration done ===");
}

// Test: Service-based filtering
static void test_discovery_service_filtering ()
{
    step_log ("=== test_discovery_service_filtering ===");

    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);

    // Setup registry
    step_log ("setup registry");
    void *registry = NULL;
    setup_registry (ctx, &registry, "inproc://reg-pub-filter",
                    "inproc://reg-router-filter");
    msleep (50);

    // Create discovery
    step_log ("setup discovery");
    void *discovery = zlink_discovery_new (ctx);
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_connect_registry (discovery, "inproc://reg-pub-filter"));

    // Subscribe to svc-A
    step_log ("subscribe to svc-A");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_subscribe (discovery, "svc-A"));
    msleep (50);

    // Create provider-A
    step_log ("create provider-A");
    void *provider_a = zlink_provider_new (ctx);
    TEST_ASSERT_NOT_NULL (provider_a);

    char bind_ep_a[64];
    snprintf (bind_ep_a, sizeof (bind_ep_a), "tcp://127.0.0.1:%d",
              test_port (5701));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_provider_bind (provider_a, bind_ep_a));

    char advertise_a[256] = {0};
    size_t advertise_len = sizeof (advertise_a);
    void *router_a = zlink_provider_router (provider_a);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_getsockopt (router_a, ZLINK_LAST_ENDPOINT, advertise_a,
                        &advertise_len));

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_provider_connect_registry (provider_a,
                                        "inproc://reg-router-filter"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_provider_register (provider_a, "svc-A", advertise_a, 10));

    // Create provider-B
    step_log ("create provider-B");
    void *provider_b = zlink_provider_new (ctx);
    TEST_ASSERT_NOT_NULL (provider_b);

    char bind_ep_b[64];
    snprintf (bind_ep_b, sizeof (bind_ep_b), "tcp://127.0.0.1:%d",
              test_port (5702));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_provider_bind (provider_b, bind_ep_b));

    char advertise_b[256] = {0};
    advertise_len = sizeof (advertise_b);
    void *router_b = zlink_provider_router (provider_b);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_getsockopt (router_b, ZLINK_LAST_ENDPOINT, advertise_b,
                        &advertise_len));

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_provider_connect_registry (provider_b,
                                        "inproc://reg-router-filter"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_provider_register (provider_b, "svc-B", advertise_b, 20));

    // Wait for provider-A to appear
    step_log ("wait for provider-A");
    TEST_ASSERT_TRUE (wait_for_provider (discovery, "svc-A", 2000));

    // Verify only provider-A is returned
    step_log ("verify svc-A providers");
    zlink_provider_info_t providers[4];
    size_t count = 4;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_get_providers (discovery, "svc-A", providers, &count));
    TEST_ASSERT_EQUAL_INT (1, (int) count);
    TEST_ASSERT_EQUAL_STRING ("svc-A", providers[0].service_name);
    TEST_ASSERT_EQUAL_STRING (advertise_a, providers[0].endpoint);
    TEST_ASSERT_EQUAL_UINT32 (10, providers[0].weight);

    // Verify svc-B is not discovered yet (not subscribed)
    int count_b = zlink_discovery_provider_count (discovery, "svc-B");
    TEST_ASSERT_EQUAL_INT (0, count_b);

    // Subscribe to svc-B
    step_log ("subscribe to svc-B");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_subscribe (discovery, "svc-B"));

    // Wait for provider-B to appear
    step_log ("wait for provider-B");
    TEST_ASSERT_TRUE (wait_for_provider (discovery, "svc-B", 2000));

    // Verify provider-B is now returned
    step_log ("verify svc-B providers");
    count = 4;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_get_providers (discovery, "svc-B", providers, &count));
    TEST_ASSERT_EQUAL_INT (1, (int) count);
    TEST_ASSERT_EQUAL_STRING ("svc-B", providers[0].service_name);
    TEST_ASSERT_EQUAL_STRING (advertise_b, providers[0].endpoint);
    TEST_ASSERT_EQUAL_UINT32 (20, providers[0].weight);

    // Verify each service still returns correct count
    step_log ("verify provider counts");
    TEST_ASSERT_EQUAL_INT (1,
                           zlink_discovery_provider_count (discovery, "svc-A"));
    TEST_ASSERT_EQUAL_INT (1,
                           zlink_discovery_provider_count (discovery, "svc-B"));

    if (test_debug_enabled ()) {
        fprintf (stderr, "[svc-A] endpoint=%s weight=%u\n", advertise_a, 10);
        fprintf (stderr, "[svc-B] endpoint=%s weight=%u\n", advertise_b, 20);
    }

    // Cleanup
    step_log ("cleanup");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_provider_destroy (&provider_a));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_provider_destroy (&provider_b));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));

    step_log ("=== test_discovery_service_filtering done ===");
}

// Test: Heartbeat expiration
static void test_discovery_heartbeat_timeout ()
{
    step_log ("=== test_discovery_heartbeat_timeout ===");

    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);

    // Setup registry with short heartbeat timeout
    step_log ("setup registry with heartbeat");
    void *registry = zlink_registry_new (ctx);
    TEST_ASSERT_NOT_NULL (registry);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_registry_set_endpoints (registry, "inproc://reg-pub-hb",
                                     "inproc://reg-router-hb"));

    // Set heartbeat: 100ms interval, 500ms timeout
    const uint32_t heartbeat_interval = 100;
    const uint32_t heartbeat_timeout = 500;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_set_heartbeat (
      registry, heartbeat_interval, heartbeat_timeout));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_start (registry));
    msleep (50);

    // Create discovery
    step_log ("setup discovery");
    void *discovery = zlink_discovery_new (ctx);
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_connect_registry (discovery, "inproc://reg-pub-hb"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_subscribe (discovery, "hb-svc"));
    msleep (50);

    // Create provider
    step_log ("create provider");
    void *provider = zlink_provider_new (ctx);
    TEST_ASSERT_NOT_NULL (provider);

    char bind_ep[64];
    snprintf (bind_ep, sizeof (bind_ep), "tcp://127.0.0.1:%d",
              test_port (5703));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_provider_bind (provider, bind_ep));

    char advertise_ep[256] = {0};
    size_t advertise_len = sizeof (advertise_ep);
    void *router = zlink_provider_router (provider);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_getsockopt (router, ZLINK_LAST_ENDPOINT, advertise_ep,
                        &advertise_len));

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_provider_connect_registry (provider, "inproc://reg-router-hb"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_provider_register (provider, "hb-svc", advertise_ep, 1));

    // Wait for provider to appear
    step_log ("wait for provider");
    TEST_ASSERT_TRUE (wait_for_provider (discovery, "hb-svc", 2000));

    // Verify provider is discovered
    int count = zlink_discovery_provider_count (discovery, "hb-svc");
    TEST_ASSERT_EQUAL_INT (1, count);

    if (test_debug_enabled ()) {
        fprintf (stderr, "[heartbeat] provider registered, endpoint=%s\n",
                 advertise_ep);
    }

    // Destroy provider (stops heartbeat)
    step_log ("destroy provider (stop heartbeat)");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_provider_destroy (&provider));

    if (test_debug_enabled ()) {
        fprintf (stderr,
                 "[heartbeat] waiting for timeout (%u ms + margin)...\n",
                 heartbeat_timeout);
    }

    // Wait for timeout + margin
    const int timeout_margin = 300;
    msleep (heartbeat_timeout + timeout_margin);

    // Verify provider is removed from discovery
    step_log ("verify provider removed");
    TEST_ASSERT_TRUE (
      wait_for_provider_removal (discovery, "hb-svc", 1000));

    count = zlink_discovery_provider_count (discovery, "hb-svc");
    TEST_ASSERT_EQUAL_INT (0, count);

    if (test_debug_enabled ()) {
        fprintf (stderr, "[heartbeat] provider removed after timeout\n");
    }

    // Cleanup
    step_log ("cleanup");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));

    step_log ("=== test_discovery_heartbeat_timeout done ===");
}

int main (void)
{
    setup_test_environment ();

    UNITY_BEGIN ();
    RUN_TEST (test_discovery_provider_registration);
    RUN_TEST (test_discovery_service_filtering);
    RUN_TEST (test_discovery_heartbeat_timeout);
    return UNITY_END ();
}
