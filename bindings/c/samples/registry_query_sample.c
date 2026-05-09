/* SPDX-License-Identifier: MPL-2.0 */

#include "sample_common.h"

typedef struct
{
    void *discovery;
    void *query;
    void *provider;
    char registry_router[256];
    char service_endpoint[256];
    int found;
} registry_query_sample_t;

static void registry_query_provider_thread (void *arg_)
{
    registry_query_sample_t *sample = (registry_query_sample_t *) arg_;

    assert (zlink_socket_attach_discovery (sample->provider, sample->discovery)
            == 0);
    assert (zlink_bind (sample->provider, sample->service_endpoint)
            == ZLINK_BIND_OK);
}

static void registry_query_client_thread (void *arg_)
{
    registry_query_sample_t *sample = (registry_query_sample_t *) arg_;
    struct timespec start;

    assert (zlink_registry_query_client_connect (
              sample->query, sample->registry_router)
            == ZLINK_CONNECT_OK);
    clock_gettime (CLOCK_MONOTONIC, &start);

    while (!sample->found) {
        zlink_registry_topology_entry_t entries[16];
        size_t count = 16;
        struct timespec now;
        long elapsed_ms;

        clock_gettime (CLOCK_MONOTONIC, &now);
        elapsed_ms = (long) (now.tv_sec - start.tv_sec) * 1000
                   + (long) (now.tv_nsec - start.tv_nsec) / 1000000;
        assert (elapsed_ms < 5000);

        assert (zlink_registry_query_snapshot (
                  sample->query, NULL, entries, &count)
                == ZLINK_CONFIG_OK);
        for (size_t i = 0; i < count; ++i) {
            if (strcmp (entries[i].channel_name, k_channel_name) == 0) {
                sample->found = 1;
                break;
            }
        }

        if (!sample->found)
            sample_pause_ms (25);
    }
}

int main (void)
{
    registry_query_sample_t sample;
    memset (&sample, 0, sizeof (sample));

    void *ctx = zlink_ctx_new ();
    void *registry = zlink_registry_new (ctx);
    sample.discovery =
      zlink_discovery_new (ctx, ZLINK_AUTO_CONNECT_FANOUT, k_channel_name);
    sample.query = zlink_registry_query_client_new (ctx);
    sample.provider = zlink_socket (ctx, ZLINK_SOCKET_PUB);
    assert (ctx != NULL);
    assert (registry != NULL);
    assert (sample.discovery != NULL);
    assert (sample.query != NULL);
    assert (sample.provider != NULL);

    char registry_pub[256];
    reserve_tcp_endpoint (sample.registry_router, sizeof (sample.registry_router));
    reserve_tcp_endpoint (registry_pub, sizeof (registry_pub));
    reserve_tcp_endpoint (sample.service_endpoint, sizeof (sample.service_endpoint));

    assert (zlink_registry_bind (registry, registry_pub, sample.registry_router)
            == 0);

    zlink_registry_status_t status;
    assert (zlink_registry_status_snapshot (registry, &status) == 0);
    assert (strlen (status.bind_endpoint) > 0);

    assert (zlink_discovery_connect_registry (
              sample.discovery, sample.registry_router)
            == ZLINK_CONNECT_OK);

    void *provider = zlink_thread_start (&registry_query_provider_thread, &sample);
    void *query = zlink_thread_start (&registry_query_client_thread, &sample);
    assert (provider != NULL);
    assert (query != NULL);

    zlink_thread_join (provider);
    zlink_thread_join (query);
    assert (sample.found == 1);

    printf ("[registry-query] service: \"%s\" -> snapshot: found\n",
            k_channel_name);

    zlink_close (sample.provider);
    zlink_registry_query_destroy (&sample.query);
    zlink_discovery_destroy (&sample.discovery);
    zlink_registry_destroy (&registry);
    zlink_ctx_term (ctx);
    return 0;
}
