/* SPDX-License-Identifier: MPL-2.0 */

#include "sample_common.hpp"

int main ()
{
    zlink::context_t ctx;
    zlink::service::registry_t registry (ctx);
    zlink::service::discovery_t provider_discovery (
      ctx, zlink::auto_connect_type::fanout, detail::k_spot_service);
    zlink::service::discovery_t client_discovery (
      ctx, zlink::auto_connect_type::fanout, detail::k_spot_service);
    zlink::pub_socket_t provider (ctx);
    assert (registry.valid ());
    assert (provider_discovery.valid ());
    assert (client_discovery.valid ());
    assert (provider.valid ());

    const std::string registry_pub = detail::unique_tcp ("registry-pub");
    const std::string registry_router = detail::unique_tcp ("registry-router");
    const std::string service_endpoint = detail::unique_tcp ("registry-service");
    registry.bind (registry_pub, registry_router);
    registry.set_broadcast_interval (50);
    provider_discovery.connect_registry (registry_router);
    client_discovery.connect_registry (registry_router);
    provider.attach_discovery (provider_discovery);
    provider.bind (service_endpoint);

    const auto deadline = std::chrono::steady_clock::now ()
                          + std::chrono::milliseconds (5000);
    bool discovered = false;
    while (std::chrono::steady_clock::now () < deadline) {
        const std::vector<zlink::member_peer_entry_t> peers =
          client_discovery.member_peers ();
        for (size_t i = 0; i < peers.size (); ++i) {
            if (peers[i].channel_name == detail::k_spot_service
                && peers[i].endpoint == service_endpoint) {
                discovered = true;
                break;
            }
        }
        if (discovered)
            break;
        std::this_thread::sleep_for (std::chrono::milliseconds (10));
    }
    assert (discovered);

    std::printf (
      "[discovery-registry] service: \"%s\" -> discovered\n",
      detail::k_spot_service);
    return 0;
}
