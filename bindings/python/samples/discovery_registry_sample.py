from contextlib import suppress

import zlink
from sample_support import tcp_endpoint, wait_until


SERVICE_NAME = "sample"


def main():
    _, registry_pub_endpoint = tcp_endpoint()
    _, registry_router_endpoint = tcp_endpoint()
    _, service_endpoint = tcp_endpoint()
    ctx = zlink.create_context()
    registry = zlink.create_registry(ctx)
    discovery = zlink.create_discovery(ctx, zlink.AutoConnectType.FANOUT, SERVICE_NAME)
    query = zlink.create_registry_query_client(ctx)
    provider = zlink.create_pub_socket(ctx)
    try:
        registry.bind(registry_pub_endpoint, registry_router_endpoint)
        discovery.connect_registry(registry_router_endpoint)
        provider.attach_discovery(discovery)
        provider.bind(service_endpoint)
        query.connect(registry_router_endpoint)

        def found():
            try:
                entries = query.topology()
            except zlink.ConfigError:
                return False
            return any(entry.channel_name == SERVICE_NAME for entry in entries)

        wait_until(found, description="discovery registry sample")
        print('[discovery-registry] service: "sample" -> discovered')
    finally:
        for resource in (provider, query, discovery, registry, ctx):
            with suppress(Exception):
                resource.close()


if __name__ == "__main__":
    main()
