/* SPDX-License-Identifier: MPL-2.0 */

#include "sample_common.h"

typedef struct
{
    void *provider_discovery;
    void *client_discovery;
    void *provider;
    char registry_router[256];
    char service_endpoint[256];
    int discovered;
} discovery_registry_sample_t;

static void discovery_registry_provider_thread (void *arg_)
{
    discovery_registry_sample_t *sample = (discovery_registry_sample_t *) arg_;

    assert (zlink_socket_attach_discovery (sample->provider,
                                           sample->provider_discovery)
            == 0);
    assert (zlink_bind (sample->provider, sample->service_endpoint)
            == ZLINK_BIND_OK);
}

static void discovery_registry_client_thread (void *arg_)
{
    discovery_registry_sample_t *sample = (discovery_registry_sample_t *) arg_;

    sample->discovered =
      wait_for_discovery_service (sample->client_discovery, k_service_name, 5000);
}

int main (void)
{
    discovery_registry_sample_t sample;
    memset (&sample, 0, sizeof (sample));

    void *ctx = zlink_ctx_new ();
    void *registry = zlink_registry_new (ctx);
    sample.provider_discovery =
      zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_SOCKET, k_service_name);
    sample.client_discovery =
      zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_SOCKET, k_service_name);
    sample.provider = zlink_socket (ctx, ZLINK_SOCKET_PUB);
    assert (ctx != NULL);
    assert (registry != NULL);
    assert (sample.provider_discovery != NULL);
    assert (sample.client_discovery != NULL);
    assert (sample.provider != NULL);

    char registry_pub[256];
    reserve_tcp_endpoint (sample.registry_router, sizeof (sample.registry_router));
    reserve_tcp_endpoint (registry_pub, sizeof (registry_pub));
    reserve_tcp_endpoint (sample.service_endpoint, sizeof (sample.service_endpoint));

    assert (zlink_registry_bind (registry, registry_pub, sample.registry_router)
            == 0);
    assert (zlink_registry_set_broadcast_interval (registry, 50)
            == ZLINK_CONFIG_OK);

    zlink_registry_status_t status;
    assert (zlink_registry_status_snapshot (registry, &status) == 0);
    assert (strlen (status.bind_endpoint) > 0);
    assert (zlink_discovery_connect_registry (
              sample.provider_discovery, sample.registry_router)
            == ZLINK_CONNECT_OK);
    assert (zlink_discovery_connect_registry (
              sample.client_discovery, sample.registry_router)
            == ZLINK_CONNECT_OK);

    void *provider = zlink_thread_start (
      &discovery_registry_provider_thread, &sample);
    void *client = zlink_thread_start (
      &discovery_registry_client_thread, &sample);
    assert (provider != NULL);
    assert (client != NULL);

    zlink_thread_join (provider);
    zlink_thread_join (client);
    assert (sample.discovered == 1);

    printf ("[discovery-registry] service: \"%s\" -> discovered\n",
            k_service_name);

    zlink_close (sample.provider);
    zlink_discovery_destroy (&sample.client_discovery);
    zlink_discovery_destroy (&sample.provider_discovery);
    zlink_registry_destroy (&registry);
    zlink_ctx_term (ctx);
    return 0;
}
