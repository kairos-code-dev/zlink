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
    provider = zlink.PubSocket(ctx)
    monitor = discovery.monitor_open()
    try:
        registry.bind(registry_pub_endpoint, registry_router_endpoint)
        discovery.connect_registry(registry_router_endpoint)
        provider.attach_discovery(discovery)
        observed = {"done": False}

        def on_event(event):
            if event.service_name == SERVICE_NAME:
                observed["done"] = True

        monitor.on_event(on_event)
        provider.bind(service_endpoint)
        wait_until(lambda: observed["done"], description="discovery registry sample")
        print('[discovery-registry] service: "sample" -> discovered')
    finally:
        for resource in (monitor, provider, discovery, registry, ctx):
            with suppress(Exception):
                resource.close()


if __name__ == "__main__":
    main()
