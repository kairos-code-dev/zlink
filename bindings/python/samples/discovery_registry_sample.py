from contextlib import suppress

import zlink
from sample_support import tcp_endpoint, wait_until


SERVICE_NAME = "sample"


def main():
    _, registry_pub_endpoint = tcp_endpoint()
    _, registry_router_endpoint = tcp_endpoint()
    _, service_endpoint = tcp_endpoint()
    ctx = zlink.Context()
    registry = zlink.Registry(ctx)
    discovery = zlink.Discovery(ctx, zlink.ServiceType.SOCKET, SERVICE_NAME)
    query = zlink.RegistryQueryClient(ctx)
    provider = zlink.PubSocket(ctx)
    try:
        registry.bind(registry_pub_endpoint, registry_router_endpoint)
        discovery.connect_registry(registry_router_endpoint)
        provider.attach_discovery(discovery)
        provider.bind(service_endpoint)
        query.connect(registry_router_endpoint)

        def found():
            try:
                entries = query.snapshot()
            except zlink.ConfigError:
                return False
            return any(entry.service_name == SERVICE_NAME for entry in entries)

        wait_until(found, description="discovery registry sample")
        print('[discovery-registry] service: "sample" -> discovered')
    finally:
        for resource in (provider, query, discovery, registry, ctx):
            with suppress(Exception):
                resource.close()


if __name__ == "__main__":
    main()
