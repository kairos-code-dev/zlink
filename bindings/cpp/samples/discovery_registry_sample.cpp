/* SPDX-License-Identifier: MPL-2.0 */

#include "sample_common.hpp"

int main ()
{
    zlink::context_t ctx;
    zlink::service::registry_t registry (ctx);
    zlink::service::discovery_t discovery (
      ctx, zlink::service_type::socket, detail::k_spot_service);
    zlink::service_monitor_handle_t discovery_monitor = discovery.monitor_open (
      zlink::service_monitor_event::discovery_service_up);
    zlink::pub_socket_t provider (ctx);
    assert (registry.valid ());
    assert (discovery.valid ());
    assert (discovery_monitor.valid ());
    assert (provider.valid ());

    const std::string registry_pub = detail::unique_tcp ("registry-pub");
    const std::string registry_router = detail::unique_tcp ("registry-router");
    const std::string service_endpoint = detail::unique_tcp ("registry-service");
    registry.bind (registry_pub, registry_router);
    discovery.connect_registry (registry_router);
    provider.attach_discovery (discovery);
    provider.bind (service_endpoint);

    assert (detail::wait_for_monitor_readable (discovery_monitor, 5000));
    const zlink::service_event_t event = discovery_monitor.recv ();
    assert (event.event_type
            == zlink::service_monitor_event::discovery_service_up);
    assert (event.service_name == detail::k_spot_service);
    if ((event.detail_flags & ZLINK_SERVICE_EVENT_DETAIL_ENDPOINT) != 0)
        assert (event.endpoint == service_endpoint);

    std::printf (
      "[discovery-registry] service: \"%s\" -> discovered\n",
      detail::k_spot_service);
    return 0;
}
