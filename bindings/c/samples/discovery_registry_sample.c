/* SPDX-License-Identifier: MPL-2.0 */

#include "sample_common.h"

int main (void)
{
    void *ctx = zlink_ctx_new ();
    void *registry = zlink_registry_new (ctx);
    void *discovery =
      zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_SOCKET, k_service_name);
    void *provider = zlink_socket (ctx, ZLINK_SOCKET_PUB);
    assert (ctx != NULL);
    assert (registry != NULL);
    assert (discovery != NULL);
    assert (provider != NULL);

    char registry_pub[256];
    char registry_router[256];
    char service_endpoint[256];
    reserve_tcp_endpoint (registry_pub, sizeof (registry_pub));
    reserve_tcp_endpoint (registry_router, sizeof (registry_router));
    reserve_tcp_endpoint (service_endpoint, sizeof (service_endpoint));

    assert (zlink_registry_bind (registry, registry_pub, registry_router) == 0);

    zlink_registry_status_t status;
    assert (zlink_registry_status_snapshot (registry, &status) == 0);
    assert (strlen (status.bind_endpoint) > 0);

    assert (zlink_discovery_connect_registry (discovery, registry_router) == 0);
    assert (zlink_socket_attach_discovery (provider, discovery) == 0);
    assert (zlink_bind (provider, service_endpoint) == 0);

    size_t count = 0;
    zlink_registry_topology_entry_t entries[16];
    int found = 0;
    struct timespec start;
    clock_gettime (CLOCK_MONOTONIC, &start);
    while (!found) {
        struct timespec now;
        clock_gettime (CLOCK_MONOTONIC, &now);
        long elapsed_ms =
          (long) (now.tv_sec - start.tv_sec) * 1000
          + (long) (now.tv_nsec - start.tv_nsec) / 1000000;
        assert (elapsed_ms < 5000);

        count = 16;
        assert (zlink_registry_topology_snapshot (registry, entries, &count) == 0);
        for (size_t i = 0; i < count; ++i) {
            if (strcmp (entries[i].service_name, k_service_name) == 0) {
                found = 1;
                break;
            }
        }
    }

    printf ("[discovery-registry] service: \"%s\" -> discovered\n",
            k_service_name);

    zlink_close (provider);
    zlink_discovery_destroy (&discovery);
    zlink_registry_destroy (&registry);
    zlink_ctx_term (ctx);
    return 0;
}
